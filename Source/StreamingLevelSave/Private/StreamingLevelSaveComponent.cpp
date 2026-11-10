// Fill out your copyright notice in the Description page of Project Settings.

#include "StreamingLevelSaveComponent.h"

#include "StreamingLevelSaveLibrary.h"
#include "StreamingLevelSaveSubsystem.h"

#define LIBRARY UStreamingLevelSaveLibrary
#define SUBSYSTEM UStreamingLevelSaveSubsystem

UStreamingLevelSaveComponent::UStreamingLevelSaveComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

SUBSYSTEM* UStreamingLevelSaveComponent::GetSubsystem() const
{
	// 修复：退出世界期间不再假定 GameInstance 存在。
	return UGameInstance::GetSubsystem<SUBSYSTEM>(GetWorld() ? GetWorld()->GetGameInstance() : nullptr);
}

void UStreamingLevelSaveComponent::BeginPlay()
{
	if (LIBRARY::IsRuntimeObject(this) && GetOwner()->HasAuthority())
	{
		if (auto* Sub = GetSubsystem()) Sub->RuntimeActorComponents.FindOrAdd(this);
	}
	
	Super::BeginPlay();
}

void UStreamingLevelSaveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LIBRARY::IsRuntimeObject(this) && GetOwner()->HasAuthority())
	{
		if (auto* Sub = GetSubsystem()) Sub->RuntimeActorComponents.Remove(this);
	}
	
	Super::EndPlay(EndPlayReason);
}
