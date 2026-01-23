// Fill out your copyright notice in the Description page of Project Settings.


#include "StreamingLevelSaveSequence.h"

#include "StreamingLevelSave.h"
#include "StreamingLevelSaveLibrary.h"
#include "StreamingLevelSaveSettings.h"
#include "StreamingLevelSaveSubsystem.h"

UStreamingLevelSaveSubsystem* UStreamingLevelSaveSequence::GetSubsystem() const
{
	return GetWorld()->GetGameInstance()->GetSubsystem<UStreamingLevelSaveSubsystem>();
}

UStreamingLevelSaveSequence* UStreamingLevelSaveSequence::NewSaveLoadSequence(UWorld* InWorld, FString SaveFileName, bool bSaving)
{
	const auto Class = GetDefault<UStreamingLevelSaveSettings>()->GetDefaultSaveSequenceClass();
	const auto NewSequence = NewObject<UStreamingLevelSaveSequence>(InWorld, Class);
	NewSequence->World = InWorld;
	NewSequence->SaveFileName = SaveFileName;
	NewSequence->bSaving = bSaving;
	if (NewSequence->bSaving)
	{
		NewSequence->BeginSave();
	}
	else
	{
		NewSequence->BeginLoad();
	}

	return NewSequence;
}

FString UStreamingLevelSaveSequence::GetSaveSlotName(FString SlotName) const
{
	return UStreamingLevelSaveLibrary::MakeSaveGameDir(SaveFileName) + SlotName;
}

void UStreamingLevelSaveSequence::SequenceFinish()
{
	if (bSaving)
	{
		CopyTempFilesToSavePath();
	}
	else
	{
		CopySaveFilesToTempPath();
	}
	
	GetSubsystem()->EndSaveLoadSequence(bSaving);
}

void UStreamingLevelSaveSequence::CopySaveFilesToTempPath() const
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Find folder
	const FString SaveFolder = UStreamingLevelSaveLibrary::MakeSaveGameDir(SaveFileName) + LevelsSaveFolder;
	if (!PlatformFile.DirectoryExists(*SaveFolder))
	{
		return;
	}

	// Copy temp files to save folder.
	TArray<FString> TempFiles;
	IFileManager::Get().FindFiles(TempFiles, *(SaveFolder + "/"));
	for (const FString& FileName : TempFiles)
	{
		PlatformFile.CopyDirectoryTree(*UStreamingLevelSaveLibrary::GetTempFileDir(), *FileName, true);
	}
}

void UStreamingLevelSaveSequence::CopyTempFilesToSavePath() const
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Create folder
	const FString SaveFolder = UStreamingLevelSaveLibrary::MakeSaveGameDir(SaveFileName) + LevelsSaveFolder;
	if (!PlatformFile.DirectoryExists(*SaveFolder))
	{
		if (!PlatformFile.CreateDirectoryTree(*SaveFolder))
		{
			return;
		}
	}

	// Copy temp files to save folder.
	const auto TempFiles = GetSubsystem()->CollectTempSaveFiles();
	for (const auto FileName : TempFiles)
	{
		PlatformFile.CopyFile(*(SaveFolder + "/" + FileName), *(UStreamingLevelSaveLibrary::GetTempFileDir() + FileName));
	}
}
