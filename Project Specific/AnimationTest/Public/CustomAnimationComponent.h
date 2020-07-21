// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Animation/AnimSequenceBase.h"
#include "CustomAnimationComponent.generated.h"

USTRUCT(BlueprintType)
struct FCustomAnimationStructure : public FTableRowBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
		TSoftObjectPtr<UAnimSequenceBase> AnimationAsset;
};

UENUM(BlueprintType)
enum CustomAnimationStopMode
{
	StopMode_Immediate,
	StopMode_OnCurrentSectionEnd
};

//Delegate Declarations
//Single
DECLARE_DELEGATE_OneParam(FOnCustomAnimationEnded, FName /*customAnimationName*/);
DECLARE_DELEGATE_TwoParams(FOnCustomAnimationSectionEnded, FName /*customAnimationName*/, FName /*sectionName*/);
DECLARE_DELEGATE_TwoParams(FOnCustomAnimationSectionLooped, FName /*customAnimationName*/, FName /*sectionName*/);
//Multicast
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCustomAnimationEndedMCDelegate, FName, CustomAnimationName, int32, MontageInstanceID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCustomAnimationSectionEndedMCDelegate, FName, CustomAnimationName, int32, MontageInstanceID, FName, SectionName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCustomAnimationSectionLoopedMCDelegate, FName, CustomAnimationName, int32, MontageInstanceID, FName, SectionName);

/*
Custom Animation Component
*/

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class ANIMATIONTEST_API UCustomAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UCustomAnimationComponent();

	//=====Methods
	UFUNCTION(BlueprintCallable, Category = Animation)
	int32 PlayCustomAnimation(FName customAnimationName, int32 numLoops, FName slot, bool freezeOnLastFrame);

	UFUNCTION(BlueprintCallable, Category = Animation)
	void StopCustomAnimation(FName customAnimationName, CustomAnimationStopMode stopMode, bool blendOut = true, bool useOutSection = true, bool freezeOnLastFrame = false);

	//Callbacks
	void OnMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 MontageInstanceId);
	void OnMontageSectionEnded(UAnimMontage* Montage, int previousSection, int nextSection, int32 MontageInstanceId);

	//Delegates
	UPROPERTY(BlueprintAssignable)
	FOnCustomAnimationEndedMCDelegate OnCustomAnimationEnded;

	UPROPERTY(BlueprintAssignable)
	FOnCustomAnimationSectionEndedMCDelegate OnCustomAnimationSectionEnded;

	UPROPERTY(BlueprintAssignable)
	FOnCustomAnimationSectionLoopedMCDelegate OnCustomAnimationSectionLooped;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	int32 PlayAnimationAsset(UAnimInstance* aniInstance, UAnimSequenceBase* asset, int32 numLoops, const FName& customAnimationName, const FName& slot, const bool freezeOnLastFrame);
	TSoftObjectPtr<UAnimSequenceBase> GetAssetPtrForName(const FName& customAnimationName);

	void StopDynamicMontage(UAnimMontage* montage, UAnimInstance* animInstance, CustomAnimationStopMode stopMode, bool blendOut, bool freezeOnLastFrame);
	void StopDatatableMontage(UAnimMontage* montage, UAnimInstance* animInstance, CustomAnimationStopMode stopMode, bool blendOut, bool useOutSection, bool freezeOnLastFrame);

public:	
	//=====Members
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UDataTable* CustomAnimationDataTable;
protected:
	//Map for noting references to dynamic montages
	TMap<FName, UAnimMontage*> DynamicMontageMap;

	//Map for quick lookup of custom animation name when given a montage.
	//This is needed as Dynamically created montages will not be given the names of their respective custom animation.
	TMap<int32, FName> MontageIdNameMap;

		
};
