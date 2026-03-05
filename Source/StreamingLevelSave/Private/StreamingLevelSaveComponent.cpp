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
	return GetWorld()->GetGameInstance()->GetSubsystem<SUBSYSTEM>();
}

void UStreamingLevelSaveComponent::BeginPlay()
{
	if (LIBRARY::IsRuntimeObject(this) && GetOwner()->HasAuthority())
	{
		GetSubsystem()->RuntimeActorComponents.FindOrAdd(this);
	}
	
	Super::BeginPlay();
}

void UStreamingLevelSaveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LIBRARY::IsRuntimeObject(this) && GetOwner()->HasAuthority())
	{
		GetSubsystem()->RuntimeActorComponents.Remove(this);
	}
	
	Super::EndPlay(EndPlayReason);
}
