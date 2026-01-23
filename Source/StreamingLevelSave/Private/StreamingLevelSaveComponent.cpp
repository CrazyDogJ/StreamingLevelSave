// Fill out your copyright notice in the Description page of Project Settings.

#include "StreamingLevelSaveComponent.h"

#include "StreamingLevelSaveLibrary.h"
#include "StreamingLevelSaveSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"

#define LIBRARY UStreamingLevelSaveLibrary
#define SUBSYSTEM UStreamingLevelSaveSubsystem

UStreamingLevelSaveComponent::UStreamingLevelSaveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

SUBSYSTEM* UStreamingLevelSaveComponent::GetSubsystem() const
{
	return GetWorld()->GetGameInstance()->GetSubsystem<SUBSYSTEM>();
}

void UStreamingLevelSaveComponent::BeginPlay()
{
	if (LIBRARY::IsRuntimeObject(this))
	{
		GetSubsystem()->RuntimeActorComponents.Add(this);
	}
	
	Super::BeginPlay();
}

void UStreamingLevelSaveComponent::TickComponent(float DeltaTime, enum ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	FGuid Id;
	if (LIBRARY::IsSaveInterfaceObject(GetOwner(), Id) &&
		bTickCheckCell && GetOwner()->GetVelocity().Length() > VelocityThreshold)
	{
		const UWorldPartitionRuntimeCell* Cell;
		LIBRARY::GetOverlappedWorldPartitionRuntimeCell2D(GetWorld(), GetOwner()->GetActorLocation(), Cell);
		if (Cell->GetCurrentState() != EWorldPartitionRuntimeCellState::Activated)
		{
			if (const auto Found = GetSubsystem()->GetOrAddTempCellSaveData(LIBRARY::GetLevelName(Cell->GetLevel())))
			{
				FStreamingLevelSaveRuntimeData RuntimeData;
				SUBSYSTEM::StoreRuntimeActor(GetOwner(), RuntimeData);
				Found->RuntimeActorsSaveDatas.Add(RuntimeData);
			}
		}
	}
}

void UStreamingLevelSaveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LIBRARY::IsRuntimeObject(this))
	{
		GetSubsystem()->RuntimeActorComponents.Remove(this);
	}
	
	Super::EndPlay(EndPlayReason);
}
