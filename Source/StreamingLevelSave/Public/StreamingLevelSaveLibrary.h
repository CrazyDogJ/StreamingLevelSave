#pragma once

#include "CoreMinimal.h"
#include "StreamingLevelSaveInterface.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StreamingLevelSaveLibrary.generated.h"

UCLASS()
class STREAMINGLEVELSAVE_API UStreamingLevelSaveLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	static FString GetTempFileFolder();
	static FString GetTempFileDir();
	static FString MakeTempFilePath(const FString& TempFileName);

	static FString MakeSaveGameFolder(FString SaveGameName);
	static FString MakeSaveGameDir(FString SaveGameName);
	
	static FString GetLevelName(const FString& PackageName);
	static FString GetLevelName(const ULevel* Level);
	
	/** Check given actor is a runtime actor. */
	UFUNCTION(BlueprintPure, Category = "Streaming Level Save")
	static bool IsRuntimeObject(const UObject* InObject);
	
	/** Check given object should be saved. */
	static bool IsSaveInterfaceObject(const UObject* Object, FGuid& OutId);
	
	/** Serialize object with default logic. */
	static FObjectDefaultSaveData GetObjectDefaultSaveData(UObject* Object);
	
	/** Get associate level by default logic.
	 * Persistent actor : Actor->GetLevel()
	 * Runtime actor : GetOverlappedWorldPartitionRuntimeCell2D()
	 * No WP : Actor->GetWorld()->GetCurrentLevel()
	 */
	static ULevel* GetAssociateLevelDefault(const AActor* Actor);

	/** As name said, query given location in which runtime cell.
	 * You should not call it in not WP level.
	 */
	static void GetOverlappedWorldPartitionRuntimeCell2D(const UWorld* World, const FVector& Location, const UWorldPartitionRuntimeCell*& OutCell);

	/** Init determined guid with given string. */
	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save")
	static void InitGuidFromString(FString String, UPARAM(ref)FGuid& Guid);

public:
	// Interface ============
	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save|Interface")
	static FGuid GetIdentityGuidInternal(const UObject* Object);

	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save|Interface")
	static FInstancedStruct GetSaveDataInternal(UObject* Object);

	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save|Interface")
	static void LoadSaveDataInternal(UObject* Object, const FInstancedStruct& SaveData);

	UFUNCTION(BlueprintCallable, Category = "Streaming Level Save|Interface")
	static ULevel* GetAssociateLevelInternal(UObject* Object);
	// Interface ============

	UFUNCTION(BlueprintPure, Category = "Streaming Level Save|Save Game")
	static FString GetUniqueNetIdString(const FUniqueNetIdRepl& NetId);

	UFUNCTION(BlueprintPure, Category = "Streaming Level Save|Save Game")
	static bool IsDirectoryExistInSaveGame(FString DirectoryName);
};
