#pragma once

#include "CoreMinimal.h"
#include "StreamingLevelSaveComponent.h"
#include "StreamingLevelSaveStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "StreamingLevelSaveSubsystem.generated.h"

class UStreamingLevelSaveSequence;
class UStreamingLevelSaveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSaveGameDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnScreenshotCapturedBlueprint, FSaveGameScreenshotData, Data);

UCLASS()
class STREAMINGLEVELSAVE_API UStreamingLevelSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static FStreamingLevelSaveData* GetOrAddCellSaveData(const FString& CellName, TMap<FString, FStreamingLevelSaveData>& InMapping);
	FStreamingLevelSaveData* GetOrAddTempCellSaveData(const FString& CellName);
	
	static void StoreRuntimeActor(AActor* Actor, FStreamingLevelSaveRuntimeData& RuntimeActorData);
	
	UPROPERTY(BlueprintAssignable)
	FSaveGameDelegate OnPreSave;

	UPROPERTY(BlueprintAssignable)
	FSaveGameDelegate OnSaveComplete;

	UPROPERTY(BlueprintAssignable)
	FSaveGameDelegate OnPreLoad;

	UPROPERTY(BlueprintAssignable)
	FSaveGameDelegate OnLoadComplete;
	
	UPROPERTY(BlueprintReadOnly)
	TSet<UStreamingLevelSaveComponent*> RuntimeActorComponents;
	
	UPROPERTY(BlueprintReadOnly)
	TMap<FString, FStreamingLevelSaveData> TempSaveDatas;

	UPROPERTY(BlueprintReadOnly)
	TSet<const ULevelStreaming*> VisibleStreamingLevels;

	// Saving Loading ==========================
	UPROPERTY(BlueprintReadOnly)
	UStreamingLevelSaveSequence* SaveLoadSequence = nullptr;
	
	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save Subsystem")
	void BeginSaveLoadSequence(FString SaveFileName, bool bSaving);

	void EndSaveLoadSequence(bool bSaving);
	
	UFUNCTION(BlueprintPure, Category = "Streaming Level Save Subsystem")
	bool IsSaving() const;

	UFUNCTION(BlueprintPure, Category = "Streaming Level Save Subsystem")
	bool IsLoading() const;
	// Saving Loading ==========================
	
	TArray<FString> CollectTempSaveFiles();

	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save Subsystem")
	static void ClearAllTempFiles();
	
	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save Subsystem")
	void AssignDelegates();

	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save Subsystem")
	void RemoveDelegates();

	UPROPERTY(BlueprintAssignable)
	FOnScreenshotCapturedBlueprint ScreenShotCaptured;
	
	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save Subsystem")
	void TakeScreenshot();

	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save Subsystem")
	UTexture2D* LoadScreenshot(FSaveGameScreenshotData PngData);
	
protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	// Save temp data.
	static bool SaveTempData(const FString& LevelStreamingName, const FStreamingLevelSaveData& SaveData);
	// Load temp data.
	static bool LoadTempData(const FString& LevelStreamingName, FStreamingLevelSaveData& SaveData);

	// Save level ptr.
	void SaveLevelInternal(const ULevel* Level, bool bOnlyCollect, bool bAsync = true);
	// Load level ptr.
	void LoadLevelInternal(const ULevel* Level);
	
	// Unsafe store object.
	static void StoreObjectUnsafe(UObject* Object, FInstancedStruct& SaveData);
	// Unsafe restore object.
	static void RestoreObjectUnsafe(UObject* Object, const FInstancedStruct& SaveData);

	// Store actor components
	static void StoreActorComponents(const AActor* Actor, TMap<FGuid, FInstancedStruct>& Mappings);
	// Restore actor components
	static void RestoreActorComponents(const AActor* Actor, const TMap<FGuid, FInstancedStruct>& Mappings);
	
	void StorePersistentActors(const ULevel* Level, FStreamingLevelSaveData* SaveData, bool bCollectOnly) const;
	void RestorePersistentActors(const ULevel* Level, FStreamingLevelSaveData* SaveData);

	void StoreRuntimeActors(const ULevel* InLevel, FStreamingLevelSaveData* SaveData, bool bCollectOnly);
	void RestoreRuntimeActors(FStreamingLevelSaveData* SaveData);

private:
	// Delegate bindings ======
	UFUNCTION()
	void OnLevelActorDestroyed(AActor* DestroyedActor);

	void OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors);
	
	void PreLoadMapWithContext(const FWorldContext& WorldContext, const FString& String);
	void PostLoadMapWithWorld(UWorld* World);
	
	void OnLevelBeginMakingVisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* Level);
	void OnLevelBeginMakingInvisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* Level);
	// Delegate bindings ======
};
