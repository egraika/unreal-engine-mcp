#include "Commands/EpicUnrealMCPThumbnailCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EditorAssetLibrary.h"
#include "ImageUtils.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "UObject/SavePackage.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "EngineUtils.h"

// For UPRKEquippableItemData resolution
#include "Inventory/Data/PRKEquippableItemData.h"

FEpicUnrealMCPThumbnailCommands::FEpicUnrealMCPThumbnailCommands()
{
}

FEpicUnrealMCPThumbnailCommands::~FEpicUnrealMCPThumbnailCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPThumbnailCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("generate_thumbnail"))
	{
		return HandleGenerateThumbnail(Params);
	}
	else if (CommandType == TEXT("generate_thumbnails"))
	{
		return HandleGenerateThumbnails(Params);
	}
	else if (CommandType == TEXT("scan_assets_for_thumbnails"))
	{
		return HandleScanAssetsForThumbnails(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown thumbnail command: %s"), *CommandType));
}

// ============================================================================
// Settings parsing
// ============================================================================

FEpicUnrealMCPThumbnailCommands::FThumbnailSettings FEpicUnrealMCPThumbnailCommands::ParseSettings(const TSharedPtr<FJsonObject>& Params) const
{
	FThumbnailSettings Settings;

	if (Params->HasField(TEXT("resolution")))
	{
		Settings.Resolution = FMath::Clamp(static_cast<int32>(Params->GetNumberField(TEXT("resolution"))), 64, 2048);
	}
	if (Params->HasField(TEXT("transparent")))
	{
		Settings.bTransparent = Params->GetBoolField(TEXT("transparent"));
	}
	if (Params->HasField(TEXT("save_directory")))
	{
		Settings.SaveDirectory = Params->GetStringField(TEXT("save_directory"));
		if (!Settings.SaveDirectory.StartsWith(TEXT("/Game/")))
		{
			Settings.SaveDirectory = FString::Printf(TEXT("/Game/%s"), *Settings.SaveDirectory);
		}
	}
	if (Params->HasField(TEXT("camera_fov")))
	{
		Settings.CameraFOV = FMath::Clamp(static_cast<float>(Params->GetNumberField(TEXT("camera_fov"))), 5.0f, 120.0f);
	}
	if (Params->HasField(TEXT("camera_pitch")))
	{
		Settings.CameraPitch = static_cast<float>(Params->GetNumberField(TEXT("camera_pitch")));
	}
	if (Params->HasField(TEXT("camera_yaw")))
	{
		Settings.CameraYaw = static_cast<float>(Params->GetNumberField(TEXT("camera_yaw")));
	}
	if (Params->HasField(TEXT("ambient_light_only")))
	{
		Settings.bAmbientLightOnly = Params->GetBoolField(TEXT("ambient_light_only"));
	}
	if (Params->HasField(TEXT("export_png")))
	{
		Settings.bExportPNG = Params->GetBoolField(TEXT("export_png"));
	}
	if (Params->HasField(TEXT("export_disk_path")))
	{
		Settings.ExportDiskPath = Params->GetStringField(TEXT("export_disk_path"));
	}

	return Settings;
}

// ============================================================================
// Command handlers
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPThumbnailCommands::HandleGenerateThumbnail(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FThumbnailSettings Settings = ParseSettings(Params);

	FString SavedPath;
	if (!RenderThumbnail(AssetPath, Settings, SavedPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to generate thumbnail for: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("saved_path"), SavedPath);

	if (Settings.bExportPNG && !Settings.ExportDiskPath.IsEmpty())
	{
		FString AssetName = FPaths::GetBaseFilename(AssetPath);
		AssetName.RemoveFromStart(TEXT("DA_"));
		AssetName.RemoveFromStart(TEXT("SM_"));
		AssetName.RemoveFromStart(TEXT("SK_"));
		FString PNGPath = FPaths::Combine(Settings.ExportDiskPath, FString::Printf(TEXT("Thumb_%s.png"), *AssetName));
		Result->SetStringField(TEXT("export_png_path"), PNGPath);
	}

	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPThumbnailCommands::HandleGenerateThumbnails(const TSharedPtr<FJsonObject>& Params)
{
	FThumbnailSettings Settings = ParseSettings(Params);
	TArray<FString> AllAssetPaths;

	if (Params->HasField(TEXT("asset_paths")))
	{
		const TArray<TSharedPtr<FJsonValue>>& PathsArray = Params->GetArrayField(TEXT("asset_paths"));
		for (const auto& Val : PathsArray)
		{
			AllAssetPaths.Add(Val->AsString());
		}
	}

	if (Params->HasField(TEXT("directories")))
	{
		const TArray<TSharedPtr<FJsonValue>>& DirsArray = Params->GetArrayField(TEXT("directories"));
		for (const auto& Val : DirsArray)
		{
			ScanDirectoryForMeshes(Val->AsString(), true, true, AllAssetPaths);
		}
	}

	if (AllAssetPaths.Num() == 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No assets found. Provide asset_paths and/or directories."));
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;
	int32 FailCount = 0;

	for (const FString& AssetPath : AllAssetPaths)
	{
		FString SavedPath;
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("asset_path"), AssetPath);

		if (RenderThumbnail(AssetPath, Settings, SavedPath))
		{
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("saved_path"), SavedPath);
			SuccessCount++;
		}
		else
		{
			Entry->SetBoolField(TEXT("success"), false);
			Entry->SetStringField(TEXT("error"), TEXT("Render failed"));
			FailCount++;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("total"), AllAssetPaths.Num());
	Result->SetNumberField(TEXT("succeeded"), SuccessCount);
	Result->SetNumberField(TEXT("failed"), FailCount);
	Result->SetArrayField(TEXT("results"), ResultsArray);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPThumbnailCommands::HandleScanAssetsForThumbnails(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("directories")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: directories"));
	}

	bool bIncludeStatic = true;
	bool bIncludeSkeletal = true;

	if (Params->HasField(TEXT("include_static")))
	{
		bIncludeStatic = Params->GetBoolField(TEXT("include_static"));
	}
	if (Params->HasField(TEXT("include_skeletal")))
	{
		bIncludeSkeletal = Params->GetBoolField(TEXT("include_skeletal"));
	}

	TArray<FString> FoundPaths;
	const TArray<TSharedPtr<FJsonValue>>& DirsArray = Params->GetArrayField(TEXT("directories"));
	for (const auto& Val : DirsArray)
	{
		ScanDirectoryForMeshes(Val->AsString(), bIncludeStatic, bIncludeSkeletal, FoundPaths);
	}

	TArray<TSharedPtr<FJsonValue>> PathsJson;
	for (const FString& Path : FoundPaths)
	{
		PathsJson.Add(MakeShareable(new FJsonValueString(Path)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), FoundPaths.Num());
	Result->SetArrayField(TEXT("asset_paths"), PathsJson);
	return Result;
}

// ============================================================================
// Directory scanning
// ============================================================================

void FEpicUnrealMCPThumbnailCommands::ScanDirectoryForMeshes(const FString& DirectoryPath, bool bIncludeStatic, bool bIncludeSkeletal, TArray<FString>& OutPaths) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetDataList;

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*DirectoryPath));
	Filter.bRecursivePaths = true;

	if (bIncludeStatic)
	{
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	}
	if (bIncludeSkeletal)
	{
		Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
	}

	AssetRegistry.GetAssets(Filter, AssetDataList);

	for (const FAssetData& Data : AssetDataList)
	{
		FString PackagePath = Data.GetObjectPathString();
		if (!OutPaths.Contains(PackagePath))
		{
			OutPaths.Add(PackagePath);
		}
	}

	UE_LOG(LogTemp, Display, TEXT("ThumbnailCommands: Scanned '%s' — found %d meshes"), *DirectoryPath, AssetDataList.Num());
}

// ============================================================================
// Mesh path resolution
// ============================================================================

FString FEpicUnrealMCPThumbnailCommands::ResolveMeshPath(const FString& AssetPath) const
{
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("ThumbnailCommands: Failed to load asset: %s"), *AssetPath);
		return FString();
	}

	if (LoadedAsset->IsA<UStaticMesh>() || LoadedAsset->IsA<USkeletalMesh>())
	{
		return AssetPath;
	}

	if (UPRKEquippableItemData* EquipData = Cast<UPRKEquippableItemData>(LoadedAsset))
	{
		if (!EquipData->EquipStaticMesh.IsNull())
		{
			return EquipData->EquipStaticMesh.ToString();
		}
		if (!EquipData->EquipSkeletalMesh.IsNull())
		{
			return EquipData->EquipSkeletalMesh.ToString();
		}
		UE_LOG(LogTemp, Warning, TEXT("ThumbnailCommands: EquippableItemData '%s' has no mesh references"), *AssetPath);
		return FString();
	}

	if (UPRKItemData* ItemData = Cast<UPRKItemData>(LoadedAsset))
	{
		if (!ItemData->WorldMesh.IsNull())
		{
			return ItemData->WorldMesh.ToString();
		}
		UE_LOG(LogTemp, Warning, TEXT("ThumbnailCommands: ItemData '%s' has no WorldMesh"), *AssetPath);
		return FString();
	}

	UE_LOG(LogTemp, Warning, TEXT("ThumbnailCommands: Asset '%s' is not a mesh or known data type (%s)"), *AssetPath, *LoadedAsset->GetClass()->GetName());
	return FString();
}

// ============================================================================
// Console variable management
// ============================================================================

void FEpicUnrealMCPThumbnailCommands::DisablePostProcessFeatures()
{
	static const TArray<TPair<FString, FString>> CVarsToSet = {
		{TEXT("r.DefaultFeature.AntiAliasing"), TEXT("0")},
		{TEXT("r.DefaultFeature.MotionBlur"), TEXT("0")},
		// NOTE: r.DefaultFeature.AutoExposure intentionally NOT disabled —
		// it conflicts with our per-capture manual exposure override (AEM_Manual)
		{TEXT("r.ScreenSpaceReflections"), TEXT("0")},
		{TEXT("r.BloomQuality"), TEXT("0")},
		{TEXT("r.DepthOfFieldQuality"), TEXT("0")},
		{TEXT("r.AmbientOcclusionLevels"), TEXT("0")},
		{TEXT("r.LensFlareQuality"), TEXT("0")},
		{TEXT("r.Tonemapper.GrainQuantization"), TEXT("0")},
	};

	SavedCVarValues.Empty();

	for (const auto& Pair : CVarsToSet)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Pair.Key);
		if (CVar)
		{
			SavedCVarValues.Add(Pair.Key, CVar->GetString());
			CVar->Set(*Pair.Value);
		}
	}
}

void FEpicUnrealMCPThumbnailCommands::RestorePostProcessFeatures()
{
	for (const auto& Pair : SavedCVarValues)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Pair.Key);
		if (CVar)
		{
			CVar->Set(*Pair.Value);
		}
	}
	SavedCVarValues.Empty();
}

// ============================================================================
// Thumbnail Generator Pro delegation
// ============================================================================

// Helper: set a Blueprint property by searching authored names (handles ? in BP var names)
static FProperty* FindBPProperty(UClass* Class, const FString& NameSubstring)
{
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		if (It->GetAuthoredName().Contains(NameSubstring) || It->GetName().Contains(NameSubstring))
		{
			return *It;
		}
	}
	return nullptr;
}

static void SetBPBool(UObject* Obj, UClass* Class, const FString& Name, bool Value)
{
	if (FBoolProperty* Prop = CastField<FBoolProperty>(FindBPProperty(Class, Name)))
	{
		Prop->SetPropertyValue_InContainer(Obj, Value);
	}
}

static void SetBPFloat(UObject* Obj, UClass* Class, const FString& Name, float Value)
{
	if (FProperty* Prop = FindBPProperty(Class, Name))
	{
		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		{
			FP->SetPropertyValue_InContainer(Obj, Value);
		}
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		{
			DP->SetPropertyValue_InContainer(Obj, static_cast<double>(Value));
		}
	}
}

static void SetBPInt(UObject* Obj, UClass* Class, const FString& Name, int32 Value)
{
	if (FIntProperty* Prop = CastField<FIntProperty>(FindBPProperty(Class, Name)))
	{
		Prop->SetPropertyValue_InContainer(Obj, Value);
	}
}

static void SetBPString(UObject* Obj, UClass* Class, const FString& Name, const FString& Value)
{
	if (FStrProperty* Prop = CastField<FStrProperty>(FindBPProperty(Class, Name)))
	{
		Prop->SetPropertyValue_InContainer(Obj, Value);
	}
}

bool FEpicUnrealMCPThumbnailCommands::RenderThumbnailViaTGP(const FString& AssetPath, const FThumbnailSettings& Settings, FString& OutSavedPath)
{
	// Resolve to actual mesh path
	FString MeshPath = ResolveMeshPath(AssetPath);
	if (MeshPath.IsEmpty())
	{
		return false;
	}

	UObject* MeshAsset = UEditorAssetLibrary::LoadAsset(MeshPath);
	if (!MeshAsset)
	{
		return false;
	}

	UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshAsset);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshAsset);
	if (!StaticMesh && !SkeletalMesh)
	{
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return false;
	}

	// Find the TGP Blueprint class
	UClass* TGPClass = LoadClass<AActor>(nullptr,
		TEXT("/ThumbnailGeneratorPro/ThumbnailGeneratorPro/Blueprints/EUB_ThumbnailGenerator.EUB_ThumbnailGenerator_C"));
	if (!TGPClass)
	{
		UE_LOG(LogTemp, Display, TEXT("ThumbnailCommands: TGP plugin not installed"));
		return false;
	}

	// Find existing TGP actor (spawned by the TGP widget when it's open)
	// NOTE: We cannot spawn the EUB ourselves — its BP functions reference EUWRef (the widget)
	// and crash with a null dereference if the widget didn't create it.
	AActor* TGPActor = nullptr;
	for (TActorIterator<AActor> It(World, TGPClass); It; ++It)
	{
		TGPActor = *It;
		break;
	}

	if (!TGPActor)
	{
		UE_LOG(LogTemp, Display, TEXT("ThumbnailCommands: TGP widget not open — open Thumbnail Generator Pro window first, or will use fallback pipeline"));
		return false;
	}

	// Find mesh components by name
	UStaticMeshComponent* TGPStaticMeshComp = nullptr;
	USkeletalMeshComponent* TGPSkeletalMeshComp = nullptr;
	for (UActorComponent* Comp : TGPActor->GetComponents())
	{
		if (Comp->GetName() == TEXT("StaticMesh"))
		{
			TGPStaticMeshComp = Cast<UStaticMeshComponent>(Comp);
		}
		else if (Comp->GetName() == TEXT("SkeletalMesh"))
		{
			TGPSkeletalMeshComp = Cast<USkeletalMeshComponent>(Comp);
		}
	}

	// Set the mesh and ObjectToDisplay enum
	FProperty* OTDProp = FindBPProperty(TGPClass, TEXT("ObjectToDisplay"));
	if (StaticMesh && TGPStaticMeshComp)
	{
		TGPStaticMeshComp->SetStaticMesh(StaticMesh);
		if (OTDProp)
		{
			*OTDProp->ContainerPtrToValuePtr<uint8>(TGPActor) = 0; // StaticMesh
		}
	}
	else if (SkeletalMesh && TGPSkeletalMeshComp)
	{
		TGPSkeletalMeshComp->SetSkeletalMeshAsset(SkeletalMesh);
		if (OTDProp)
		{
			*OTDProp->ContainerPtrToValuePtr<uint8>(TGPActor) = 1; // SkeletalMesh
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ThumbnailCommands: TGP missing mesh component for asset type"));
		return false;
	}

	// Derive output name
	FString AssetName = FPaths::GetBaseFilename(AssetPath);

	// Set OutputFileName and DestinationFolderPath
	SetBPString(TGPActor, TGPClass, TEXT("OutputFileName"), AssetName);
	FString Folder = Settings.SaveDirectory;
	if (!Folder.EndsWith(TEXT("/")))
	{
		Folder += TEXT("/");
	}
	SetBPString(TGPActor, TGPClass, TEXT("DestinationFolderPath"), Folder);

	// Call ShowStaticOrSkeletalMesh → sets visibility
	if (UFunction* ShowFunc = TGPClass->FindFunctionByName(TEXT("ShowStaticOrSkeletalMesh")))
	{
		TGPActor->ProcessEvent(ShowFunc, nullptr);
	}

	// Call UpdateMeshs → auto-position mesh in frame
	if (UFunction* UpdateMeshsFunc = TGPClass->FindFunctionByName(TEXT("UpdateMeshs")))
	{
		TGPActor->ProcessEvent(UpdateMeshsFunc, nullptr);
	}

	// Call UpdateEUB → refresh lights, camera, post-process, capture
	if (UFunction* UpdateFunc = TGPClass->FindFunctionByName(TEXT("UpdateEUB")))
	{
		TGPActor->ProcessEvent(UpdateFunc, nullptr);
	}

	// Force capture refresh
	if (UFunction* CaptureFunc = TGPClass->FindFunctionByName(TEXT("UpdateCameraCaptureRender")))
	{
		TGPActor->ProcessEvent(CaptureFunc, nullptr);
	}

	FlushRenderingCommands();

	// Save the texture
	if (UFunction* SaveFunc = TGPClass->FindFunctionByName(TEXT("SaveStaticTexture")))
	{
		TGPActor->ProcessEvent(SaveFunc, nullptr);
	}

	OutSavedPath = FString::Printf(TEXT("%s/%s"), *Settings.SaveDirectory, *AssetName);
	UE_LOG(LogTemp, Display, TEXT("ThumbnailCommands: Generated via TGP → %s"), *OutSavedPath);
	return true;
}

// ============================================================================
// Core rendering pipeline (fallback when TGP not available)
// ============================================================================

bool FEpicUnrealMCPThumbnailCommands::RenderThumbnail(const FString& AssetPath, const FThumbnailSettings& Settings, FString& OutSavedPath)
{
	// Try Thumbnail Generator Pro first — uses user's configured lighting/camera settings
	if (RenderThumbnailViaTGP(AssetPath, Settings, OutSavedPath))
	{
		return true;
	}

	UE_LOG(LogTemp, Display, TEXT("ThumbnailCommands: TGP not available, using custom SceneCapture pipeline"));

	// Resolve to actual mesh path
	FString MeshPath = ResolveMeshPath(AssetPath);
	if (MeshPath.IsEmpty())
	{
		return false;
	}

	UObject* MeshAsset = UEditorAssetLibrary::LoadAsset(MeshPath);
	if (!MeshAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("ThumbnailCommands: Failed to load mesh: %s"), *MeshPath);
		return false;
	}

	UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshAsset);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshAsset);
	if (!StaticMesh && !SkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("ThumbnailCommands: Asset is not a mesh: %s"), *MeshPath);
		return false;
	}

	DisablePostProcessFeatures();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		RestorePostProcessFeatures();
		UE_LOG(LogTemp, Error, TEXT("ThumbnailCommands: No editor world available"));
		return false;
	}

	// Create render target
	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RenderTarget->InitCustomFormat(Settings.Resolution, Settings.Resolution, PF_B8G8R8A8, false);
	RenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	RenderTarget->UpdateResourceImmediate(true);

	// Spawn mesh at world origin — uses the current level's lighting (studio level)
	const FVector SpawnOrigin(0.0f, 0.0f, 0.0f);

	AActor* MeshActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnOrigin, FRotator::ZeroRotator);
	if (!MeshActor)
	{
		RestorePostProcessFeatures();
		return false;
	}
	MeshActor->SetFlags(RF_Transient);

	USceneComponent* RootComp = NewObject<USceneComponent>(MeshActor, TEXT("Root"));
	MeshActor->SetRootComponent(RootComp);
	RootComp->RegisterComponent();

	FBoxSphereBounds MeshBounds;

	if (StaticMesh)
	{
		UStaticMeshComponent* SMComp = NewObject<UStaticMeshComponent>(MeshActor, TEXT("MeshComp"));
		SMComp->SetupAttachment(RootComp);
		SMComp->SetStaticMesh(StaticMesh);
		SMComp->RegisterComponent();
		MeshBounds = SMComp->CalcBounds(SMComp->GetComponentTransform());
	}
	else
	{
		USkeletalMeshComponent* SKComp = NewObject<USkeletalMeshComponent>(MeshActor, TEXT("MeshComp"));
		SKComp->SetupAttachment(RootComp);
		SKComp->SetSkeletalMeshAsset(SkeletalMesh);
		SKComp->RegisterComponent();
		MeshBounds = SKComp->CalcBounds(SKComp->GetComponentTransform());
	}

	UE_LOG(LogTemp, Display, TEXT("ThumbnailCommands: Mesh '%s' bounds — Origin(%s) Radius(%.1f)"),
		*MeshPath, *MeshBounds.Origin.ToString(), MeshBounds.SphereRadius);

	// Camera placement — match portrait camera pattern:
	// Position relative to mesh center using bounds-based distance
	const FVector BoundsCenter = MeshBounds.Origin;
	const float BoundsRadius = FMath::Max(MeshBounds.SphereRadius, 1.0f);
	const float HalfFOVRad = FMath::DegreesToRadians(Settings.CameraFOV * 0.5f);
	const float CameraDistance = BoundsRadius / FMath::Tan(HalfFOVRad) * 1.5f;

	// Camera looks TOWARD the mesh center from the computed direction
	const FRotator CameraRotation(Settings.CameraPitch, Settings.CameraYaw, 0.0f);
	const FVector CameraDirection = CameraRotation.Vector();
	const FVector CameraLocation = BoundsCenter - CameraDirection * CameraDistance;

	UE_LOG(LogTemp, Display, TEXT("ThumbnailCommands: Camera at (%s) looking at (%s) dist=%.1f"),
		*CameraLocation.ToString(), *BoundsCenter.ToString(), CameraDistance);

	// Spawn capture component
	AActor* CaptureActor = World->SpawnActor<AActor>(AActor::StaticClass(), CameraLocation, FRotator::ZeroRotator);
	CaptureActor->SetFlags(RF_Transient);

	USceneComponent* CaptureRoot = NewObject<USceneComponent>(CaptureActor, TEXT("Root"));
	CaptureActor->SetRootComponent(CaptureRoot);
	CaptureRoot->RegisterComponent();

	USceneCaptureComponent2D* CaptureComp = NewObject<USceneCaptureComponent2D>(CaptureActor, TEXT("Capture"));
	CaptureComp->SetupAttachment(CaptureRoot);
	CaptureComp->TextureTarget = RenderTarget;
	CaptureComp->FOVAngle = Settings.CameraFOV;
	CaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
	CaptureComp->bCaptureEveryFrame = false;
	CaptureComp->bCaptureOnMovement = false;
	CaptureComp->bAlwaysPersistRenderingState = true;
	// ShowOnlyList: renders ONLY the mesh, but level lights still illuminate it
	CaptureComp->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	CaptureComp->ShowOnlyActors.Add(MeshActor);
	CaptureComp->SetWorldLocationAndRotation(CameraLocation, CameraRotation);

	// Disable sky/atmosphere/fog so background is purely ClearColor
	// (Critical for dual-render alpha — otherwise sky renders in both passes,
	//  producing alpha=1.0 everywhere and washing out colors)
	auto AddShowFlagOverride = [&](const FString& FlagName, bool bEnabled)
	{
		FEngineShowFlagsSetting Setting;
		Setting.ShowFlagName = FlagName;
		Setting.Enabled = bEnabled;
		CaptureComp->ShowFlagSettings.Add(Setting);
	};
	AddShowFlagOverride(TEXT("Atmosphere"), false);
	AddShowFlagOverride(TEXT("Fog"), false);
	AddShowFlagOverride(TEXT("VolumetricFog"), false);
	AddShowFlagOverride(TEXT("Cloud"), false);

	// Manual exposure — prevents auto-exposure from blowing out colors
	CaptureComp->PostProcessBlendWeight = 1.0f;
	CaptureComp->PostProcessSettings.bOverride_AutoExposureMethod = true;
	CaptureComp->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	CaptureComp->PostProcessSettings.bOverride_AutoExposureBias = true;
	CaptureComp->PostProcessSettings.AutoExposureBias = 1.0f;

	CaptureComp->RegisterComponent();

	// Lighting — either ambient SkyLight or three-point directional
	AActor* KeyLightActor = nullptr;
	AActor* FillLightActor = nullptr;
	AActor* RimLightActor = nullptr;
	AActor* SkyLightActor = nullptr;

	if (Settings.bAmbientLightOnly)
	{
		// Ambient mode: spawn a SkyLight that captures the level's sky for soft IBL
		// (matches Thumbnail Generator Pro "Use Ambient Light" behavior)
		SkyLightActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnOrigin, FRotator::ZeroRotator);
		SkyLightActor->SetFlags(RF_Transient);
		USceneComponent* SkyRoot = NewObject<USceneComponent>(SkyLightActor, TEXT("Root"));
		SkyLightActor->SetRootComponent(SkyRoot);
		SkyRoot->RegisterComponent();

		USkyLightComponent* SkyLight = NewObject<USkyLightComponent>(SkyLightActor, TEXT("AmbientSkyLight"));
		SkyLight->SetupAttachment(SkyRoot);
		SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
		SkyLight->Intensity = 5.0f;
		SkyLight->bLowerHemisphereIsBlack = false;
		SkyLight->RegisterComponent();
		SkyLight->RecaptureSky();
		FlushRenderingCommands();
	}
	else
	{
		// Three-point directional lighting — self-contained, works regardless of level
		auto SpawnDirLight = [&](const TCHAR* Name, float Intensity, FRotator Rotation) -> AActor*
		{
			AActor* LightActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnOrigin, FRotator::ZeroRotator);
			LightActor->SetFlags(RF_Transient);
			USceneComponent* LightRoot = NewObject<USceneComponent>(LightActor, TEXT("Root"));
			LightActor->SetRootComponent(LightRoot);
			LightRoot->RegisterComponent();

			UDirectionalLightComponent* Light = NewObject<UDirectionalLightComponent>(LightActor, *FString(Name));
			Light->SetupAttachment(LightRoot);
			Light->SetIntensity(Intensity);
			Light->SetWorldRotation(Rotation);
			Light->SetVisibility(true);
			Light->RegisterComponent();
			return LightActor;
		};

		KeyLightActor = SpawnDirLight(TEXT("KeyLight"), 3.0f, FRotator(-45.0f, -30.0f, 0.0f));
		FillLightActor = SpawnDirLight(TEXT("FillLight"), 1.5f, FRotator(-30.0f, 150.0f, 0.0f));
		RimLightActor = SpawnDirLight(TEXT("RimLight"), 2.0f, FRotator(-15.0f, 180.0f, 0.0f));
	}

	// Ensure all spawned lights are fully registered with the renderer before capture
	FlushRenderingCommands();

	// ---- Pass 1: Capture with BLACK background ----
	RenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	RenderTarget->UpdateResourceImmediate(true);
	CaptureComp->CaptureScene();
	FlushRenderingCommands();

	TArray<FColor> BlackPixels;
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		World->DestroyActor(MeshActor);
		World->DestroyActor(CaptureActor);
		if (KeyLightActor) { World->DestroyActor(KeyLightActor); }
		if (FillLightActor) { World->DestroyActor(FillLightActor); }
		if (RimLightActor) { World->DestroyActor(RimLightActor); }
		if (SkyLightActor) { World->DestroyActor(SkyLightActor); }
		RestorePostProcessFeatures();
		return false;
	}

	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	RTResource->ReadPixels(BlackPixels, ReadFlags);

	TArray<FColor> FinalPixels;

	if (Settings.bTransparent)
	{
		// ---- Pass 2: Capture with WHITE background ----
		RenderTarget->ClearColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
		RenderTarget->UpdateResourceImmediate(true);
		CaptureComp->CaptureScene();
		FlushRenderingCommands();

		TArray<FColor> WhitePixels;
		RTResource->ReadPixels(WhitePixels, ReadFlags);

		// Dual-render alpha: foreground pixels are the same on both passes (alpha=1),
		// background pixels differ (black=0, white=255, alpha=0)
		FinalPixels.SetNum(BlackPixels.Num());
		for (int32 i = 0; i < BlackPixels.Num(); i++)
		{
			const FColor& B = BlackPixels[i];
			const FColor& W = WhitePixels[i];

			const float DiffR = static_cast<float>(W.R - B.R) / 255.0f;
			const float DiffG = static_cast<float>(W.G - B.G) / 255.0f;
			const float DiffB = static_cast<float>(W.B - B.B) / 255.0f;
			const float MaxDiff = FMath::Max3(DiffR, DiffG, DiffB);
			const float Alpha = FMath::Clamp(1.0f - MaxDiff, 0.0f, 1.0f);
			const uint8 A = static_cast<uint8>(Alpha * 255.0f);

			if (A > 0)
			{
				const float InvAlpha = 1.0f / Alpha;
				FinalPixels[i].R = static_cast<uint8>(FMath::Clamp(B.R * InvAlpha, 0.0f, 255.0f));
				FinalPixels[i].G = static_cast<uint8>(FMath::Clamp(B.G * InvAlpha, 0.0f, 255.0f));
				FinalPixels[i].B = static_cast<uint8>(FMath::Clamp(B.B * InvAlpha, 0.0f, 255.0f));
				FinalPixels[i].A = A;
			}
			else
			{
				FinalPixels[i] = FColor(0, 0, 0, 0);
			}
		}
	}
	else
	{
		FinalPixels = MoveTemp(BlackPixels);
	}

	// Derive output asset name
	FString AssetName = FPaths::GetBaseFilename(AssetPath);
	AssetName.RemoveFromStart(TEXT("DA_"));
	AssetName.RemoveFromStart(TEXT("SM_"));
	AssetName.RemoveFromStart(TEXT("SK_"));
	FString ThumbAssetName = FString::Printf(TEXT("Thumb_%s"), *AssetName);
	FString PackagePath = FString::Printf(TEXT("%s/%s"), *Settings.SaveDirectory, *ThumbAssetName);

	// Create the texture asset
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();

	UTexture2D* NewTexture = NewObject<UTexture2D>(Package, FName(*ThumbAssetName), RF_Public | RF_Standalone);
	NewTexture->Source.Init(Settings.Resolution, Settings.Resolution, 1, 1, TSF_BGRA8);

	uint8* MipData = NewTexture->Source.LockMip(0);
	FMemory::Memcpy(MipData, FinalPixels.GetData(), FinalPixels.Num() * sizeof(FColor));
	NewTexture->Source.UnlockMip(0);

	NewTexture->SRGB = true;
	NewTexture->CompressionSettings = Settings.bTransparent ? TC_EditorIcon : TC_Default;
	NewTexture->MipGenSettings = TMGS_NoMipmaps;
	NewTexture->LODGroup = TEXTUREGROUP_UI;
	NewTexture->NeverStream = true;

	NewTexture->UpdateResource();
	NewTexture->PostEditChange();
	Package->MarkPackageDirty();

	FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, NewTexture, *PackageFilename, SaveArgs);

	OutSavedPath = PackagePath;
	UE_LOG(LogTemp, Display, TEXT("ThumbnailCommands: Saved thumbnail to %s"), *PackagePath);

	// PNG export to disk
	if (Settings.bExportPNG && !Settings.ExportDiskPath.IsEmpty())
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*Settings.ExportDiskPath);

		FString PNGFilePath = FPaths::Combine(Settings.ExportDiskPath, ThumbAssetName + TEXT(".png"));
		TArray<uint8> PNGData;
		FImageUtils::CompressImageArray(Settings.Resolution, Settings.Resolution, FinalPixels, PNGData);
		FFileHelper::SaveArrayToFile(PNGData, *PNGFilePath);

		UE_LOG(LogTemp, Display, TEXT("ThumbnailCommands: Exported PNG to %s"), *PNGFilePath);
	}

	// Cleanup
	World->DestroyActor(MeshActor);
	World->DestroyActor(CaptureActor);
	if (KeyLightActor) { World->DestroyActor(KeyLightActor); }
	if (FillLightActor) { World->DestroyActor(FillLightActor); }
	if (RimLightActor) { World->DestroyActor(RimLightActor); }
	if (SkyLightActor) { World->DestroyActor(SkyLightActor); }

	RestorePostProcessFeatures();
	return true;
}
