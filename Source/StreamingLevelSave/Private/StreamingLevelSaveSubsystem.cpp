#include "StreamingLevelSaveSubsystem.h"

#include "ImageUtils.h"
#include "StreamingLevelSaveComponent.h"
#include "StreamingLevelSaveInterface.h"
#include "StreamingLevelSaveLibrary.h"
#include "StreamingLevelSaveSequence.h"
#include "StreamingLevelSaveSettings.h"
#include "Kismet/GameplayStatics.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"

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

void UStreamingLevelSaveSubsystem::BeginSaveLoadSequence(FString SaveFileName, bool bSaving, bool bMultiplay, bool bLan)
{
	if (!IsSaving() && !IsLoading())
	{
		if (bSaving)
		{
			OnPreSave.Broadcast();
		}
		else
		{
			OnPreLoad.Broadcast();
		}
		
		SaveLoadSequence->SaveFileName = SaveFileName;
		SaveLoadSequence->bSaving = bSaving;
		SaveLoadSequence->bProgressing = true;
		SaveLoadSequence->bMultiplay = bMultiplay;
		SaveLoadSequence->bLan = bLan;
		
		// Call level saving and loading at begin.
		if (bSaving)
		{
			SaveLoadSequence->CopyTempFilesToSavePath();
			SaveLoadSequence->BeginSave();
		}
		else
		{
			// Set current save slot name.
			if (SaveLoadSequence->CheckSaveFileNameValid(SaveFileName))
			{
				SetCurrentSaveSlotName(SaveFileName);
				SaveLoadSequence->CopySaveFilesToTempPath();
				SaveLoadSequence->BeginLoad();
			}
			else
			{
				// If save file is not valid, end load right now.
				EndSaveLoadSequence();
			}
		}
	}
}

void UStreamingLevelSaveSubsystem::EndSaveLoadSequence()
{
	if (SaveLoadSequence)
	{
		SaveLoadSequence->bProgressing = false;
		
		if (SaveLoadSequence->bSaving)
		{
			OnSaveComplete.Broadcast();
		}
		else
		{
			OnLoadComplete.Broadcast();
		}
	}
}

bool UStreamingLevelSaveSubsystem::IsAllowSaving() const
{
	if (SaveLoadSequence)
	{
		return SaveLoadSequence->IsAllowSaving();
	}

	return true;
}

bool UStreamingLevelSaveSubsystem::IsSaving() const
{
	if (SaveLoadSequence->bProgressing && SaveLoadSequence->bSaving)
	{
		return true;
	}
	
	return false;
}

bool UStreamingLevelSaveSubsystem::IsLoading() const
{
	if (SaveLoadSequence->bProgressing && !SaveLoadSequence->bSaving)
	{
		return true;
	}
	
	return false;
}

TArray<FString> UStreamingLevelSaveSubsystem::CollectTempSaveFiles()
{
	// Save levels.
	for (const auto Level : VisibleStreamingLevels)
	{
		// TODO : Not async here, may cause performance issue.
		SaveLevelInternal(Level, true, false);
	}
	
	TArray<FString> TempFiles;
	IFileManager::Get().FindFiles(TempFiles, *LIBRARY::GetTempFileFolder());

	return TempFiles;
}

TArray<FString> UStreamingLevelSaveSubsystem::FindMetaDataFiles(FString MetaDataFileName)
{
	TArray<FString> Result;
	const FString SaveGamesDir = FPaths::ProjectSavedDir() / TEXT("SaveGames");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	if (!PlatformFile.DirectoryExists(*SaveGamesDir))
	{
		return Result;
	}
	
	TArray<FString> TempFiles;
	IFileManager::Get().FindFilesRecursive(TempFiles, *SaveGamesDir, *MetaDataFileName, true, false);
	for (const auto Itr : TempFiles)
	{
		// Relative to save games folder.
		FString RelativePath = Itr;
		FPaths::MakePathRelativeTo(RelativePath, *SaveGamesDir);
		// Remove extension and "SaveGames/".
		RelativePath = FPaths::GetBaseFilename(RelativePath, false);
		FString RightSplit;
		RelativePath.Split("SaveGames/", nullptr, &RightSplit);
		// Finish
		Result.Add(RightSplit);
	}

	return Result;
}

void UStreamingLevelSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	if (SETTINGS::GetEnableSaveLoad())
	{
		AssignDelegates();
	}

	const auto Class = GetDefault<UStreamingLevelSaveSettings>()->GetDefaultSaveSequenceClass();
	SaveLoadSequence = NewObject<UStreamingLevelSaveSequence>(this, Class);
	SaveLoadSequence->SetSubsystem(this);
}

void UStreamingLevelSaveSubsystem::Deinitialize()
{
	Super::Deinitialize();

	if (SaveLoadSequence)
	{
		SaveLoadSequence->CleanUp();
		SaveLoadSequence->ConditionalBeginDestroy();
		SaveLoadSequence = nullptr;
	}
	
	RemoveDelegates();
	
	if (SETTINGS::GetClearTempFilesOnEndGame())
	{
		ClearAllTempFiles();
	}
}

void UStreamingLevelSaveSubsystem::Tick(float DeltaTime)
{
	// Stop runtime actors to get out of bound.
	for (const auto Itr : RuntimeActorComponents)
	{
		if (Itr->bTickCheckCell)
		{
			const auto Actor = Itr->GetOwner();
			const auto Velocity = Actor->GetVelocity();
			const auto VelThreshold = Itr->VelocityThreshold;
			if (Velocity.Length() >= VelThreshold)
			{
				for (const auto Level : VisibleStreamingLevels)
				{
					const auto OwnerActor = Itr->GetOwner();
					if (const auto Cell = Level->GetWorldPartitionRuntimeCell())
					{
						// If actor is trying to move outside of bound, we immediately stop that actor.
						if (!Cell->GetCellBounds().IsInsideXY(OwnerActor->GetActorLocation() + Velocity))
						{
							OwnerActor->GetRootComponent()->ComponentVelocity = FVector::ZeroVector;
						}
					}
				}
			}
		}
	}
}

TStatId UStreamingLevelSaveSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UStreamingLevelSaveSubsystem, STATGROUP_Tickables);
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
	// Dont save level in ignore list.
	if (const auto Settings = GetDefault<UStreamingLevelSaveSettings>())
	{
		if (Settings->IgnoreLevelNames.Find(StreamingLevelName) >= 0)
		{
			return;
		}
	}
	
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
			UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Level, CachedData, StreamingLevelName, bOnlyCollect]()
			{
				SaveTempData(StreamingLevelName, CachedData);
				if (!bOnlyCollect)
				{
					AsyncTask(ENamedThreads::GameThread, [StreamingLevelName, Level, this]()
					{
						TempSaveDatas.Remove(StreamingLevelName);
					});
				}
			});
		}
		else
		{
			// Sync
			SaveTempData(StreamingLevelName, *Found);
			if (!bOnlyCollect)
			{
				TempSaveDatas.Remove(StreamingLevelName);
			}
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
		// Write to temp data.
		const auto Ptr = GetOrAddTempCellSaveData(LIBRARY::GetLevelName(Level));
		LoadTempData(StreamingLevelName, *Ptr);
		const auto LoadedData = *Ptr;
		
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
			if (SaveData->DestroyedActors.Find(Id) >= 0)
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
		// Store runtime actor
		const auto OwnerActor = Itr->GetOwner();
		const auto Cell = InLevel->GetWorldPartitionRuntimeCell();
		if (Cell && Cell->GetCellBounds().IsInsideXY(OwnerActor->GetActorLocation()))
		{
			FStreamingLevelSaveRuntimeData RuntimeData;
			StoreRuntimeActor(Itr->GetOwner(), RuntimeData);
			SaveData->RuntimeActorsSaveDatas.Add(RuntimeData);
			Components.Add(Itr);
		}
	}
	// Remove actors.
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
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::PostLoadMapWithWorld);
	FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::PreLoadMapWithContext);
	
	FWorldDelegates::LevelAddedToWorld.AddUObject(this, &ThisClass::LevelAddedToWorld);
	FWorldDelegates::PreLevelRemovedFromWorld.AddUObject(this, &ThisClass::PreLevelRemovedFromWorld);
}

void UStreamingLevelSaveSubsystem::RemoveDelegates()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	FCoreUObjectDelegates::PreLoadMapWithContext.RemoveAll(this);
	
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	FWorldDelegates::PreLevelRemovedFromWorld.RemoveAll(this);
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

void UStreamingLevelSaveSubsystem::PostLoadMapWithWorld(UWorld* World)
{
	// Clear temp files when load these maps/
	if (GetDefault<UStreamingLevelSaveSettings>()->PostLoadMapClearTemp.Find(World->GetCurrentLevel()->GetName()))
	{
		ClearAllTempFiles();
	}
	
	if (!World->GetWorldPartition() && World->GetNetMode() != NM_Client)
	{
		VisibleStreamingLevels.Add(World->PersistentLevel);
		LoadLevelInternal(World->PersistentLevel);
	}
}

void UStreamingLevelSaveSubsystem::PreLoadMapWithContext(const FWorldContext& WorldContext, const FString& String)
{
	if (!WorldContext.World()->GetWorldPartition() && WorldContext.World()->GetNetMode() != NM_Client)
	{
		// Do not save files because now is loading files and opening new level.
		if (SaveLoadSequence && !SaveLoadSequence->bProgressing)
		{
			VisibleStreamingLevels.Remove(WorldContext.World()->PersistentLevel);
			SaveLevelInternal(WorldContext.World()->PersistentLevel, false, true);
		}
	}
}

void UStreamingLevelSaveSubsystem::LevelAddedToWorld(ULevel* Level, UWorld* World)
{
	if (World && World->GetNetMode() != NM_Client)
	{
		VisibleStreamingLevels.Add(Level);
		LoadLevelInternal(Level);
	}
}

void UStreamingLevelSaveSubsystem::PreLevelRemovedFromWorld(ULevel* Level, UWorld* World)
{
	if (World && World->GetNetMode() != NM_Client)
	{
		// Do not save files because now is loading files and opening new level.
		if (SaveLoadSequence && !SaveLoadSequence->bProgressing)
		{
			VisibleStreamingLevels.Remove(Level);
			SaveLevelInternal(Level, false, true);
		}
	}
}
