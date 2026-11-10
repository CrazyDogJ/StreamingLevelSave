#include "StreamingLevelSaveSubsystem.h"

#include "ImageUtils.h"
#include "Engine/GameViewportClient.h"
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
	// 修复：未配置存档序列或退出后，不进入蓝图回调。
	if (IsValid(SaveLoadSequence) && !SaveLoadSequence->bProgressing)
	{
		SaveLoadSequence->SaveFileName = SaveFileName;
		SaveLoadSequence->bSaving = bSaving;
		SaveLoadSequence->bProgressing = true;
		SaveLoadSequence->bMultiplay = bMultiplay;
		SaveLoadSequence->bLan = bLan;
		// 修复：广播前占用序列，监听器重入时不会再次启动同一存档操作。
		if (bSaving)
		{
			OnPreSave.Broadcast();
		}
		else
		{
			OnPreLoad.Broadcast();
		}
		if (!IsValid(SaveLoadSequence) || !SaveLoadSequence->bProgressing) return;
		
		// Call level saving and loading at begin.
		if (bSaving)
		{
			SaveLoadSequence->CopyTempFilesToSavePath();
			SaveLoadSequence->BeginSave();
		}
		else
		{
			// Clear temp file first(Fix bug)
			ClearAllTempFiles();
			
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
	if (SaveLoadSequence && SaveLoadSequence->bProgressing)
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

	return false;
}

bool UStreamingLevelSaveSubsystem::IsSaving() const
{
	if (SaveLoadSequence && SaveLoadSequence->bProgressing && SaveLoadSequence->bSaving)
	{
		return true;
	}
	
	return false;
}

bool UStreamingLevelSaveSubsystem::IsLoading() const
{
	if (SaveLoadSequence && SaveLoadSequence->bProgressing && !SaveLoadSequence->bSaving)
	{
		return true;
	}
	
	return false;
}

TArray<FString> UStreamingLevelSaveSubsystem::CollectTempSaveFiles()
{
	// 修复：收集可触发蓝图并改变可见关卡集合，使用快照遍历。
	FinishPendingWrite();
	const auto Levels = VisibleStreamingLevels.Array();
	for (const auto Level : Levels)
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
	// 修复：缺失或抽象的序列类无法实例化。
	if (Class && !Class->HasAnyClassFlags(CLASS_Abstract))
	{
		SaveLoadSequence = NewObject<UStreamingLevelSaveSequence>(this, Class);
		SaveLoadSequence->SetSubsystem(this);
	}
}

void UStreamingLevelSaveSubsystem::Deinitialize()
{
	// 修复：停止接收世界事件并收尾后台写入，再释放子系统拥有的对象。
	RemoveDelegates();
	UGameViewportClient::OnScreenshotCaptured().RemoveAll(this);
	FinishPendingWrite();

	if (SaveLoadSequence)
	{
		SaveLoadSequence->CleanUp();
		SaveLoadSequence->SetSubsystem(nullptr);
		SaveLoadSequence = nullptr;
	}
	
	VisibleStreamingLevels.Reset();
	RuntimeActorComponents.Reset();
	TempSaveDatas.Reset();
	Super::Deinitialize();
	if (SETTINGS::GetClearTempFilesOnEndGame())
	{
		ClearAllTempFiles();
	}
}

void UStreamingLevelSaveSubsystem::Tick(float DeltaTime)
{
	if (!HasAuthority())
	{
		return;
	}
	
	// Stop runtime actors to get out of bound.
	for (const auto Itr : RuntimeActorComponents)
	{
		if (IsValid(Itr) && IsValid(Itr->GetOwner()) && Itr->GetOwner()->GetRootComponent() && Itr->bTickCheckCell)
		{
			const auto Actor = Itr->GetOwner();
			const auto Velocity = Actor->GetVelocity();
			const auto VelThreshold = Itr->VelocityThreshold;
			if (Velocity.Length() >= VelThreshold)
			{
				for (const auto Level : VisibleStreamingLevels)
				{
					const auto OwnerActor = Itr->GetOwner();
					if (const auto Cell = IsValid(Level) ? Level->GetWorldPartitionRuntimeCell() : nullptr)
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

bool UStreamingLevelSaveSubsystem::HasAuthority() const
{
	if (!GetWorld())
	{
		return false;
	}
	
	if (GetWorld()->GetAuthGameMode())
	{
		return true;
	}
	
	return false;
}

void UStreamingLevelSaveSubsystem::FinishPendingWrite()
{
	if (PendingWrite.IsValid())
	{
		PendingWrite.Wait();
		PendingWrite = UE::Tasks::FTask();
	}
}

void UStreamingLevelSaveSubsystem::SaveTempData(const FString& LevelStreamingName, FStreamingLevelSaveData& SaveData, bool bAsync)
{
	FinishPendingWrite();
	TArray<uint8> Data;
	{
		// 修复：FInstancedStruct 可能包含 UObject，只在游戏线程进行反射序列化。
		FMemoryWriter MemoryWriter(Data, true);
		FObjectAndNameAsStringProxyArchive WriterProxy(MemoryWriter, false);
		FStreamingLevelSaveData::StaticStruct()->SerializeBin(WriterProxy, &SaveData);
		if (WriterProxy.IsError()) return;
	}

	auto Write = [FilePath = LIBRARY::MakeTempFilePath(LevelStreamingName), Bytes = MoveTemp(Data)]()
	{
		if (!FFileHelper::SaveArrayToFile(Bytes, *FilePath))
		{
			UE_LOG(LogTemp, Error, TEXT("StreamingLevelSave: could not write %s"), *FilePath);
		}
	};
	if (bAsync)
	{
		PendingWrite = UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(Write));
	}
	else
	{
		Write();
	}
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
	if (!HasAuthority())
	{
		return;
	}
	
	if (!IsValid(Level)) return;
	const auto StreamingLevelName = LIBRARY::GetLevelName(Level);
	// Dont save level in ignore list.
	if (const auto Settings = GetDefault<UStreamingLevelSaveSettings>())
	{
		if (Settings->IgnoreLevelNames.Find(StreamingLevelName) >= 0)
		{
			return;
		}
	}
	
	if (StreamingLevelName.IsEmpty()) return;

	// 修复：脚本回调可扩容 TempSaveDatas，不能跨回调持有 TMap 元素地址。
	FStreamingLevelSaveData Snapshot = TempSaveDatas.FindRef(StreamingLevelName);
	Snapshot.RuntimeActorsSaveDatas.Reset();
	StorePersistentActors(Level, &Snapshot, bOnlyCollect);
	StoreRuntimeActors(Level, &Snapshot, bOnlyCollect);
	if (const auto* Current = TempSaveDatas.Find(StreamingLevelName))
	{
		for (const FGuid& Id : Current->DestroyedActors) Snapshot.DestroyedActors.AddUnique(Id);
	}
	SaveTempData(StreamingLevelName, Snapshot, bAsync);
	if (bOnlyCollect)
	{
		TempSaveDatas.Add(StreamingLevelName, MoveTemp(Snapshot));
	}
	else
	{
		TempSaveDatas.Remove(StreamingLevelName);
	}
}

void UStreamingLevelSaveSubsystem::LoadLevelInternal(const ULevel* Level)
{
	if (!HasAuthority())
	{
		return;
	}
	
	if (!IsValid(Level)) return;
	const auto StreamingLevelName = LIBRARY::GetLevelName(Level);

	// 修复：关卡快速重新加载时等旧写入落盘，并让恢复回调使用稳定快照。
	if (StreamingLevelName.IsEmpty()) return;
	FinishPendingWrite();
	FStreamingLevelSaveData Snapshot = TempSaveDatas.FindRef(StreamingLevelName);
	LoadTempData(StreamingLevelName, Snapshot);
	auto& Cached = TempSaveDatas.Add(StreamingLevelName, Snapshot);
	Cached.RuntimeActorsSaveDatas.Reset();
	RestorePersistentActors(Level, &Snapshot);
	RestoreRuntimeActors(&Snapshot);
}

void UStreamingLevelSaveSubsystem::StoreObjectUnsafe(UObject* Object, FInstancedStruct& SaveData)
{
	// 修复：带注册组件的运行时 Actor 不一定实现存档接口。
	if (IsValid(Object) && Object->Implements<UStreamingLevelSaveInterface>()) SaveData = INTERFACE::Execute_GetSaveData(Object);
}

void UStreamingLevelSaveSubsystem::RestoreObjectUnsafe(UObject* Object, const FInstancedStruct& SaveData)
{
	if (IsValid(Object) && Object->Implements<UStreamingLevelSaveInterface>()) INTERFACE::Execute_LoadSaveData(Object, SaveData);
}

void UStreamingLevelSaveSubsystem::StoreActorComponents(const AActor* Actor, TMap<FGuid, FInstancedStruct>& Mappings)
{
	if (!IsValid(Actor)) return;
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
	if (!IsValid(Actor)) return;
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

	// 修复：加载、销毁或保存脚本可改变关卡 Actor 数组。
	const auto Actors = Level->Actors;
	for (auto Itr : Actors)
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
	
	// 修复：加载、销毁或保存脚本可改变关卡 Actor 数组。
	const auto Actors = Level->Actors;
	for (auto Itr : Actors)
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
				continue;
			}
			else
			{
				if (const auto FoundData = SaveData->SaveDatas.Find(Id))
				{
					RestoreObjectUnsafe(Itr, *FoundData);
				}
				else
				{
					// If not destroyed we execute post load save data.
					INTERFACE::Execute_PostLoadSaveData(Itr);
				}
				
				if (!IsValid(Itr)) continue;
				Itr->OnDestroyed.AddUniqueDynamic(this, &ThisClass::OnLevelActorDestroyed);
			}
		}
		RestoreActorComponents(Itr, SaveData->SaveDatas);
	}
}

void UStreamingLevelSaveSubsystem::StoreRuntimeActors(const ULevel* InLevel,
	FStreamingLevelSaveData* SaveData, bool bCollectOnly)
{
	TArray<UStreamingLevelSaveComponent*> Components;
	const auto Registered = RuntimeActorComponents.Array();
	for (const auto Itr : Registered)
	{
		// Store runtime actor
		if (!IsValid(Itr) || !IsValid(Itr->GetOwner())) continue;
		const auto OwnerActor = Itr->GetOwner();
		const auto Cell = InLevel->GetWorldPartitionRuntimeCell();
		if (Cell && Cell->GetCellBounds().IsInsideXY(OwnerActor->GetActorLocation()))
		{
			if (Itr->bSave)
			{
				FStreamingLevelSaveRuntimeData RuntimeData;
				StoreRuntimeActor(OwnerActor, RuntimeData);
				SaveData->RuntimeActorsSaveDatas.Add(RuntimeData);
				Components.Add(Itr);
			}
			else
			{
				// Additional Feature : Destroy runtime actor if not save that.
				Components.Add(Itr);
			}
		}
	}
	// Remove actors.
	if (!bCollectOnly)
	{
		for (const auto Itr : Components)
		{
			if (IsValid(Itr) && IsValid(Itr->GetOwner())) Itr->GetOwner()->Destroy(true);
		}
	}
}

void UStreamingLevelSaveSubsystem::StoreRuntimeActor(AActor* Actor,
	FStreamingLevelSaveRuntimeData& RuntimeActorData)
{
	// 修复：调用者可能传入空 Actor，蓝图序列化也可能销毁它。
	if (!IsValid(Actor)) return;
	FInstancedStruct SaveDataStruct;
	StoreObjectUnsafe(Actor, SaveDataStruct);
	if (!IsValid(Actor)) return;
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
	
	// Empty runtime actors when spawned.
	SaveData->RuntimeActorsSaveDatas.Empty();
}

void UStreamingLevelSaveSubsystem::ClearAllTempFiles()
{
	FinishPendingWrite();
	TempSaveDatas.Empty();
	IFileManager::Get().DeleteDirectory(*LIBRARY::GetTempFileFolder(), true, true);
}

void UStreamingLevelSaveSubsystem::AssignDelegates()
{
	// 修复：蓝图可重复调用绑定，确保每个世界事件仅处理一次。
	RemoveDelegates();
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
	// 修复：专服或世界切换时没有本地视口，截图委托为静态共享事件。
	if (!GetGameInstance() || !GetGameInstance()->GetGameViewportClient()) return;
	UGameViewportClient::OnScreenshotCaptured().RemoveAll(this);
	UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &ThisClass::OnScreenshotCaptured);
	FScreenshotRequest::RequestScreenshot(false);
}

UTexture2D* UStreamingLevelSaveSubsystem::LoadScreenshot(const FSaveGameScreenshotData PngData)
{
	if (PngData.Data.Num() > 0)
		return FImageUtils::ImportBufferAsTexture2D(PngData.Data);

	return nullptr;
}

void UStreamingLevelSaveSubsystem::AddDestroyedLevelActor(const FStreamingLevelActorData InData)
{
	if (InData.IsValid())
	{
		if (const auto Found = GetOrAddTempCellSaveData(InData.LevelName))
		{
			Found->DestroyedActors.AddUnique(InData.ActorGuid);
		}
	}
}

void UStreamingLevelSaveSubsystem::OnLevelActorDestroyed(AActor* DestroyedActor)
{
	if (!HasAuthority())
	{
		return;
	}
	
	if (const ULevel* Level = INTERFACE::Execute_GetAssociateLevel(DestroyedActor))
	{
		if (const auto Found = GetOrAddTempCellSaveData(LIBRARY::GetLevelName(Level)))
		{
			Found->DestroyedActors.AddUnique(FGuid::NewDeterministicGuid(DestroyedActor->GetPathName()));
		}
	}
}

void UStreamingLevelSaveSubsystem::OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	UGameViewportClient::OnScreenshotCaptured().RemoveAll(this);
	
	// Convert down to PNG
	TArray<uint8> PngData;
	FImageUtils::ThumbnailCompressImageArray(Width, Height, Colors, PngData);
	
	ScreenShotCaptured.Broadcast(FSaveGameScreenshotData(PngData));
}

void UStreamingLevelSaveSubsystem::PostLoadMapWithWorld(UWorld* World)
{
	// 修复：全局世界事件也会收到加载失败或其他 PIE 实例的通知。
	if (!World || World->GetGameInstance() != GetGameInstance() || !World->GetCurrentLevel()) return;
	// Clear temp files when load these maps/
	if (GetDefault<UStreamingLevelSaveSettings>()->PostLoadMapClearTemp.Contains(World->GetCurrentLevel()->GetName()))
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
	if (!WorldContext.World() || WorldContext.World()->GetGameInstance() != GetGameInstance()) return;
	if (!WorldContext.World()->GetWorldPartition() && WorldContext.World()->GetNetMode() != NM_Client)
	{
		VisibleStreamingLevels.Remove(WorldContext.World()->PersistentLevel);
		// Do not save files because now is loading files and opening new level.
		if (SaveLoadSequence && !SaveLoadSequence->bProgressing)
		{
			SaveLevelInternal(WorldContext.World()->PersistentLevel, false, true);
		}
	}
}

void UStreamingLevelSaveSubsystem::LevelAddedToWorld(ULevel* Level, UWorld* World)
{
	if (World && World->GetGameInstance() == GetGameInstance() && Level && World->GetNetMode() != NM_Client)
	{
		VisibleStreamingLevels.Add(Level);
		LoadLevelInternal(Level);
	}
}

void UStreamingLevelSaveSubsystem::PreLevelRemovedFromWorld(ULevel* Level, UWorld* World)
{
	if (World && World->GetGameInstance() == GetGameInstance() && Level && World->GetNetMode() != NM_Client)
	{
		// 修复：存档流程暂停写盘期间，仍须释放卸载关卡的强引用。
		VisibleStreamingLevels.Remove(Level);
		// Do not save files because now is loading files and opening new level.
		if (SaveLoadSequence && !SaveLoadSequence->bProgressing)
		{
			SaveLevelInternal(Level, false, true);
		}
	}
}
