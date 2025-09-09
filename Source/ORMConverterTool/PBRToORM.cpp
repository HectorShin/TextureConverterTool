// Fill out your copyright notice in the Description page of Project Settings.


#include "PBRToORM.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/StaticMeshActor.h"
//#include "Engine/SkeletalMeshActor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Editor/UnrealEd/Public/ObjectTools.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/Texture.h"
#include "TextureResource.h"
#include "TextureCompiler.h"
#endif


// Helper para pegar raw data da textura
static FString GetRelativePath(FString File) {

    FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());

    if (!File.StartsWith(ContentDir))
    {
        UE_LOG(LogTemp, Error, TEXT("Path is not inside /Content folder."));
        return FString();
    }

    FString RelativePath = File;
    RelativePath.RemoveFromStart(ContentDir);

    RelativePath = FPaths::ChangeExtension(RelativePath, TEXT("")); // remove .uasset
    FString AssetName = FPaths::GetBaseFilename(RelativePath);

    FString ObjectPath = FString::Printf(TEXT("/Game/%s.%s"), *RelativePath.Replace(TEXT("\\"), TEXT("/")), *AssetName);

    return ObjectPath;
}

UMaterialInstanceConstant* UPBRToORM::GetAssetPathFromFolder(FString FolderPath)
{
    // Caminho completo da pasta "Content"
    FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());

    // Validação: a pasta precisa estar dentro de /Content
    if (!FolderPath.StartsWith(ContentDir))
    {
        UE_LOG(LogTemp, Error, TEXT("Path is not inside /Content folder."));
        return nullptr;
    }

    // Remove o prefixo absoluto e obtém caminho relativo a /Game
    FString RelativePath = FolderPath;
    RelativePath.RemoveFromStart(ContentDir);
    RelativePath = RelativePath.Replace(TEXT("\\"), TEXT("/"));

    // Remove barra final, se tiver
    RelativePath.RemoveFromEnd(TEXT("/"));

    // Pega o nome da última pasta
    FString FolderName = FPaths::GetCleanFilename(RelativePath);

    // Monta o caminho no formato /Game/Path/To/Folder/FolderName.FolderName
    FString ObjectPath = FString::Printf(TEXT("/Game/%s/%s.%s"), *RelativePath, *FolderName, *FolderName);

    UMaterialInstanceConstant* MaterialInstance = Cast<UMaterialInstanceConstant>(StaticLoadObject(UMaterialInstanceConstant::StaticClass(), nullptr, *ObjectPath));

    return MaterialInstance;
}

void SetUseORM(UMaterialInstanceConstant* MaterialInstance, bool bUseORM)
{
    if (!MaterialInstance) return;

    // Nome do parâmetro estático bool
    static const FName ParamName(TEXT("UseORM"));

    // Atualiza o valor do parâmetro
    FStaticParameterSet StaticParams;
    MaterialInstance->GetStaticParameterValues(StaticParams);

    for (FStaticSwitchParameter& Switch : StaticParams.StaticSwitchParameters)
    {
        if (Switch.ParameterInfo.Name == ParamName)
        {
            Switch.Value = bUseORM;
            Switch.bOverride = true;
            MaterialInstance->UpdateStaticPermutation(StaticParams);
            MaterialInstance->PostEditChange(); // Atualiza no editor
            MaterialInstance->MarkPackageDirty();
            return;
        }
    }

    // Se o parâmetro não existir ainda
    FStaticSwitchParameter NewSwitch;
    NewSwitch.ParameterInfo = FMaterialParameterInfo(ParamName);
    NewSwitch.Value = bUseORM;
    NewSwitch.bOverride = true;

    StaticParams.StaticSwitchParameters.Add(NewSwitch);
    MaterialInstance->UpdateStaticPermutation(StaticParams);
    MaterialInstance->PostEditChange();
    MaterialInstance->MarkPackageDirty();
}


UTexture2D* UPBRToORM::ConvertPBRToORM(FString TexturePackagePath)
{
#if WITH_EDITOR
    FString AOPackagePath = TexturePackagePath + TEXT("/AO.AO");
    FString RoughnessPackagePath = TexturePackagePath + TEXT("/Roughness.Roughness");
    FString MetallicPackagePath = TexturePackagePath + TEXT("/Metalic.Metalic");

    UTexture2D* AO = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *AOPackagePath));
    UTexture2D* Roughness = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *RoughnessPackagePath));
    UTexture2D* Metallic = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *MetallicPackagePath));

    if (!AO || !Roughness || !Metallic) return nullptr;

    // Make sure all async texture compilation is finished before we touch Source
    FTextureCompilingManager::Get().FinishAllCompilation();

    const int32 Width = AO->GetSizeX();
    const int32 Height = AO->GetSizeY();
    if (Roughness->GetSizeX() != Width || Metallic->GetSizeX() != Width ||
        Roughness->GetSizeY() != Height || Metallic->GetSizeY() != Height)
    {
        return nullptr;
    }

    const int32 TotalPixels = Width * Height;
    const uint8* RoughData = Roughness->Source.LockMip(0);
    const uint8* MetalData = Metallic->Source.LockMip(0);
    const uint8* AOData = AO->Source.LockMip(0);

    if (!RoughData || !MetalData || !AOData)
    {
        Roughness->Source.UnlockMip(0);
        Metallic->Source.UnlockMip(0);
        AO->Source.UnlockMip(0);
        return nullptr;
    }

    ETextureSourceFormat roughFmt = Roughness->Source.GetFormat();
    ETextureSourceFormat metalFmt = Metallic->Source.GetFormat();
    ETextureSourceFormat aoFmt = AO->Source.GetFormat();

    TArray<uint8> CombinedPixels;
    CombinedPixels.AddUninitialized(TotalPixels * 4);

    auto GetGray = [&](const uint8* Data, ETextureSourceFormat Format, int32 index)->uint8
        {
            switch (Format)
            {
            case TSF_G8: return Data[index];
            case TSF_G16: return Data[index * 2];
            case TSF_BGRA8:
            case TSF_BGRE8: return Data[index * 4 + 2];
            default: return Data[index];
            }
        };

    for (int32 i = 0; i < TotalPixels; ++i)
    {
        uint8 roughVal = GetGray(RoughData, roughFmt, i);
        uint8 metalVal = GetGray(MetalData, metalFmt, i);
        uint8 aoVal = GetGray(AOData, aoFmt, i);

        CombinedPixels[i * 4 + 2] = roughVal;
        CombinedPixels[i * 4 + 1] = metalVal;
        CombinedPixels[i * 4 + 0] = aoVal;
        CombinedPixels[i * 4 + 3] = 0xFF;
    }

    Roughness->Source.UnlockMip(0);
    Metallic->Source.UnlockMip(0);
    AO->Source.UnlockMip(0);

    FString FolderPath = FPackageName::GetLongPackagePath(AOPackagePath);
    FString FolderName = FPaths::GetCleanFilename(FolderPath);
    FString NewTexName = FolderName + TEXT("_ORM");
    FString NewTexPackagePath = FolderPath / NewTexName;

    UPackage* NewTexPackage = CreatePackage(*NewTexPackagePath);
    NewTexPackage->FullyLoad();

    UTexture2D* NewORMTexture = NewObject<UTexture2D>(NewTexPackage, *NewTexName, RF_Public | RF_Standalone);
    NewORMTexture->PreEditChange(nullptr);

    NewORMTexture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, CombinedPixels.GetData());
    NewORMTexture->SRGB = false;
    NewORMTexture->CompressionSettings = TC_Masks;
    NewORMTexture->MipGenSettings = TMGS_NoMipmaps;

    NewORMTexture->PostEditChange();
    NewORMTexture->MarkPackageDirty();

    FAssetRegistryModule::AssetCreated(NewORMTexture);

    FString FilePath = FPackageName::LongPackageNameToFilename(NewTexPackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GError;
    SaveArgs.bWarnOfLongFilename = true;
    SaveArgs.SaveFlags = SAVE_NoError;
    UPackage::SavePackage(NewTexPackage, NewORMTexture, *FilePath, SaveArgs);

    return NewORMTexture;
#else
    return nullptr;
#endif
}

void UPBRToORM::ReplacePBRWithORMInMaterialInstance(UMaterialInstanceConstant * MaterialInstance, UTexture2D* NewORMTexture)
{
#if WITH_EDITOR
    if (!MaterialInstance || !NewORMTexture)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid input to ReplacePBRWithORMInMaterialInstance."));
        return;
    }

    // Define os nomes dos parâmetros PBR
    static const FName RoughnessParamName("Roughness");
    static const FName MetallicParamName("Metalic");
    static const FName AOParamName("AO");

    //// Limpa apenas os parâmetros PBR se estiverem definidos
    //MaterialInstance->ClearParameterValueEditorOnly(FMaterialParameterInfo("Roughness"));
    //MaterialInstance->ClearParameterValueEditorOnly(FMaterialParameterInfo("Metallic"));
    //MaterialInstance->ClearParameterValueEditorOnly(FMaterialParameterInfo("AO"));

    TArray<FName> PBRParams = { FName("Metalic"), FName("Roughness"), FName("AO") };

    MaterialInstance->TextureParameterValues.RemoveAll([&](const FTextureParameterValue& Param) {
        return PBRParams.Contains(Param.ParameterInfo.Name);
        });

    SetUseORM(MaterialInstance, true);

    // Define o novo parâmetro ORM
    static const FName ORMParamName("ORM");
    MaterialInstance->SetTextureParameterValueEditorOnly(ORMParamName, NewORMTexture);

    // Atualiza o material na interface
    MaterialInstance->PostEditChange();
    MaterialInstance->MarkPackageDirty();
#endif
}

TArray<UMaterialInstanceConstant*> UPBRToORM::GetMaterialInstancesFromTexture(FString TexturePath)
{
    TArray<UMaterialInstanceConstant*> Result;
#if WITH_EDITOR
    FString AOPackagePath = TexturePath + TEXT("/AO");
    FName TexturePathName(*AOPackagePath);
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    TArray<FName> ReferencerNames;

	AssetRegistry.GetReferencers(TexturePathName, ReferencerNames, UE::AssetRegistry::EDependencyCategory::All);

    for (FName& RefName : ReferencerNames)
    {
        FString RefString = RefName.ToString();

        // Garantir que é PackageName.AssetName
        if (!RefString.Contains(TEXT(".")))
        {
            FString AssetName = FPaths::GetCleanFilename(RefString);
            RefString += TEXT(".") + AssetName;
        }
        FName CoorectedRefName(*RefString);

        FAssetData RefData = AssetRegistryModule.Get().GetAssetByObjectPath(CoorectedRefName);
        UE_LOG(LogTemp, Error, TEXT("AssetClass: %s"), *RefData.AssetClass.ToString());
        
        UMaterialInstanceConstant* MaterialInstance = Cast<UMaterialInstanceConstant>(StaticLoadObject(UMaterialInstanceConstant::StaticClass(), nullptr, *RefString));
        if (MaterialInstance)
        {
              Result.Add(MaterialInstance);
        }

    }

        
    return Result;
#endif
    return Result;
}



