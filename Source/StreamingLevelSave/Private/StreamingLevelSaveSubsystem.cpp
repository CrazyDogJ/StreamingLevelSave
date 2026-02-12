#include "StreamingLevelSaveSubsystem.h"

#include "ImageUtils.h"
#include "StreamingLevelSaveComponent.h"
#include "StreamingLevelSaveInterface.h"
#include "StreamingLevelSaveLibrary.h"
#include "StreamingLevelSaveSequence.h"
#include "StreamingLevelSaveSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Streaming/LevelStreamingDelegates.h"

#define LIBRARY UStreamingLevelSaveLibrary
#define SETTINGS UStreamingLevelSaveSettings
#define INTERFACE IStreamingLevelSaveInterface

FStreamingLevelSaveData* UStreamingLevelSaveSubsystem::GetOrAddCellSaveData(const FString& CellName,
	TMap<FString, FStreamingLevelSaveData>& InMapping)
{
	if (CellName.IsEmpty())
	{
		return nullptr;
	}
	
	if (const auto Found = InMapping.Find(CellName))
	{
		return Found;
	}
	
	return &InMapping.Add(CellName);
}

FStreamingLevelSaveData* UStreamingLevelSaveSubsystem::GetOrAddTempCellSaveData(const FString& CellName)
{
	return GetOrAddCellSaveData(CellName, TempSaveDatas);
}

void UStreamingLevelSaveSubsystem::BeginSaveLoadSequence(FString SaveFileName, bool bSaving)
{
	if (!SaveLoadSequence)
	{
		if (bSaving)
		{
			OnPreSave.Broadcast();
		}
		else
		{
			OnPreLoad.Broadcast();
		}
		
		SaveLoadSequence = UStreamingLevelSaveSequence::NewSaveLoadSequence(GetWorld(), SaveFileName, bSaving);
	}
}

void UStreamingLevelSaveSubsystem::EndSaveLoadSequence(bool bSaving)
{
	if (SaveLoadSequence)
	{
		SaveLoadSequence->ConditionalBeginDestroy();
		SaveLoadSequence = nullptr;

		if (bSaving)
		{
			OnSaveComplete.Broadcast();
		}
		else
		{
			OnLoadComplete.Broadcast();
		}
	}
}

bool UStreamingLevelSaveSubsystem::IsSaving() const
{
	if (SaveLoadSequence && SaveLoadSequence->bSaving)
	{
		return true;
	}
	
	return false;
}

bool UStreamingLevelSaveSubsystem::IsLoading() const
{
	if (SaveLoadSequence && !SaveLoadSequence->bSaving)
	{
		return true;
	}
	
	return false;
}

TArray<FString> UStreamingLevelSaveSubsystem::CollectTempSaveFiles()
{
	for (const auto Level : VisibleStreamingLevels)
	{
		// TODO : Not async here, may cause performance issue.
		SaveLevelInternal(Level->GetLoadedLevel(), true, false);
	}
	
	TArray<FString> TempFiles;
	IFileManager::Get().FindFiles(TempFiles, *LIBRARY::GetTempFileFolder());

	return TempFiles;
}

void UStreamingLevelSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	if (SETTINGS::GetEnableSaveLoad())
	{
		AssignDelegates();
	}
}

void UStreamingLevelSaveSubsystem::Deinitialize()
{
	Super::Deinitialize();

	RemoveDelegates();
	
	if (SETTINGS::GetClearTempFilesOnEndGame())
	{
		ClearAllTempFiles();
	}
}

bool UStreamingLevelSaveSubsystem::SaveTempData(const FString& LevelStreamingName, const FStreamingLevelSaveData& SaveData)
{
	if (LevelStreamingName.IsEmpty()) return false;

	TArray<uint8> Data;
	FMemoryWriter MemoryWriter(Data, true);
	FObjectAndNameAsStringProxyArchive WriterProxy(MemoryWriter, /*bInLoadIfFindFails*/false);
	FStreamingLevelSaveData::StaticStruct()->SerializeBin(WriterProxy, &const_cast<FStreamingLevelSaveData&>(SaveData));
	
	const FString FilePath = LIBRARY::MakeTempFilePath(LevelStreamingName);
	const bool bSuccess = FFileHelper::SaveArrayToFile(Data, *FilePath);

	return bSuccess;
}

bool UStreamingLevelSaveSubsystem::LoadTempData(const FString& LevelStreamingName, FStreamingLevelSaveData& SaveData)
{
	if (LevelStreamingName.IsEmpty()) return false;
	
	TArray<uint8> BinaryData;
	const FString FilePath = LIBRARY::MakeTempFilePath(LevelStreamingName);
	if (!FPaths::FileExists(FilePath)) return false;
	if (!FFileHelper::LoadFileToArray(BinaryData, *FilePath)) return false;

	FMemoryReader MemoryReader(BinaryData, true);
	FObjectAndNameAsStringProxyArchive ReaderProxy(MemoryReader, /*bInLoadIfFindFails*/true);
	FStreamingLevelSaveData::StaticStruct()->SerializeBin(ReaderProxy, &SaveData);
	
	return true;
}

void UStreamingLevelSaveSubsystem::SaveLevelInternal(const ULevel* Level, bool bOnlyCollect, bool bAsync)
{
	const auto StreamingLevelName = LIBRARY::GetLevelName(Level);
	if (const auto Found = GetOrAddTempCellSaveData(StreamingLevelName))
	{
		// Capture datas in game thread.
		StorePersistentActors(Level, Found, bOnlyCollect);
		StoreRuntimeActors(Level, Found, bOnlyCollect);
		// Async save data to hard drive.
		if (bAsync)
		{
			// Async
			auto CachedData = *Found;
			UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Level, CachedData, StreamingLevelName]()
			{
				SaveTempData(StreamingLevelName, CachedData);
				AsyncTask(ENamedThreads::GameThread, [StreamingLevelName, Level, this]()
				{
					TempSaveDatas.Remove(StreamingLevelName);
				});
			});
		}
		else
		{
			// Sync
			SaveTempData(StreamingLevelName, *Found);
			TempSaveDatas.Remove(StreamingLevelName);
		}
	}
}

void UStreamingLevelSaveSubsystem::LoadLevelInternal(const ULevel* Level)
{
	const auto StreamingLevelName = LIBRARY::GetLevelName(Level);
	// Async load data from hard drive and then restore actor's stage.
	UE::Tasks::Launch(UE_SOURCE_LOCATION,
	[this, StreamingLevelName, Level]()
	{
		FStreamingLevelSaveData LoadedData;
		LoadTempData(StreamingLevelName, LoadedData);
		
		// Main thread task.
		AsyncTask(ENamedThreads::GameThread, [LoadedData, Level, this]()
		{
			if (IsValid(Level))
			{
				FStreamingLevelSaveData GameLoadedData = LoadedData;
				RestorePersistentActors(Level, &GameLoadedData);
				RestoreRuntimeActors(&GameLoadedData);
			}
		});
	});
}

void UStreamingLevelSaveSubsystem::StoreObjectUnsafe(UObject* Object, FInstancedStruct& SaveData)
{
	SaveData = INTERFACE::Execute_GetSaveData(Object);
}

void UStreamingLevelSaveSubsystem::RestoreObjectUnsafe(UObject* Object, const FInstancedStruct& SaveData)
{
	INTERFACE::Execute_LoadSaveData(Object, SaveData);
}

void UStreamingLevelSaveSubsystem::StoreActorComponents(const AActor* Actor, TMap<FGuid, FInstancedStruct>& Mappings)
{
	TInlineComponentArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	for (const auto Itr : Components)
	{
		if (FGuid Id; LIBRARY::IsSaveInterfaceObject(Itr, Id))
		{
			FInstancedStruct SaveDataStruct;
			StoreObjectUnsafe(Itr, SaveDataStruct);
			Mappings.Add(Id, SaveDataStruct);
		}
	}
}

void UStreamingLevelSaveSubsystem::RestoreActorComponents(const AActor* Actor,
	const TMap<FGuid, FInstancedStruct>& Mappings)
{
	TInlineComponentArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	for (const auto Itr : Components)
	{
		if (FGuid Id; LIBRARY::IsSaveInterfaceObject(Itr, Id))
		{
			if (const auto FoundData = Mappings.Find(Id))
			{
				RestoreObjectUnsafe(Itr, *FoundData);
			}
		}
	}
}

void UStreamingLevelSaveSubsystem::StorePersistentActors(const ULevel* Level, FStreamingLevelSaveData* SaveData, bool bCollectOnly) const
{
	if (!SaveData || !Level)
	{
		return;
	}

	for (auto Itr : Level->Actors)
	{
		if (!IsValid(Itr)) continue;
		if (Itr->HasAnyFlags(RF_ClassDefaultObject)) continue;
		if (Itr->IsActorBeingDestroyed()) continue;

		if (FGuid Id; LIBRARY::IsSaveInterfaceObject(Itr, Id))
		{
			if (!bCollectOnly)
			{
				Itr->OnDestroyed.RemoveAll(this);
			}
			FInstancedStruct SaveDataStruct;
			StoreObjectUnsafe(Itr, SaveDataStruct);
			SaveData->SaveDatas.Add(Id, SaveDataStruct);
		}
		StoreActorComponents(Itr, SaveData->SaveDatas);
	}
}

void UStreamingLevelSaveSubsystem::RestorePersistentActors(const ULevel* Level, FStreamingLevelSaveData* SaveData)
{
	if (!SaveData || !Level)
	{
		return;
	}
	
	for (auto Itr : Level->Actors)
	{
		if (!IsValid(Itr)) continue;
		if (Itr->HasAnyFlags(RF_ClassDefaultObject)) continue;
		if (Itr->IsActorBeingDestroyed()) continue;
		
		FGuid Id;
		if (LIBRARY::IsSaveInterfaceObject(Itr, Id))
		{
			// Destroy state.
			if (SaveData->DestroyedActors.Find(Id))
			{
				Itr->Destroy(true);
			}
			else
			{
				if (const auto FoundData = SaveData->SaveDatas.Find(Id))
				{
					RestoreObjectUnsafe(Itr, *FoundData);
				}
				
				Itr->OnDestroyed.AddDynamic(this, &ThisClass::OnLevelActorDestroyed);
			}
		}
		RestoreActorComponents(Itr, SaveData->SaveDatas);
	}
}

void UStreamingLevelSaveSubsystem::StoreRuntimeActors(const ULevel* InLevel,
	FStreamingLevelSaveData* SaveData, bool bCollectOnly)
{
	TArray<UStreamingLevelSaveComponent*> Components;
	for (const auto Itr : RuntimeActorComponents)
	{
		// Get current overlap or associate level
		const ULevel* Level = nullptr;
		FGuid Id;
		if (LIBRARY::IsSaveInterfaceObject(Itr->GetOwner(), Id))
		{
			Level = INTERFACE::Execute_GetAssociateLevel(Itr->GetOwner());
		}

		// If actor is in the same place that level streaming is making invisible.
		if (Level && LIBRARY::GetLevelName(InLevel) == LIBRARY::GetLevelName(Level))
		{
			if (LIBRARY::IsSaveInterfaceObject(Itr->GetOwner(), Id))
			{
				FStreamingLevelSaveRuntimeData RuntimeData;
				StoreRuntimeActor(Itr->GetOwner(), RuntimeData);
				SaveData->RuntimeActorsSaveDatas.Add(RuntimeData);
				Components.Add(Itr);
			}
		}
	}
	if (!bCollectOnly)
	{
		for (const auto Itr : Components)
		{
			Itr->GetOwner()->Destroy(true);
		}
	}
}

void UStreamingLevelSaveSubsystem::StoreRuntimeActor(AActor* Actor,
	FStreamingLevelSaveRuntimeData& RuntimeActorData)
{
	FInstancedStruct SaveDataStruct;
	StoreObjectUnsafe(Actor, SaveDataStruct);
	RuntimeActorData.AdditionalData = SaveDataStruct;
	RuntimeActorData.ActorClass = Actor->GetClass();
	RuntimeActorData.ActorTransform = Actor->GetActorTransform();
	RuntimeActorData.ActorVelocity = Actor->GetVelocity();
	StoreActorComponents(Actor, RuntimeActorData.Components);
}

void UStreamingLevelSaveSubsystem::RestoreRuntimeActors(FStreamingLevelSaveData* SaveData)
{
	for (const auto Itr : SaveData->RuntimeActorsSaveDatas)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (const auto NewActor = GetWorld()->SpawnActor(Itr.ActorClass.LoadSynchronous(), &Itr.ActorTransform, SpawnParams))
		{
			if (NewActor->GetRootComponent())
			{
				NewActor->GetRootComponent()->ComponentVelocity = Itr.ActorVelocity;
			}

			RestoreObjectUnsafe(NewActor, Itr.AdditionalData);
			RestoreActorComponents(NewActor, Itr.Components);
		}
	}
}

void UStreamingLevelSaveSubsystem::ClearAllTempFiles()
{
	IFileManager::Get().DeleteDirectory(*LIBRARY::GetTempFileFolder(), true, true);
}

void UStreamingLevelSaveSubsystem::AssignDelegates()
{
	FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::PreLoadMapWithContext);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::PostLoadMapWithWorld);
	
	FLevelStreamingDelegates::OnLevelBeginMakingVisible.AddUObject(this, &ThisClass::OnLevelBeginMakingVisible);
	FLevelStreamingDelegates::OnLevelBeginMakingInvisible.AddUObject(this, &ThisClass::OnLevelBeginMakingInvisible);
}

void UStreamingLevelSaveSubsystem::RemoveDelegates()
{
	FCoreUObjectDelegates::PreLoadMapWithContext.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	
	FLevelStreamingDelegates::OnLevelBeginMakingVisible.RemoveAll(this);
	FLevelStreamingDelegates::OnLevelBeginMakingInvisible.RemoveAll(this);
}

void UStreamingLevelSaveSubsystem::TakeScreenshot()
{
	// Memory-based screenshot request
	UGameViewportClient* ViewportClient = UGameplayStatics::GetPlayerController(GetWorld(), 0)->GetLocalPlayer()->ViewportClient;
	ViewportClient->OnScreenshotCaptured().AddUObject(this, &ThisClass::OnScreenshotCaptured);
	FScreenshotRequest::RequestScreenshot(false);
}

UTexture2D* UStreamingLevelSaveSubsystem::LoadScreenshot(const FSaveGameScreenshotData PngData)
{
	if (PngData.Data.Num() > 0)
		return FImageUtils::ImportBufferAsTexture2D(PngData.Data);

	return nullptr;
}

void UStreamingLevelSaveSubsystem::OnLevelActorDestroyed(AActor* DestroyedActor)
{
	if (const ULevel* Level = INTERFACE::Execute_GetAssociateLevel(DestroyedActor))
	{
		if (const auto Found = GetOrAddTempCellSaveData(LIBRARY::GetLevelName(Level)))
		{
			Found->DestroyedActors.Add(FGuid::NewDeterministicGuid(DestroyedActor->GetPathName()));
		}
	}
}

void UStreamingLevelSaveSubsystem::OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	const auto ViewportClient = UGameplayStatics::GetPlayerController(GetWorld(), 0)->GetLocalPlayer()->ViewportClient;
	ViewportClient->OnScreenshotCaptured().RemoveAll(this);
	
	// Convert down to PNG
	TArray<uint8> PngData;
	FImageUtils::ThumbnailCompressImageArray(Width, Height, Colors, PngData);
	
	ScreenShotCaptured.Broadcast(FSaveGameScreenshotData(PngData));
}

void UStreamingLevelSaveSubsystem::PreLoadMapWithContext(const FWorldContext& WorldContext, const FString& String)
{
	// Only work in world partition world and authority mode.
	if (!WorldContext.World()->GetWorldPartition() && WorldContext.World()->GetNetMode() != NM_Client)
	{
		SaveLevelInternal(WorldContext.World()->GetCurrentLevel(), false, true);
	}
}

void UStreamingLevelSaveSubsystem::PostLoadMapWithWorld(UWorld* World)
{
	// Only work in world partition world and authority mode.
	if (!World->GetWorldPartition() && World->GetNetMode() != NM_Client)
	{
		LoadLevelInternal(World->GetCurrentLevel());
	}
}

void UStreamingLevelSaveSubsystem::OnLevelBeginMakingVisible(UWorld* World, const ULevelStreaming* LevelStreaming,
                                                             ULevel* Level)
{
	// Only work in authority mode.
	if (World && World->GetNetMode() != NM_Client)
	{
		VisibleStreamingLevels.Add(LevelStreaming);
		LoadLevelInternal(LevelStreaming->GetLoadedLevel());
	}
}

void UStreamingLevelSaveSubsystem::OnLevelBeginMakingInvisible(UWorld* World, const ULevelStreaming* LevelStreaming,
	ULevel* Level)
{
	// Only work in authority mode.
	if (World && World->GetNetMode() != NM_Client)
	{
		VisibleStreamingLevels.Remove(LevelStreaming);
		SaveLevelInternal(LevelStreaming->GetLoadedLevel(), false, true);
	}
}
