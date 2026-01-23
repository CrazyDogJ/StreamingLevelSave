#include "StreamingLevelSaveLibrary.h"

#include "StreamingLevelSaveInterface.h"
#include "StreamingLevelSaveSettings.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

FString UStreamingLevelSaveLibrary::GetTempFileFolder()
{
	return FPaths::ProjectSavedDir() + "SaveGames/" + UStreamingLevelSaveSettings::GetTempFileFolder();
}

FString UStreamingLevelSaveLibrary::GetTempFileDir()
{
	return GetTempFileFolder() + "/";
}

FString UStreamingLevelSaveLibrary::MakeTempFilePath(const FString& TempFileName)
{
	return GetTempFileDir() + TempFileName + ".sav";
}

FString UStreamingLevelSaveLibrary::MakeSaveGameFolder(FString SaveGameName)
{
	return FPaths::ProjectSavedDir() + "SaveGames/" + SaveGameName;
}

FString UStreamingLevelSaveLibrary::MakeSaveGameDir(FString SaveGameName)
{
	return MakeSaveGameFolder(SaveGameName) + "/";
}

FString UStreamingLevelSaveLibrary::GetLevelName(const FString& PackageName)
{
	// Detect what level an object originated from
	// GetLevel()->GetName / GetFName() returns "PersistentLevel" all the time
	// GetLevel()->GetPathName returns e.g. /Game/Maps/[UEDPIE_0_]TestAdventureMap.TestAdventureMap:PersistentLevel
	// Outer is "PersistentLevel"
	// Outermost is "/Game/Maps/[UEDPIE_0_]TestAdventureStream0" so that's what we want
	// Note that using Actor->GetOutermost() with WorldPartition will return some wrapper object.
	FString LevelName;
	PackageName.Split("/", nullptr, &LevelName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	// Strip off PIE prefix, "UEDPIE_N_" where N is a number
	if (LevelName.StartsWith("UEDPIE_"))
		LevelName = LevelName.Right(LevelName.Len() - 9);
	return LevelName;
}

FString UStreamingLevelSaveLibrary::GetLevelName(const ULevel* Level)
{
	if (!Level) return FString();
	
	if (const auto OuterMost = Level->GetOutermost())
	{
		return GetLevelName(OuterMost->GetName());
	}

	return FString();
}

bool UStreamingLevelSaveLibrary::IsRuntimeObject(const UObject* InObject)
{
	// If comp using actor's result.
	if (const auto Comp = Cast<UActorComponent>(InObject))
	{
		return !Comp->GetOwner()->HasAnyFlags(RF_WasLoaded);
	}
	
	return !InObject->HasAnyFlags(RF_WasLoaded);
}

bool UStreamingLevelSaveLibrary::IsSaveInterfaceObject(const UObject* Object, FGuid& OutId)
{
	if (!Object) return false;

	if (Object->Implements<UStreamingLevelSaveInterface>())
	{
		OutId = IStreamingLevelSaveInterface::Execute_GetIdentityGuid(Object);
		return OutId.IsValid();
	}
	
	return false;
}

FObjectDefaultSaveData UStreamingLevelSaveLibrary::GetObjectDefaultSaveData(UObject* Object)
{
	return FObjectDefaultSaveData(Object);
}

ULevel* UStreamingLevelSaveLibrary::GetAssociateLevelDefault(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	
	// Persistent actor
	if (Actor->HasAnyFlags(RF_WasLoaded))
	{
		return Actor->GetLevel();
	}
	
	// Runtime actor
	const UWorldPartitionRuntimeCell* Cell = nullptr;
	GetOverlappedWorldPartitionRuntimeCell2D(Actor->GetWorld(), Actor->GetActorLocation(), Cell);
	if (Cell)
	{
		return Cell->GetLevel();
	}

	// No WP, use current persistent level.
	return Actor->GetWorld()->GetCurrentLevel();
}

void UStreamingLevelSaveLibrary::GetOverlappedWorldPartitionRuntimeCell2D(
	const UWorld* World, const FVector& Location, const UWorldPartitionRuntimeCell*& OutCell)
{
	OutCell = nullptr;
	
	const auto WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
	if (!WorldPartitionSubsystem)
	{
		return;
	}

	auto SmallestCellVolume = 0.f;

	const auto ForEachCellFunction = [&OutCell, &Location, &SmallestCellVolume](const UWorldPartitionRuntimeCell* Cell) -> bool
	{
		// for simplicity, assuming actor bounds are small enough that only a single cell needs to be considered
		if (const auto CellBounds = Cell->GetCellBounds(); CellBounds.IsInsideXY(Location))
		{
			// use the smallest cell
			if (const auto Volume = CellBounds.GetVolume(); !OutCell || Volume < SmallestCellVolume)
			{
				SmallestCellVolume = Volume;
				OutCell = Cell;
			}
		}
		return true;
	};

	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	auto ForEachWpFunction = [ForEachCellFunction](UWorldPartition* WorldPartition) -> bool
	{
		if (WorldPartition)
		{
			WorldPartition->RuntimeHash->ForEachStreamingCells(ForEachCellFunction);
		}

		return true;
	};

	WorldPartitionSubsystem->ForEachWorldPartition(ForEachWpFunction);
}

void UStreamingLevelSaveLibrary::InitGuidFromString(FString String, FGuid& Guid)
{
	Guid = FGuid::NewDeterministicGuid(String);
}

FGuid UStreamingLevelSaveLibrary::GetIdentityGuidInternal(const UObject* Object)
{
	if (!Object) return FGuid();
	
	FGuid Guid;
	if (IsRuntimeObject(Object))
	{
		// If runtime component using name.
		if (const auto Comp = Cast<UActorComponent>(Object))
		{
			InitGuidFromString(Comp->GetName(), Guid);
			return Guid;
		}
	}
	else
	{
		// Persistent actor/component using path name as guid.
		InitGuidFromString(Object->GetPathName(), Guid);
		return Guid;
	}

	return Guid;
}

FInstancedStruct UStreamingLevelSaveLibrary::GetSaveDataInternal(UObject* Object)
{
	const auto SaveData = GetObjectDefaultSaveData(Object);
	return FInstancedStruct::Make(SaveData);
}

void UStreamingLevelSaveLibrary::LoadSaveDataInternal(UObject* Object, const FInstancedStruct& SaveData)
{
	if (const auto DefaultSaveData = SaveData.GetPtr<FObjectDefaultSaveData>())
	{
		DefaultSaveData->LoadSaveData(Object);
	}

	IStreamingLevelSaveInterface::Execute_PostLoadSaveData(Object);
}

ULevel* UStreamingLevelSaveLibrary::GetAssociateLevelInternal(UObject* Object)
{
	if (const auto Actor = Cast<AActor>(Object))
	{
		return GetAssociateLevelDefault(Actor);
	}
	
	if (const auto Comp = Cast<UActorComponent>(Object))
	{
		return GetAssociateLevelDefault(Comp->GetOwner());
	}

	return nullptr;
}
