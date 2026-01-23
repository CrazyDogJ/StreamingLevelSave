#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "StreamingLevelSaveStructs.generated.h"

USTRUCT(BlueprintType)
struct FStreamingLevelSaveRuntimeData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TSoftClassPtr<AActor> ActorClass;
	
	UPROPERTY(BlueprintReadOnly)
	FTransform ActorTransform;

	UPROPERTY(BlueprintReadOnly)
	FVector ActorVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	TMap<FGuid, FInstancedStruct> Components;
	
	UPROPERTY(BlueprintReadOnly)
	FInstancedStruct AdditionalData;
};

USTRUCT(BlueprintType)
struct FStreamingLevelSaveData
{
	GENERATED_BODY()

	/** Save destroyed datas for persistent actors. */
	UPROPERTY(BlueprintReadOnly)
	TSet<FGuid> DestroyedActors;

	/** Save datas for persistent actors and components. */
	UPROPERTY(BlueprintReadOnly)
	TMap<FGuid, FInstancedStruct> SaveDatas;
	
	UPROPERTY(BlueprintReadOnly)
	TArray<FStreamingLevelSaveRuntimeData> RuntimeActorsSaveDatas;
};

USTRUCT(BlueprintType)
struct FSaveGameScreenshotData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TArray<uint8> Data;
};