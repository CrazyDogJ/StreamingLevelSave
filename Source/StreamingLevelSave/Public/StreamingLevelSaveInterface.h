// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Interface.h"
#include "StreamingLevelSaveInterface.generated.h"

USTRUCT(BlueprintType)
struct STREAMINGLEVELSAVE_API FObjectDefaultSaveData
{
	GENERATED_BODY()

	FObjectDefaultSaveData() {}
	FObjectDefaultSaveData(UObject* ObjectToSave)
	{
		if (ObjectToSave)
		{
			FMemoryWriter MemoryWriter(Data, true);
			FObjectAndNameAsStringProxyArchive Ar(MemoryWriter, false);
			ObjectToSave->Serialize(Ar);
		}
	}

	void LoadSaveData(UObject* ObjectToLoad) const
	{
		if (ObjectToLoad)
		{
			FMemoryReader MemoryReader(Data, true);
			FObjectAndNameAsStringProxyArchive Ar(MemoryReader, true);
			ObjectToLoad->Serialize(Ar);
		}
	}
	
	UPROPERTY(BlueprintReadOnly, SaveGame)
	TArray<uint8> Data;
};

// This class does not need to be modified.
UINTERFACE(BlueprintType)
class UStreamingLevelSaveInterface : public UInterface
{
	GENERATED_BODY()
};

class STREAMINGLEVELSAVE_API IStreamingLevelSaveInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Streaming Level Save")
	FGuid GetIdentityGuid() const;
	virtual FGuid GetIdentityGuid_Implementation() const;
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Streaming Level Save")
	FInstancedStruct GetSaveData();
	virtual FInstancedStruct GetSaveData_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Streaming Level Save")
	void LoadSaveData(const FInstancedStruct& SaveData);
	virtual void LoadSaveData_Implementation(const FInstancedStruct& SaveData);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Streaming Level Save")
	void PostLoadSaveData();
	virtual void PostLoadSaveData_Implementation() {}
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Streaming Level Save")
	ULevel* GetAssociateLevel();
	virtual ULevel* GetAssociateLevel_Implementation();
};
