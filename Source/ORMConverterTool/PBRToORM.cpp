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


static bool GetRawTextureData(UTexture2D* Texture, TArray<FColor>& OutPixels)
{
    if (!Texture) return false;

    const int32 Width = Texture->GetSizeX();
    const int32 Height = Texture->GetSizeY();

    TArray64<uint8> RawData;
    Texture->Source.GetMipData(RawData, 0);

    OutPixels.SetNumUninitialized(Width * Height);
    FMemory::Memcpy(OutPixels.GetData(), RawData.GetData(), RawData.Num());

    return true;
}

UTexture2D* UPBRToORM::ConvertPBRToORM(FString AOFilePath, FString RoughnessFilePath, FString MetallicFilePath, const FString& SavePath)
{
#if WITH_EDITOR
    FString AOFileRelativePath = GetRelativePath(AOFilePath);
    FString RoughnessFileRelativePath = GetRelativePath(RoughnessFilePath);
    FString MetalicFileRelativePath = GetRelativePath(MetallicFilePath);
    UTexture2D* AO = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *AOFileRelativePath));
    UTexture2D* Roughness = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *RoughnessFileRelativePath));
    UTexture2D* Metallic = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *MetalicFileRelativePath));

    AO->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
    AO->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
    AO->SRGB = false;
    AO->UpdateResource();

    Roughness->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
    Roughness->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
    Roughness->SRGB = false;
    Roughness->UpdateResource();

    Metallic->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
    Metallic->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
    Metallic->SRGB = false;
    Metallic->UpdateResource();

    if (!AO || !Roughness || !Metallic) return nullptr;

    const int32 Width = AO->GetPlatformData()->SizeX;
    const int32 Height = AO->GetPlatformData()->SizeX;

    UE_LOG(LogTemp, Error, TEXT("Surface Size - Width: %d, Height: %d"), Width, Height);

    if (Roughness->GetSizeX() != Width || Metallic->GetSizeX() != Width)
    {
        UE_LOG(LogTemp, Error, TEXT("All textures must be same size."));
        return nullptr;
    }
    const int32 TotalPixels = Width * Height;
    const uint8* RoughData = Roughness->Source.LockMip(0);
    const uint8* MetalData = Metallic->Source.LockMip(0);
    const uint8* AOData = AO->Source.LockMip(0);
    if (!RoughData || !MetalData || !AOData)
    {
        UE_LOG(LogTemp, Error,
            TEXT("Failed to access source data of one or more textures."));
        Roughness->Source.UnlockMip(0);
        Metallic->Source.UnlockMip(0);
        AO->Source.UnlockMip(0);
        return nullptr;
    }
    // Determine source pixel format for each (to handle bytes per pixel)
    ETextureSourceFormat roughFmt = Roughness->Source.GetFormat();
    ETextureSourceFormat metalFmt = Metallic->Source.GetFormat();
    ETextureSourceFormat aoFmt = AO->Source.GetFormat();
    // Prepare buffer for combined texture pixels (4 bytes per pixel, BGRA8 format)
    TArray<uint8> CombinedPixels;
    CombinedPixels.AddUninitialized(TotalPixels * 4);
    // Lambda to fetch a single grayscale value from source data (supports G8 or BGRA8 formats)
    auto GetGray = [&](const uint8* Data, ETextureSourceFormat Format, int32 index)->uint8
        {
            switch (Format)
            {
            case TSF_G8: // 8-bit grayscale
                return Data[index];
            case TSF_G16: // 16-bit grayscale (take high byte as approximation)
                return Data[index * 2];
            case TSF_BGRA8: // 8-bit BGRA (use R channel)
            case TSF_BGRE8:
                return Data[index * 4 + 2]; // R channel of BGRA
            default:
                // For other formats, just return first byte (might not be valid in all cases)
                return Data[index];
            }
        };
    // Loop through each pixel and assign combined channels: R=roughness, G = metallic, B = AO, A = 255
    for (int32 i = 0; i < TotalPixels; ++i)
    {
        uint8 roughVal = GetGray(RoughData, roughFmt, i);
        uint8 metalVal = GetGray(MetalData, metalFmt, i);
        uint8 aoVal = GetGray(AOData, aoFmt, i);
        CombinedPixels[i * 4 + 2] = roughVal; // Red channel
        CombinedPixels[i * 4 + 1] = metalVal; // Green channel
        CombinedPixels[i * 4 + 0] = aoVal; // Blue channel
        CombinedPixels[i * 4 + 3] = 0xFF; // Alpha channel (opaque)
    }
    UE_LOG(LogTemp, Error, TEXT("Pixeling"));

    // Unlock source data arrays
    Roughness->Source.UnlockMip(0);
    Metallic->Source.UnlockMip(0);
    AO->Source.UnlockMip(0);

    UE_LOG(LogTemp, Error, TEXT("Creating the ORM texture"));
    FString FolderPath = FPackageName::GetLongPackagePath(AOFileRelativePath);
    FString FolderName = FPaths::GetCleanFilename(FolderPath);

    FString NewTexName = FolderName + TEXT("_ORM");
    FString NewTexPackagePath = FolderPath / NewTexName;
    UPackage* NewTexPackage = CreatePackage(*NewTexPackagePath);
    NewTexPackage->FullyLoad();
    UTexture2D* NewORMTexture = NewObject<UTexture2D>(NewTexPackage, *NewTexName, RF_Public | RF_Standalone | RF_MarkAsRootSet);

    // Initialize texture properties and allocate MIP data
    NewORMTexture->AddToRoot(); // prevent garbage collection



    FTexturePlatformData* TexturePlatformData = new FTexturePlatformData();
    TexturePlatformData->SizeX = Width;
    TexturePlatformData->SizeY = Height;
    //TexturePlatformData->NumSlices = 1;
    TexturePlatformData->PixelFormat = PF_B8G8R8A8;

    // Define os dados de plataforma da textura




    //NewORMTexture->PlatformData = new FTexturePlatformData();
    //NewORMTexture->PlatformData->SizeX = Width;
    //NewORMTexture->PlatformData->SizeY = Height;
    //NewORMTexture->PlatformData->NumSlices = 1;
    //NewORMTexture->PlatformData->PixelFormat = PF_B8G8R8A8;


    // Allocate first mipmap
    FTexture2DMipMap* Mip = new FTexture2DMipMap();
    Mip->SizeX = Width;
    Mip->SizeY = Height;
    Mip->BulkData.Lock(LOCK_READ_WRITE);
    void* NewTexData = Mip->BulkData.Realloc(TotalPixels * 4);
    FMemory::Memcpy(NewTexData, CombinedPixels.GetData(), TotalPixels * 4);
    Mip->BulkData.Unlock();
    TexturePlatformData->Mips.Add(Mip);

    NewORMTexture->SetPlatformData(TexturePlatformData);

    UE_LOG(LogTemp, Error, TEXT("Successfully set platformdata"));

    NewORMTexture->SRGB = false;
    NewORMTexture->CompressionSettings = TC_Masks;
    NewORMTexture->MipGenSettings = TMGS_NoMipmaps;
    // Initialize source art data for the texture asset (store uncompressed source pixels)
    NewORMTexture->Source.Init(Width, Height, /*NumSlices=*/1, /*NumMips=*/1,
        TSF_BGRA8, CombinedPixels.GetData());
    NewORMTexture->UpdateResource();

    UE_LOG(LogTemp, Error, TEXT("Created textures"));
    // Save the new texture asset to Content Browser
    FAssetRegistryModule::AssetCreated(NewORMTexture);
    NewTexPackage->MarkPackageDirty();
    FString FilePath =
        FPackageName::LongPackageNameToFilename(NewTexPackagePath,
            FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GError;
    SaveArgs.bWarnOfLongFilename = true;
    SaveArgs.SaveFlags = SAVE_NoError;
    UPackage::SavePackage(NewTexPackage, NewORMTexture, *FilePath, SaveArgs); // save .uasset to disk
    UE_LOG(LogTemp, Error, TEXT("Saved  the texture"));

    //auto GetPixels = [](UTexture2D* Tex) -> TArray<FColor>
    //    {
    //        TArray<FColor> Pixels;

    //        if (!Tex || !Tex->GetPlatformData() || Tex->GetPlatformData()->Mips.Num() == 0)
    //            return Pixels;

    //        FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
    //        void* Data = Mip.BulkData.Lock(LOCK_READ_ONLY);
    //        if (Data)
    //        {
    //            FColor* FormattedData = static_cast<FColor*>(Data);
    //            Pixels.Append(FormattedData, Tex->GetSizeX() * Tex->GetSizeY());
    //        }
    //        Mip.BulkData.Unlock();
    //        return Pixels;
    //    };

    //TArray<FColor> AOPixels = GetPixels(AO);
    //TArray<FColor> RoughnessPixels = GetPixels(Roughness);
    //TArray<FColor> MetallicPixels = GetPixels(Metallic);

    //UE_LOG(LogTemp, Error, TEXT("Successfully extracted pixel data from input textures."));

    //if (AOPixels.Num() == 0 || RoughnessPixels.Num() == 0 || MetallicPixels.Num() == 0)
    //{
    //    UE_LOG(LogTemp, Error, TEXT("Failed to extract pixel data from input textures."));
    //    return false;
    //}

    //UE_LOG(LogTemp, Error, TEXT("Prepring ORM structure"));

    //TArray<FColor> ORMPixels;
    //ORMPixels.SetNum(Width * Height);

    //for (int32 i = 0; i < ORMPixels.Num(); ++i)
    //{
    //    ORMPixels[i] = FColor(AOPixels[i].R, RoughnessPixels[i].R, MetallicPixels[i].R, 255);
    //}

    //UE_LOG(LogTemp, Error, TEXT("Successfully created ORM texture"));

    //// Convert to PNG
    //IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    //TSharedPtr<IImageWrapper> PNGWriter = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    //if (!PNGWriter.IsValid())
    //{
    //    UE_LOG(LogTemp, Error, TEXT("Failed to create PNG writer."));
    //    return false;
    //}

    //PNGWriter->SetRaw(ORMPixels.GetData(), ORMPixels.GetAllocatedSize(), Width, Height, ERGBFormat::RGBA, 8);

    //const TArray64<uint8>& PNGData = PNGWriter->GetCompressed();

    //// Ensure directory exists
    //IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    //FString Directory = FPaths::GetPath(SavePath);
    //if (!PlatformFile.DirectoryExists(*SavePath))
    //{
    //    PlatformFile.CreateDirectoryTree(*SavePath);
    //}
    //FString FilePAth = SavePath + FString("/MyORMTexture.png");
    //if (FFileHelper::SaveArrayToFile(PNGData, *FilePAth))
    //{
    //    UE_LOG(LogTemp, Log, TEXT("Saved ORM texture to: %s"), *FilePAth);
    //    return true;
    //}
    //else
    //{
    //    UE_LOG(LogTemp, Error, TEXT("Failed to save PNG to: %s"), *FilePAth);
    //    return false;
    //}
    return NewORMTexture;
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





