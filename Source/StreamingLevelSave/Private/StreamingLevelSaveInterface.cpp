// Fill out your copyright notice in the Description page of Project Settings.


#include "StreamingLevelSaveInterface.h"

#include "StreamingLevelSaveLibrary.h"

FGuid IStreamingLevelSaveInterface::GetIdentityGuid_Implementation() const
{
	const auto Object = _getUObject();
	return UStreamingLevelSaveLibrary::GetIdentityGuidInternal(Object);
}

FInstancedStruct IStreamingLevelSaveInterface::GetSaveData_Implementation()
{
	const auto Object = _getUObject();
	return UStreamingLevelSaveLibrary::GetSaveDataInternal(Object);
}

void IStreamingLevelSaveInterface::LoadSaveData_Implementation(const FInstancedStruct& SaveData)
{
	const auto Object = _getUObject();
	UStreamingLevelSaveLibrary::LoadSaveDataInternal(Object, SaveData);
}

ULevel* IStreamingLevelSaveInterface::GetAssociateLevel_Implementation()
{
	const auto Object = _getUObject();
	return UStreamingLevelSaveLibrary::GetAssociateLevelInternal(Object);
}
