// EpicUnrealMCPTextureImportCommands.cpp

#include "Commands/EpicUnrealMCPTextureImportCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture.h"
#include "Engine/TextureDefines.h"
#include "Factories/TextureFactory.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "UObject/SavePackage.h"

namespace EpicUnrealMCPTextureImportLocal
{
	// PRK feedback_unity_anon_namespace_collision: named namespace, not anonymous.

	TextureCompressionSettings ParseCompression(const FString& Name)
	{
		if (Name.Equals(TEXT("UserInterface2D"), ESearchCase::IgnoreCase)) return TC_EditorIcon;
		if (Name.Equals(TEXT("EditorIcon"),      ESearchCase::IgnoreCase)) return TC_EditorIcon;
		if (Name.Equals(TEXT("Alpha"),           ESearchCase::IgnoreCase)) return TC_Alpha;
		if (Name.Equals(TEXT("Normalmap"),       ESearchCase::IgnoreCase)) return TC_Normalmap;
		if (Name.Equals(TEXT("Masks"),           ESearchCase::IgnoreCase)) return TC_Masks;
		if (Name.Equals(TEXT("Grayscale"),       ESearchCase::IgnoreCase)) return TC_Grayscale;
		if (Name.Equals(TEXT("HDR"),             ESearchCase::IgnoreCase)) return TC_HDR;
		return TC_Default;
	}

	TextureGroup ParseLODGroup(const FString& Name)
	{
		if (Name.Equals(TEXT("UI"),              ESearchCase::IgnoreCase)) return TEXTUREGROUP_UI;
		if (Name.Equals(TEXT("WorldNormalMap"),  ESearchCase::IgnoreCase)) return TEXTUREGROUP_WorldNormalMap;
		if (Name.Equals(TEXT("Character"),       ESearchCase::IgnoreCase)) return TEXTUREGROUP_Character;
		if (Name.Equals(TEXT("Effects"),         ESearchCase::IgnoreCase)) return TEXTUREGROUP_Effects;
		return TEXTUREGROUP_World;
	}

	// Role preset — drives compression, sRGB, sampler, LOD group, mips, streaming.
	struct FRolePreset
	{
		TextureCompressionSettings Compression;
		bool                       bSRGB;
		TextureGroup               LODGroup;
		bool                       bNoMips;
		bool                       bNeverStream;
		// Sampler type is set on the Texture via Texture->Filter implicitly through
		// CompressionSettings + sRGB; the per-MaterialExpression SamplerType is a
		// separate concern for downstream wiring. The asset itself is correctly typed
		// by Compression+sRGB alone.
	};

	bool ResolveRole(const FString& RoleName, FRolePreset& OutPreset, FString& OutError)
	{
		if (RoleName.Equals(TEXT("Color"),        ESearchCase::IgnoreCase))
		{
			OutPreset = { TC_Default,   true,  TEXTUREGROUP_World, false, false };
			return true;
		}
		if (RoleName.Equals(TEXT("Normal"),       ESearchCase::IgnoreCase))
		{
			OutPreset = { TC_Normalmap, false, TEXTUREGROUP_WorldNormalMap, false, false };
			return true;
		}
		if (RoleName.Equals(TEXT("ORD"),          ESearchCase::IgnoreCase) ||
		    RoleName.Equals(TEXT("Mask"),         ESearchCase::IgnoreCase))
		{
			OutPreset = { TC_Masks,     false, TEXTUREGROUP_World, false, false };
			return true;
		}
		if (RoleName.Equals(TEXT("Displacement"), ESearchCase::IgnoreCase))
		{
			OutPreset = { TC_Grayscale, false, TEXTUREGROUP_World, false, false };
			return true;
		}
		if (RoleName.Equals(TEXT("BlueNoise"),    ESearchCase::IgnoreCase))
		{
			OutPreset = { TC_Masks,     false, TEXTUREGROUP_World, true,  false };
			return true;
		}
		if (RoleName.Equals(TEXT("UI"),           ESearchCase::IgnoreCase))
		{
			OutPreset = { TC_EditorIcon, true, TEXTUREGROUP_UI,    true,  true };
			return true;
		}
		OutError = FString::Printf(
			TEXT("UnknownRole: '%s'; valid: Color, Normal, ORD, Mask, Displacement, BlueNoise, UI"),
			*RoleName);
		return false;
	}
} // namespace EpicUnrealMCPTextureImportLocal

TSharedPtr<FJsonObject> FEpicUnrealMCPTextureImportCommands::HandleCommand(
	const FString& CommandType,
	const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("import_texture_for_icon"))
	{
		return HandleImportTextureForIcon(Params);
	}
	if (CommandType == TEXT("import_texture"))
	{
		return HandleImportTexture(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown texture import command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPTextureImportCommands::HandleImportTextureForIcon(
	const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("source_file")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: source_file"));
	}
	if (!Params->HasField(TEXT("destination_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: destination_path"));
	}
	if (!Params->HasField(TEXT("asset_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_name"));
	}

	const FString SourceFile = Params->GetStringField(TEXT("source_file"));
	const FString DestinationPath = Params->GetStringField(TEXT("destination_path"));
	const FString AssetName = Params->GetStringField(TEXT("asset_name"));

	if (!FPaths::FileExists(SourceFile))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Source file does not exist: %s"), *SourceFile));
	}

	const bool bReplaceExisting = !Params->HasField(TEXT("replace_existing"))
		|| Params->GetBoolField(TEXT("replace_existing"));
	const bool bSRGB = !Params->HasField(TEXT("srgb"))
		|| Params->GetBoolField(TEXT("srgb"));
	const bool bNoMips = !Params->HasField(TEXT("no_mips"))
		|| Params->GetBoolField(TEXT("no_mips"));
	const bool bNeverStream = !Params->HasField(TEXT("never_stream"))
		|| Params->GetBoolField(TEXT("never_stream"));
	const FString CompressionName = Params->HasField(TEXT("compression_settings"))
		? Params->GetStringField(TEXT("compression_settings"))
		: FString(TEXT("UserInterface2D"));
	const bool bDeleteSource = Params->HasField(TEXT("delete_source_after_import"))
		&& Params->GetBoolField(TEXT("delete_source_after_import"));

	UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
	ImportTask->AddToRoot();
	ImportTask->Filename = SourceFile;
	ImportTask->DestinationPath = DestinationPath;
	ImportTask->DestinationName = AssetName;
	ImportTask->bReplaceExisting = bReplaceExisting;
	ImportTask->bAutomated = true;
	ImportTask->bSave = true;

	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	TextureFactory->SuppressImportOverwriteDialog();
	ImportTask->Factory = TextureFactory;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.ImportAssetTasks({ ImportTask });

	const TArray<UObject*>& ImportedObjects = ImportTask->GetObjects();
	if (ImportedObjects.Num() == 0)
	{
		ImportTask->RemoveFromRoot();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Texture import failed (no asset created). Check source format and destination path."));
	}

	UTexture2D* Texture = Cast<UTexture2D>(ImportedObjects[0]);
	if (!Texture)
	{
		ImportTask->RemoveFromRoot();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Imported asset is not a UTexture2D."));
	}

	Texture->PreEditChange(nullptr);
	Texture->LODGroup = TEXTUREGROUP_UI;
	Texture->CompressionSettings = EpicUnrealMCPTextureImportLocal::ParseCompression(CompressionName);
	Texture->SRGB = bSRGB;
	Texture->MipGenSettings = bNoMips ? TMGS_NoMipmaps : TMGS_FromTextureGroup;
	Texture->NeverStream = bNeverStream;
	Texture->PostEditChange();
	Texture->MarkPackageDirty();

	UPackage* Package = Texture->GetOutermost();
	if (Package)
	{
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bForceByteSwapping = false;
		SaveArgs.bWarnOfLongFilename = false;
		UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs);
	}

	if (bDeleteSource)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.DeleteFile(*SourceFile);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Texture->GetPathName());
	Result->SetStringField(TEXT("package_name"), Package ? Package->GetName() : FString());
	Result->SetNumberField(TEXT("size_x"), Texture->GetSizeX());
	Result->SetNumberField(TEXT("size_y"), Texture->GetSizeY());

	ImportTask->RemoveFromRoot();
	return Result;
}

// ============================================================================
// import_texture — role-aware import. See header for the role table.
//
// Why a separate handler instead of just extending import_texture_for_icon:
// the per-role compression / sRGB / sampler / LOD-group combination is what
// silently breaks downstream uses when wrong (PRK feedback_ord_texture_import:
// "Megascans ORDs ship with Color settings → SAMPLERTYPE_Masks compile fails").
// Encoding the role mapping centrally means a caller can't accidentally
// import an ORD with sRGB on, and the failure mode is "role unknown" up
// front rather than "shader compile broke 200 materials later."
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPTextureImportCommands::HandleImportTexture(
	const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPTextureImportLocal;

	for (const TCHAR* Required : { TEXT("source_file"), TEXT("destination_path"), TEXT("asset_name"), TEXT("role") })
	{
		if (!Params->HasField(Required))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Missing required field: %s"), Required));
		}
	}

	const FString SourceFile      = Params->GetStringField(TEXT("source_file"));
	const FString DestinationPath = Params->GetStringField(TEXT("destination_path"));
	const FString AssetName       = Params->GetStringField(TEXT("asset_name"));
	const FString RoleName        = Params->GetStringField(TEXT("role"));

	if (!FPaths::FileExists(SourceFile))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Source file does not exist: %s"), *SourceFile));
	}

	FRolePreset Preset;
	FString RoleErr;
	if (!ResolveRole(RoleName, Preset, RoleErr))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(RoleErr);
	}

	// Per-role overrides — caller can still override individual settings.
	if (Params->HasField(TEXT("srgb")))         Preset.bSRGB         = Params->GetBoolField(TEXT("srgb"));
	if (Params->HasField(TEXT("never_stream"))) Preset.bNeverStream  = Params->GetBoolField(TEXT("never_stream"));
	if (Params->HasField(TEXT("no_mips")))      Preset.bNoMips       = Params->GetBoolField(TEXT("no_mips"));
	if (Params->HasField(TEXT("compression"))) Preset.Compression   = ParseCompression(Params->GetStringField(TEXT("compression")));
	if (Params->HasField(TEXT("lod_group")))   Preset.LODGroup      = ParseLODGroup(Params->GetStringField(TEXT("lod_group")));

	const bool bReplaceExisting = !Params->HasField(TEXT("replace_existing"))
		|| Params->GetBoolField(TEXT("replace_existing"));
	const bool bDeleteSource = Params->HasField(TEXT("delete_source_after_import"))
		&& Params->GetBoolField(TEXT("delete_source_after_import"));

	UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
	ImportTask->AddToRoot();
	ImportTask->Filename = SourceFile;
	ImportTask->DestinationPath = DestinationPath;
	ImportTask->DestinationName = AssetName;
	ImportTask->bReplaceExisting = bReplaceExisting;
	ImportTask->bAutomated = true;
	ImportTask->bSave = true;

	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	TextureFactory->SuppressImportOverwriteDialog();
	// Tell the factory not to second-guess our intent for Normal/Mask roles —
	// the factory's auto-detection sometimes flips sRGB based on filename hints.
	if (Preset.Compression == TC_Normalmap)
	{
		TextureFactory->NoCompression = false;
		TextureFactory->bFlipNormalMapGreenChannel = false;
	}
	ImportTask->Factory = TextureFactory;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.ImportAssetTasks({ ImportTask });

	const TArray<UObject*>& ImportedObjects = ImportTask->GetObjects();
	if (ImportedObjects.Num() == 0)
	{
		ImportTask->RemoveFromRoot();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Texture import failed (no asset created). Check source format and destination path."));
	}

	UTexture2D* Texture = Cast<UTexture2D>(ImportedObjects[0]);
	if (!Texture)
	{
		ImportTask->RemoveFromRoot();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Imported asset is not a UTexture2D."));
	}

	Texture->PreEditChange(nullptr);
	Texture->LODGroup           = Preset.LODGroup;
	Texture->CompressionSettings = Preset.Compression;
	Texture->SRGB               = Preset.bSRGB;
	Texture->MipGenSettings     = Preset.bNoMips ? TMGS_NoMipmaps : TMGS_FromTextureGroup;
	Texture->NeverStream        = Preset.bNeverStream;
	Texture->PostEditChange();
	Texture->MarkPackageDirty();

	// Post-condition verify: re-read the settings we just wrote. If anything
	// regressed (factory post-processing can clobber), report it explicitly.
	const bool bSettingsHeld =
		Texture->LODGroup == Preset.LODGroup &&
		Texture->CompressionSettings == Preset.Compression &&
		(Texture->SRGB ? 1 : 0) == (Preset.bSRGB ? 1 : 0) &&
		(Texture->NeverStream ? 1 : 0) == (Preset.bNeverStream ? 1 : 0);

	UPackage* Package = Texture->GetOutermost();
	if (Package)
	{
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bForceByteSwapping = false;
		SaveArgs.bWarnOfLongFilename = false;
		UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs);
	}

	if (bDeleteSource)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.DeleteFile(*SourceFile);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Texture->GetPathName());
	Result->SetStringField(TEXT("package_name"), Package ? Package->GetName() : FString());
	Result->SetStringField(TEXT("role"), RoleName);
	Result->SetNumberField(TEXT("size_x"), Texture->GetSizeX());
	Result->SetNumberField(TEXT("size_y"), Texture->GetSizeY());
	Result->SetBoolField(TEXT("srgb"), Texture->SRGB);
	Result->SetNumberField(TEXT("compression_settings"), static_cast<int32>(Texture->CompressionSettings));
	Result->SetNumberField(TEXT("lod_group"), static_cast<int32>(Texture->LODGroup));
	if (!bSettingsHeld)
	{
		Result->SetStringField(TEXT("warning"),
			TEXT("Some settings did not hold after PostEditChange (factory may have overridden). Inspect texture asset manually."));
	}

	ImportTask->RemoveFromRoot();
	return Result;
}
