// Fill out your copyright notice in the Description page of Project Settings.


#include "StreamingLevelSaveSequence.h"

#include "StreamingLevelSaveLibrary.h"
#include "StreamingLevelSaveSubsystem.h"

UStreamingLevelSaveSubsystem* UStreamingLevelSaveSequence::GetSubsystem() const
{
	// 修复：使用初始化时绑定的子系统，切图期间世界可能为空。
	return Subsystem;
}

FString UStreamingLevelSaveSequence::GetSaveSlotName(FString SlotName) const
{
	return SaveFileName + "/" + SlotName;
}

void UStreamingLevelSaveSequence::DeleteCurrentSaveFiles()
{
	UStreamingLevelSaveLibrary::DeleteSaveGame(SaveFileName);
}

void UStreamingLevelSaveSequence::SequenceFinish()
{
	CleanUp();
	if (IsValid(Subsystem) && Subsystem->SaveLoadSequence == this) Subsystem->EndSaveLoadSequence();
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
	// TArray<FString> TempFiles;
	// IFileManager::Get().FindFiles(TempFiles, *(SaveFolder + "/"));
	// Create directory if not exist. Fixing loading problem.
	if (!PlatformFile.DirectoryExists(*UStreamingLevelSaveLibrary::GetTempFileDir()))
	{
		PlatformFile.CreateDirectory(*UStreamingLevelSaveLibrary::GetTempFileDir());
	}
	PlatformFile.CopyDirectoryTree(*UStreamingLevelSaveLibrary::GetTempFileDir(), *(SaveFolder + "/"), true);
}

class UWorld* UStreamingLevelSaveSequence::GetWorld() const
{
	if (const auto SaveSubsystem = Cast<UStreamingLevelSaveSubsystem>(GetOuter()))
	{
		return SaveSubsystem->GetGameInstance()->GetWorld();
	}
	
	return UObject::GetWorld();
}

bool UStreamingLevelSaveSequence::CheckSaveFileNameValid_Implementation(const FString& SlotName) const
{
	return true;
}

bool UStreamingLevelSaveSequence::IsAllowSaving_Implementation() const
{
	return true;
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
	if (!IsValid(Subsystem)) return;
	const auto TempFiles = Subsystem->CollectTempSaveFiles();
	for (const auto FileName : TempFiles)
	{
		PlatformFile.CopyFile(*(SaveFolder + "/" + FileName), *(UStreamingLevelSaveLibrary::GetTempFileDir() + FileName));
	}
}
