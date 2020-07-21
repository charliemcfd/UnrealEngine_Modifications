// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimTrace.h"

float FAnimNode_SequenceEvaluator::GetCurrentAssetTime()
{
	return ExplicitTime;
}

float FAnimNode_SequenceEvaluator::GetCurrentAssetLength()
{
	return Sequence ? Sequence->SequenceLength : 0.0f;
}

/////////////////////////////////////////////////////
// FAnimSequenceEvaluatorNode

void FAnimNode_SequenceEvaluator::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_AssetPlayerBase::Initialize_AnyThread(Context);
	bReinitialized = true;
}

void FAnimNode_SequenceEvaluator::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
}

void FAnimNode_SequenceEvaluator::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);

	if (Sequence)
	{
		//Charlie - Distance Matching Implementation
		if (bShouldUseExplicityTimeAsDistanceCurveLookup && Sequence->GetSkeleton())
		{
			FSmartName CurveName;
			Sequence->GetSkeleton()->GetSmartNameByName(USkeleton::AnimCurveMappingName, DistanceCurve, CurveName);
			const FFloatCurve* Curve = (const FFloatCurve*)Sequence->GetCurveData().GetCurveData(CurveName.UID);
			if (Curve)
			{
				//Get the previous position of the curve
				const float prevDistance = Curve->Evaluate(StartPosition);
				//Get the current position of the curve (If we are using the input param (ExplicitTime) as a delta representation, perform a calculation)
				float currentDistance = bDistanceCurveInputIsDeltaDistance ? prevDistance + ExplicitTime : ExplicitTime;

				const float maxTime = Sequence->GetPlayLength();
				const float maxDistance = Curve->Evaluate(maxTime);
				float distance = 0.0f;
				float time = currentDistance < prevDistance ? 0.0f : StartPosition;

				if (currentDistance > maxDistance)
				{
					if (!bShouldLoop)
					{
						time = maxTime;
						//Skip the loop
						distance = currentDistance;
					}
					else
					{
						//If we are in a loop, do fmod for the distance
						time = 0.0f;
						currentDistance = fmod(currentDistance, maxDistance);
					}
				}

				//Progress time and look at the distance traveled
				//When distance is bigger, break out of loop
				if (prevDistance != currentDistance)
				{
					FRichCurveKey prevKey;
					const TArray<FRichCurveKey>& floatCurve = Curve->FloatCurve.GetCopyOfKeys();
					//Iterate over keys in curve
					for (const FRichCurveKey& key : floatCurve)
					{
						//If the value of the key is greater than our current distance travelled
						if (key.Value >= currentDistance)
						{
							//Calculate the distance delta between this key and the previous key
							const float delta = key.Value - prevKey.Value;
							//Calculate the alpha so that we know how "far" between the keys we were
							const float alpha = delta != 0.0f ? (currentDistance - prevKey.Value) / delta : 0.0f;
							//Calculate a new time based on that alpha
							time = prevKey.Time + alpha * (key.Time - prevKey.Time);
							//Stop iteration
							break;
						}
						prevKey = key;
					}

					StartPosition = time;
					InternalTimeAccumulator = time;
				}
			}
		}
		else
		{
			//~Charlie
			// Clamp input to a valid position on this sequence's time line.
			ExplicitTime = FMath::Clamp(ExplicitTime, 0.f, Sequence->SequenceLength);

			if ((!bTeleportToExplicitTime || (GroupIndex != INDEX_NONE)) && (Context.AnimInstanceProxy->IsSkeletonCompatible(Sequence->GetSkeleton())))
			{
				if (bReinitialized)
				{
					switch (ReinitializationBehavior)
					{
					case ESequenceEvalReinit::StartPosition: InternalTimeAccumulator = StartPosition; break;
					case ESequenceEvalReinit::ExplicitTime: InternalTimeAccumulator = ExplicitTime; break;
					}

					InternalTimeAccumulator = FMath::Clamp(InternalTimeAccumulator, 0.f, Sequence->SequenceLength);
				}

				float TimeJump = ExplicitTime - InternalTimeAccumulator;
				if (bShouldLoop)
				{
					if (FMath::Abs(TimeJump) > (Sequence->SequenceLength * 0.5f))
					{
						if (TimeJump > 0.f)
						{
							TimeJump -= Sequence->SequenceLength;
						}
						else
						{
							TimeJump += Sequence->SequenceLength;
						}
					}
				}

				// if you jump from front to end or end to front, your time jump is 0.f, so nothing moves
				// to prevent that from happening, we set current accumulator to explicit time
				if (TimeJump == 0.f)
				{
					InternalTimeAccumulator = ExplicitTime;
				}

				const float DeltaTime = Context.GetDeltaTime();
				const float RateScale = Sequence->RateScale;
				const float PlayRate = FMath::IsNearlyZero(DeltaTime) || FMath::IsNearlyZero(RateScale) ? 0.f : (TimeJump / (DeltaTime * RateScale));
				CreateTickRecordForNode(Context, Sequence, bShouldLoop, PlayRate);
			}
			else
			{
				InternalTimeAccumulator = ExplicitTime;
			}
		}
	}
	//~Charlie
	bReinitialized = false;

	TRACE_ANIM_NODE_VALUE(Context, TEXT("Name"), Sequence != nullptr ? Sequence->GetFName() : NAME_None);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Sequence"), Sequence);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("InputTime"), ExplicitTime);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Time"), InternalTimeAccumulator);
}

void FAnimNode_SequenceEvaluator::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)
	check(Output.AnimInstanceProxy != nullptr);
	if ((Sequence != nullptr) && (Output.AnimInstanceProxy->IsSkeletonCompatible(Sequence->GetSkeleton())))
	{
		Sequence->GetAnimationPose(Output.Pose, Output.Curve, FAnimExtractContext(InternalTimeAccumulator, Output.AnimInstanceProxy->ShouldExtractRootMotion()));
	}
	else
	{
		Output.ResetToRefPose();
	}
}

void FAnimNode_SequenceEvaluator::OverrideAsset(UAnimationAsset* NewAsset)
{
	if(UAnimSequenceBase* NewSequence = Cast<UAnimSequenceBase>(NewAsset))
	{
		Sequence = NewSequence;
	}
}

void FAnimNode_SequenceEvaluator::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);
	
	DebugLine += FString::Printf(TEXT("('%s' InputTime: %.3f, Time: %.3f)"), *GetNameSafe(Sequence), ExplicitTime, InternalTimeAccumulator);
	DebugData.AddDebugItem(DebugLine, true);
}
