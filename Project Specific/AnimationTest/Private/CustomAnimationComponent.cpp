// Fill out your copyright notice in the Description page of Project Settings.


#include "CustomAnimationComponent.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"

const FString ContextString(TEXT("Custom Animation Context"));
const FName OutSectionName = FName(TEXT("Out"));

// Sets default values for this component's properties
UCustomAnimationComponent::UCustomAnimationComponent()
{
}


// Called when the game starts
void UCustomAnimationComponent::BeginPlay()
{
}

int32 UCustomAnimationComponent::PlayCustomAnimation(FName customAnimationName, int32 numLoops, FName slot, bool freezeOnLastFrame)
{
	//Attempts to get pointer to the animation asset
	TSoftObjectPtr<UAnimSequenceBase> animationAsset = GetAssetPtrForName(customAnimationName);

	if (!animationAsset.IsNull())
	{
		USkeletalMeshComponent* meshComponent = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
		if (meshComponent)
		{
			UAnimInstance* animInstance = meshComponent->GetAnimInstance();
			if (animInstance)
			{
				//If the animation has not yet been loaded, load it, and then play it
				if (animationAsset.IsPending())
				{
					//TODO::Change to async load
					animationAsset.LoadSynchronous();
					return PlayAnimationAsset(animInstance, animationAsset.Get(), numLoops, customAnimationName, slot, freezeOnLastFrame);
				}
				//If the animation has already been loaded, play it
				else if (animationAsset.IsValid())
				{
					return PlayAnimationAsset(animInstance, animationAsset.Get(), numLoops, customAnimationName, slot, freezeOnLastFrame);
				}
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Animation asset for custom animation %s in Datatable %s has not been assigned"), *(customAnimationName.ToString()), *(CustomAnimationDataTable->GetName()));
	}

	return INDEX_NONE;
}

int32 UCustomAnimationComponent::PlayAnimationAsset(UAnimInstance* animInstance, UAnimSequenceBase* asset, int32 numLoops, const FName& customAnimationName, const FName& slot, const bool freezeOnLastFrame)
{
	UAnimMontage* montage = nullptr;
	FAnimMontageInstance* montageInstance = nullptr;

	//Animation assets can either be montage assets or sequence assets and should be handled differently.
	//Montage assets can be played straight away, sequence assets need to be played by being converted into a dynamic montage (Done in the anim instance class)

	if (asset->IsA(UAnimMontage::StaticClass()))
	{
		montage = CastChecked<UAnimMontage>(asset);
		animInstance->Montage_Play(montage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, true, numLoops);

		montageInstance = animInstance->GetActiveInstanceForMontage(montage);
	}
	else
	{
		montage = animInstance->PlaySlotAnimationAsDynamicMontage(asset, slot, 0.25f, 0.25f, 1.0f, numLoops);
		montageInstance = animInstance->GetActiveInstanceForMontage(montage);

		//If a montge instance was successfully created for this dynamic motage, add the source montage to the map for future lookup
		if (montageInstance)
		{
			DynamicMontageMap.Add(customAnimationName, montage);
		}
	}

	//Bind Callback
	if (montage && montageInstance)
	{
		MontageIdNameMap.Add(montageInstance->GetInstanceID(), customAnimationName);
		montageInstance->OnMontageEnded.BindUObject(this, &UCustomAnimationComponent::OnMontageEnded, montageInstance->GetInstanceID());
		montageInstance->OnMontageSectionEnded.BindUObject(this, &UCustomAnimationComponent::OnMontageSectionEnded);
		montageInstance->bEnableAutoBlendOut = !freezeOnLastFrame;
		return montageInstance->GetInstanceID();
	}

	return INDEX_NONE;
}

void UCustomAnimationComponent::StopCustomAnimation(FName customAnimationName, CustomAnimationStopMode stopMode, bool blendOut, bool useOutSection, bool freezeOnLastFrame)
{
	//Get the mesh component. Make sure there is an active anim instance
	USkeletalMeshComponent* meshComponent = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
	if (meshComponent)
	{
		UAnimInstance* animInstance = meshComponent->GetAnimInstance();
		if (animInstance)
		{
			//Look up  the custom animation in the dynamic montage map
			UAnimMontage* montage = nullptr;
			montage = DynamicMontageMap.FindRef(customAnimationName);
			if (montage)
			{
				StopDynamicMontage(montage, animInstance, stopMode, blendOut, freezeOnLastFrame);
				return;
			}
			//If the custom anim was not found above, attempt to find it via the custom anim database
			if (!montage)
			{
				TSoftObjectPtr<UAnimSequenceBase> animationAsset = GetAssetPtrForName(customAnimationName);
				//If the asset is currently pending, it has not yet been loaded
				if (animationAsset.IsPending())
				{
					UE_LOG(LogTemp, Warning, TEXT("Attempting to stop Custom Animation %s in the DataTable %s but it has not yet been loaded"), *(customAnimationName.ToString()), *(CustomAnimationDataTable->GetName()));
				}
				else if (animationAsset.IsValid())
				{
					//At this point it is expected that the asset is probably a montage, however it could be the case that it is a sequence
					//if the stop call has been called on a sequence that has not yet been played. Check for this

					UAnimSequenceBase* asset = animationAsset.Get();
					if (asset->IsA(UAnimMontage::StaticClass()))
					{
						montage = CastChecked<UAnimMontage>(asset);
						if (montage)
						{
							StopDatatableMontage(montage, animInstance, stopMode, blendOut, useOutSection, freezeOnLastFrame);
						}
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("Attempting to stop Custom Animation %s in the DataTable %s but it has not yet been played"), *(customAnimationName.ToString()), *(CustomAnimationDataTable->GetName()));
					}
				}
			}
		}
	}
}

void UCustomAnimationComponent::StopDynamicMontage(UAnimMontage* montage, UAnimInstance* animInstance, CustomAnimationStopMode stopMode, bool blendOut, bool freezeOnLastFrame)
{
	/*
	Used for stopping custom animations that were converted into dynamic montages when originally played.
	These animations will be of "anim sequence" type in the data table
	*/

	if (montage)
	{
		//As the montage was found within the dynamic montage map, it means that we need to edit the loop variable within the montage class itself,
		//rather than the montage instance.
		//Note: A dynamic montage will only have one slot track (0)
		if (montage->SlotAnimTracks.IsValidIndex(0))
		{
			if (montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsValidIndex(0))
			{
				//Get Montage Instance
				FAnimMontageInstance* montageInstance = animInstance->GetActiveInstanceForMontage(montage);
				if (montageInstance)
				{
					montageInstance->bEnableAutoBlendOut = !freezeOnLastFrame;

					switch (stopMode)
					{
					case StopMode_Immediate:
					{
						float blendOutTime = blendOut ? montage->BlendOut.GetBlendTime() : 0.0f;
						animInstance->Montage_Stop(blendOutTime, montage);
						break;
					}
					//The dynamic montages do not have an "out" section, so nstead if this is selected
					//we should just let them finish their loop
					case StopMode_OnCurrentSectionEnd:
					{
						//Set the looping count on the base montage object to be 1
						montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].LoopingCount = 1;
						//Get the new length of the ontage
						float newSequenceLength = montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetLength();
						//Get the current position of th emontage instance (Current pos = play time)
						float modPosition = montageInstance->GetPosition();
						//FMod with the new length to get a new play position that should be equivalent to the old one, but appropriate to the new lengths
						modPosition = fmod(modPosition, newSequenceLength);
						//Set the new play position
						montageInstance->SetPosition(modPosition);
						//Update the length of the base montage to be the newly calculated length
						montage->SequenceLength = newSequenceLength;
						break;
					}
					default:
						UE_LOG(LogTemp, Warning, TEXT("Attempting to stop Custom Animation with invalid Stop Mode"));
						break;
					}
				}

			}
		}
	}
}

void UCustomAnimationComponent::StopDatatableMontage(UAnimMontage* montage, UAnimInstance* animInstance, CustomAnimationStopMode stopMode, bool blendOut, bool useOutSection, bool freezeOnLastFrame)
{
	/*
	This method is for stopping custom animations that were stored in the data table as montages
	*/

	if (montage && animInstance)
	{
		//Attempt to get the montage instance for this montage
		FAnimMontageInstance* montageInstance = animInstance->GetActiveInstanceForMontage(montage);
		if (montageInstance)
		{
			montageInstance->bEnableAutoBlendOut = !freezeOnLastFrame;

			int32 currentSecctionIndex = montage->GetSectionIndex(montageInstance->GetCurrentSection());
			int32 endSectionIndex = montage->GetSectionIndex(OutSectionName);

			//The montage *should* have an out section, but if it doesn't then change the stop mode
			//so that we will not try to use it. Print warning
			if (useOutSection && endSectionIndex == INDEX_NONE)
			{
				useOutSection = false;
				UE_LOG(LogTemp, Warning, TEXT("Attempting to stop custom animation %s by using 'Out section', but it does not exist"), *(montage->GetName()));
			}

			montageInstance->bCustomAnimationBlendOut = blendOut;

			switch (stopMode)
			{
			case StopMode_Immediate:
			{
				//If we want to exit the montage right now, but also want to use the out section
				if (useOutSection)
				{
					montageInstance->JumpToSectionName("Out");
				}
				//If we want to exit the montage right now without using the out section
				else
				{
					float blendOutTime = blendOut ? montage->BlendOut.GetBlendTime() : 0.0f;
					animInstance->Montage_Stop(blendOutTime, montage);
				}
				break;
			}
			case StopMode_OnCurrentSectionEnd:
			{
				montageInstance->CustomAnimationLoopingSectionLoops = 0;
				//If we want to exit the montage when the current section finishes, and want to use the out section
				if (useOutSection)
				{
					montageInstance->SetNextSectionID(currentSecctionIndex, endSectionIndex);
				}
				//If we want to exit the montage when the current section finished, but do not want to use the out section
				else
				{
					montageInstance->SetNextSectionID(currentSecctionIndex, -1);
				}
				break;
			}
			default:
				break;
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Attempting to stop Custom Animation %s but there is no assigned montage instance."), *(montage->GetName()));
		}
	}
}

void UCustomAnimationComponent::OnMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 MontageInstanceId)
{
	//Note: OnMontageEnded should be the LAST event called by the anim instance for a given montage instance, therefore removing our local references to it here 
	//should be fine

	//Find and remove the name of the custom Anim
	FName customAnimationName = MontageIdNameMap.FindAndRemoveChecked(MontageInstanceId);

	//Find and remove from the dynamic map
	DynamicMontageMap.Remove(customAnimationName);

	OnCustomAnimationEnded.Broadcast(customAnimationName, MontageInstanceId);
}

void UCustomAnimationComponent::OnMontageSectionEnded(UAnimMontage* Montage, int previousSection, int nextSection, int32 MontageInstanceId)
{
	//Find the name of the custom animation
	FName customAnimationName = MontageIdNameMap.FindChecked(MontageInstanceId);
	FName sectionName = Montage->GetSectionName(previousSection);

	//If the sections are different, fire a section ended event. Otherwise, fire a section looped event
	if (previousSection != nextSection)
	{
		OnCustomAnimationSectionEnded.Broadcast(customAnimationName, MontageInstanceId, sectionName);
	}
	else
	{
		OnCustomAnimationSectionLooped.Broadcast(customAnimationName, MontageInstanceId, sectionName);
	}
}

TSoftObjectPtr<UAnimSequenceBase> UCustomAnimationComponent::GetAssetPtrForName(const FName& customAnimationName)
{
	FCustomAnimationStructure* tableRow = CustomAnimationDataTable->FindRow<FCustomAnimationStructure>(customAnimationName, ContextString, true);
	if (tableRow)
	{
		return tableRow->AnimationAsset;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Row not found for Custom Animation %s in Datatable %s"), *(customAnimationName.ToString()), *(CustomAnimationDataTable->GetName()));
	}
	return nullptr;
}