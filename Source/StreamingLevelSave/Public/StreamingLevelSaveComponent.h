// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StreamingLevelSaveComponent.generated.h"

class UStreamingLevelSaveSubsystem;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class STREAMINGLEVELSAVE_API UStreamingLevelSaveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStreamingLevelSaveComponent();
	
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = "Streaming Level Save")
	bool bTickCheckCell = false;

	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = "Streaming Level Save")
	float VelocityThreshold = 0.1f;

protected:
	UStreamingLevelSaveSubsystem* GetSubsystem() const;
	
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;	
};
