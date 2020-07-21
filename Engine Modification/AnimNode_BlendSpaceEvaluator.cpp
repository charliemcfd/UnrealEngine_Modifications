// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_BlendSpaceEvaluator.h"
#include "Animation/BlendSpaceBase.h"
#include "Animation/AnimTrace.h"
//Charlie - Distance matching implementation
#include "Animation/AnimSequence.h"
//~Charlie

/////////////////////////////////////////////////////
// FAnimNode_BlendSpaceEvaluator

FAnimNode_BlendSpaceEvaluator::FAnimNode_BlendSpaceEvaluator()
	: FAnimNode_BlendSpacePlayer()
	, NormalizedTime(0.f)
{
}

void FAnimNode_BlendSpaceEvaluator::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);
	//Charlie - Distance Matching implementation
	if (bUseDistanceMatching && BlendSpace && BlendSpace->GetSkeleton())
	{
		const float inputDistance = NormalizedTime;
		const float prevTime = InternalTimeAccumulator;

		FSmartName CurveName;
		BlendSpace->GetSkeleton()->GetSmartNameByName(USkeleton::AnimCurveMappingName, DistanceCurve, CurveName);

		const FVector BlendInput(X, Y, Z);
		TArray<FBlendSampleData> BlendSamples;
		BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSamples);

		//Note: To get curve info in a release build when the animations are compressed, it might be necessary to do this:
		// FAnimCurveBufferAcess CurveBuffer = FAnimCurveBufferAccess(BlendSamples[0].Animation, CurveName.UID);

		//Create an empty curve that will be used for blending
		FRichCurve BlendedDistanceCurve;
		//Create keys (Normalized)
		for (int i = 0; i <= 10; i++)
		{
			//We add some blank keys along the cruve with a value of 0
			BlendedDistanceCurve.Keys.Add(FRichCurveKey(0.1f * i, 0.0f));
		}

		//Iterate over all of the sample animations in the blend space
		for (const FBlendSampleData& sample : BlendSamples)
		{
			if (sample.Animation)
			{
				const float timeMultiplier = sample.Animation->GetPlayLength();
				//Grab the curve from the sample
				const FFloatCurve* Curve = (const FFloatCurve*)sample.Animation->GetCurveData().GetCurveData(CurveName.UID);
				//If the curve does not exist, skip
				if (!Curve)
					continue;
				//Iterate over every key in our BLENDED distance curve
				for (FRichCurveKey& key : BlendedDistanceCurve.Keys)
				{
					//Either sqaush or stretch the other curve so that it fits within our blended curve
					//This works as the keys within the blended curve were set at a normalized time (0->1)
					const float adjustedTime = key.Time * timeMultiplier;

					//Calculate the new value for our blended key by evaluating the anim curve at the 
					//adjusted time. We then multiply this evaluated value by the weighting of the sample animation
					//and then add it to the running total for the blended key.
					const float value = Curve->Evaluate(adjustedTime) * sample.GetWeight() + key.Value;
					key.Value = value;
				}
			}
		}

		//Get Min, max and Delta values
		const float maxDistance = BlendedDistanceCurve.Keys.Last().Value;
		const float minDistance = BlendedDistanceCurve.Keys[0].Value;
		const float deltaDistance = maxDistance - minDistance; //This can be used to determine if the curve goes positive or negative. For now, assume positive

		//Calculate the Distance to match
		float distance = bUseDeltaDistance ? BlendedDistanceCurve.Eval(prevTime) + inputDistance : inputDistance;

		if (bLoop)
		{
			//Handle cases where the distance loops past the start or the end
			if (distance > maxDistance)
			{
				distance = minDistance + fmod(distance, deltaDistance);
			}
			else if (distance < minDistance)
			{
				distance = maxDistance - fmod(distance, deltaDistance);
			}
		}

		if (distance >= maxDistance)
		{
			InternalTimeAccumulator = 1.0f;
		}
		else if (distance <= minDistance)
		{
			InternalTimeAccumulator = 0.0f;
		}
		else
		{
			float time = 0.0f;
			FRichCurveKey prevKey;
			//Iterate over our blended curve
			for (const FRichCurveKey& key : BlendedDistanceCurve.Keys)
			{
				//If the value of the key is greater than our current distance travelled
				if (key.Value > distance)
				{
					//Calculate the distance delta between this key and the previous key
					const float delta = key.Value - prevKey.Value;
					//Calculate the alpha so that we know how "far" between the keys we were
					const float alpha = delta != 0.0f ? (distance - prevKey.Value) / delta : 0.0f;
					//Calculate a new time based on that alpha
					time = prevKey.Time + alpha * (key.Time - prevKey.Time);
					//Stop iteration
					break;
				}
				prevKey = key;
			}

			//Normalize Playtime
			InternalTimeAccumulator = time;
		}
	}
	else
	{
		InternalTimeAccumulator = FMath::Clamp(NormalizedTime, 0.f, 1.f);
	}
	//~Charlie
	PlayRate = 0.f;

	UpdateInternal(Context);

	TRACE_ANIM_NODE_VALUE(Context, TEXT("Name"), BlendSpace ? *BlendSpace->GetName() : TEXT("None"));
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Blend Space"), BlendSpace);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Playback Time"), InternalTimeAccumulator);
}

void FAnimNode_BlendSpaceEvaluator::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);

	DebugLine += FString::Printf(TEXT("('%s' Play Time: %.3f)"), *BlendSpace->GetName(), InternalTimeAccumulator);
	DebugData.AddDebugItem(DebugLine, true);
}
