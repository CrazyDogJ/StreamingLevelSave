// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StreamingLevelSaveSequence.h"
#include "UObject/Object.h"
#include "StreamingLevelSaveSettings.generated.h"

UCLASS(Config = StreamingLevelSave, DefaultConfig)
class STREAMINGLEVELSAVE_API UStreamingLevelSaveSettings : public UObject
{
	GENERATED_BODY()

public:
	static bool GetEnableSaveLoad();
	static bool GetClearTempFilesOnEndGame();
	static TSubclassOf<UStreamingLevelSaveSequence> GetDefaultSaveSequenceClass();
	static FString GetTempFileFolder();

public:
	UPROPERTY(Config, EditAnywhere)
	bool bEnableSaveLoad = true;
	
	UPROPERTY(Config, EditAnywhere)
	bool bClearTempFilesOnEndGame = false;

	UPROPERTY(Config, EditAnywhere)
	TSoftClassPtr<UStreamingLevelSaveSequence> DefaultSaveSequenceClass;
	
	UPROPERTY(Config, EditAnywhere)
	FString TempSaveFilesFolder = "TempLevels";
};
