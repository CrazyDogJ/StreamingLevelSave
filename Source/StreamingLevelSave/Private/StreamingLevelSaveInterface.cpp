// Fill out your copyright notice in the Description page of Project Settings.


#include "StreamingLevelSaveInterface.h"

#include "StreamingLevelSaveLibrary.h"

FGuid IStreamingLevelSaveInterface::GetIdentityGuid_Implementation() const
{
	return FGuid();
}

FInstancedStruct IStreamingLevelSaveInterface::GetSaveData_Implementation()
{
	return FInstancedStruct();
}

void IStreamingLevelSaveInterface::LoadSaveData_Implementation(const FInstancedStruct& SaveData)
{
}

ULevel* IStreamingLevelSaveInterface::GetAssociateLevel_Implementation()
{
	return nullptr;
}
