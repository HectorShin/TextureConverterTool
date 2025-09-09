// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/Texture2D.h"
#include "PBRToORM.generated.h"

/**
 * 
 */
UCLASS()
class ORMCONVERTERTOOL_API UPBRToORM : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Convert PBR to ORM"))
	static UTexture2D* ConvertPBRToORM(FString TexturePackagePath);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Replace PBR With ORM In MaterialInstance"))
	static void ReplacePBRWithORMInMaterialInstance(UMaterialInstanceConstant* MaterialInstance, UTexture2D* NewORMTexture);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Asset Path From Folder"))
	static UMaterialInstanceConstant* GetAssetPathFromFolder(FString FolderPath);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Material Instances FromTexture"))
	static  TArray<UMaterialInstanceConstant*> GetMaterialInstancesFromTexture(FString TexturePath);

};
