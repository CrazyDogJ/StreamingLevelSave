// Fill out your copyright notice in the Description page of Project Settings.


#include "StreamingLevelSaveSettings.h"

bool UStreamingLevelSaveSettings::GetEnableSaveLoad()
{
	if (const auto Settings = GetDefault<UStreamingLevelSaveSettings>())
	{
		return Settings->bEnableSaveLoad;
	}
	
	return true;
}

bool UStreamingLevelSaveSettings::GetClearTempFilesOnEndGame()
{
	if (const auto Settings = GetDefault<UStreamingLevelSaveSettings>())
	{
		return Settings->bClearTempFilesOnEndGame;
	}
	
	return false;
}

TSubclassOf<UStreamingLevelSaveSequence> UStreamingLevelSaveSettings::GetDefaultSaveSequenceClass()
{
	if (const auto Settings = GetDefault<UStreamingLevelSaveSettings>())
	{
		return Settings->DefaultSaveSequenceClass.LoadSynchronous();
	}

	return nullptr;
}

FString UStreamingLevelSaveSettings::GetTempFileFolder()
{
	if (const auto Settings = GetDefault<UStreamingLevelSaveSettings>())
	{
		return Settings->TempSaveFilesFolder;
	}
	
	return "TempLevels";
}
