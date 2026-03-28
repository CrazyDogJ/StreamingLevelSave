// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StreamingLevelSaveSubsystem.h"
#include "UObject/Object.h"
#include "StreamingLevelSaveSequence.generated.h"

class USaveGame;

UCLASS(BlueprintType, Blueprintable)
class STREAMINGLEVELSAVE_API UStreamingLevelSaveSequence : public UObject
{
	GENERATED_BODY()
	
public:
	UStreamingLevelSaveSubsystem* GetSubsystem() const;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Base")
	bool bSaving = true;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Base")
	bool bMultiplay = false;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Base")
	bool bLan = false;
	
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Base")
	bool bProgressing = false;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Base")
	FString SaveFileName = "DefaultSaveGame";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Base")
	FString LevelsSaveFolder = "Levels";

	UFUNCTION(BlueprintPure, Category = "Streaming Level Save")
	FString GetSaveSlotName(FString SlotName) const;

	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save")
	void DeleteCurrentSaveFiles();
	
	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save")
	void SequenceFinish();
	
	// Event that you should begin serialize datas to SaveGamePaths.
	UFUNCTION(BlueprintImplementableEvent, Category = "Streaming Level Save")
	void BeginSave();
	
	// Event that begin loading datas.
	UFUNCTION(BlueprintImplementableEvent, Category = "Streaming Level Save")
	void BeginLoad();

	UFUNCTION(BlueprintImplementableEvent, Category = "Streaming Level Save")
	void CleanUp();

	UFUNCTION(BlueprintNativeEvent, Category = "Streaming Level Save")
	bool IsAllowSaving() const;
	
	UFUNCTION(BlueprintNativeEvent, Category = "Streaming Level Save")
	bool CheckSaveFileNameValid(const FString& SlotName) const;

	void CopyTempFilesToSavePath() const;
	void CopySaveFilesToTempPath() const;

	void SetSubsystem(UStreamingLevelSaveSubsystem* InSubsystem) { Subsystem = InSubsystem; }
	
protected:
	UPROPERTY()
	UStreamingLevelSaveSubsystem* Subsystem = nullptr;
	
#if WITH_EDITOR
	virtual bool ImplementsGetWorld() const override { return true; }
#endif
	virtual class UWorld* GetWorld() const override;
};
