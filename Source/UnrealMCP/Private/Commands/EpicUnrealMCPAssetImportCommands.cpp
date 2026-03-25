// EpicUnrealMCPAssetImportCommands.cpp
// MCP command handler for importing assets into the project

#include "Commands/EpicUnrealMCPAssetImportCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "AssetImportTask.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/Paths.h"

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetImportCommands::HandleCommand(
	const FString& CommandType,
	const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("import_fbx"))
	{
		return HandleImportFBX(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown import command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetImportCommands::HandleImportFBX(
	const TSharedPtr<FJsonObject>& Params)
{
	// ── Validate required params ──

	if (!Params->HasField(TEXT("source_file")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: source_file"));
	}

	if (!Params->HasField(TEXT("destination_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: destination_path"));
	}

	const FString SourceFile = Params->GetStringField(TEXT("source_file"));
	const FString DestinationPath = Params->GetStringField(TEXT("destination_path"));

	// Validate file exists
	if (!FPaths::FileExists(SourceFile))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Source file does not exist: %s"), *SourceFile));
	}

	// ── Read optional params with defaults ──

	const FString AssetName = Params->HasField(TEXT("asset_name"))
		? Params->GetStringField(TEXT("asset_name")) : TEXT("");
	const bool bCombineMeshes = !Params->HasField(TEXT("combine_meshes"))
		|| Params->GetBoolField(TEXT("combine_meshes"));
	const bool bComputeNormals = !Params->HasField(TEXT("compute_normals"))
		|| Params->GetBoolField(TEXT("compute_normals"));
	const bool bImportMaterials = !Params->HasField(TEXT("import_materials"))
		|| Params->GetBoolField(TEXT("import_materials"));
	const bool bImportTextures = !Params->HasField(TEXT("import_textures"))
		|| Params->GetBoolField(TEXT("import_textures"));
	const bool bAutoGenerateCollision = !Params->HasField(TEXT("auto_generate_collision"))
		|| Params->GetBoolField(TEXT("auto_generate_collision"));
	const double ScaleFactor = Params->HasField(TEXT("scale_factor"))
		? Params->GetNumberField(TEXT("scale_factor")) : 1.0;
	const bool bBuildNanite = Params->HasField(TEXT("build_nanite"))
		&& Params->GetBoolField(TEXT("build_nanite"));

	// ── Create import task ──

	UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
	ImportTask->AddToRoot(); // Prevent GC during import
	ImportTask->Filename = SourceFile;
	ImportTask->DestinationPath = DestinationPath;
	if (!AssetName.IsEmpty())
	{
		ImportTask->DestinationName = AssetName;
	}
	ImportTask->bReplaceExisting = true;
	ImportTask->bAutomated = true;
	ImportTask->bSave = true;

	// ── Create FBX factory ──

	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	ImportTask->Factory = FbxFactory;

	// ── Configure FBX import options ──

	UFbxImportUI* ImportUI = NewObject<UFbxImportUI>();
	ImportUI->MeshTypeToImport = FBXIT_StaticMesh;
	ImportUI->bImportMaterials = bImportMaterials;
	ImportUI->bImportTextures = bImportTextures;
	ImportUI->bImportAnimations = false;
	ImportUI->bImportAsSkeletal = false;
	ImportUI->bIsReimport = false;
	ImportUI->bOverrideFullName = !AssetName.IsEmpty();

	// ── Configure static mesh import data ──

	if (ImportUI->StaticMeshImportData)
	{
		UFbxStaticMeshImportData* MeshData = ImportUI->StaticMeshImportData;

		// Normal handling — fixes "no smoothing group" warnings
		MeshData->NormalImportMethod = bComputeNormals
			? FBXNIM_ComputeNormals
			: FBXNIM_ImportNormalsAndTangents;
		MeshData->NormalGenerationMethod = EFBXNormalGenerationMethod::MikkTSpace;
		MeshData->bComputeWeightedNormals = true;

		// Mesh combining — merges all sub-meshes into one
		MeshData->bCombineMeshes = bCombineMeshes;

		// Collision
		MeshData->bAutoGenerateCollision = bAutoGenerateCollision;

		// Nanite
		MeshData->bBuildNanite = bBuildNanite;

		// Scale
		MeshData->ImportUniformScale = static_cast<float>(ScaleFactor);

		// Quality defaults
		MeshData->bRemoveDegenerates = true;
		MeshData->bGenerateLightmapUVs = true;
	}

	// Assign options to task
	ImportTask->Options = ImportUI;

	// Also set on the factory itself (some code paths read from factory->ImportUI)
	FbxFactory->SetDetectImportTypeOnImport(false);
	FbxFactory->ImportUI = ImportUI;

	// ── Execute import ──

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.ImportAssetTasks({ImportTask});

	// ── Build response ──

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	const TArray<UObject*>& ImportedObjects = ImportTask->GetObjects();
	if (ImportedObjects.Num() > 0)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetNumberField(TEXT("imported_count"), ImportedObjects.Num());

		TArray<TSharedPtr<FJsonValue>> PathsArray;
		for (const UObject* Obj : ImportedObjects)
		{
			PathsArray.Add(MakeShareable(new FJsonValueString(Obj->GetPathName())));
		}
		Result->SetArrayField(TEXT("imported_paths"), PathsArray);

		// Report first asset name for convenience
		Result->SetStringField(TEXT("primary_asset"), ImportedObjects[0]->GetPathName());
	}
	else
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("message"), TEXT("Import completed but no assets were created. Check the source file and destination path."));
	}

	// Allow GC
	ImportTask->RemoveFromRoot();

	return Result;
}
