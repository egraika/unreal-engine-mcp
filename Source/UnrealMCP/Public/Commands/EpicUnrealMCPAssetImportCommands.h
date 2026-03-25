// EpicUnrealMCPAssetImportCommands.h
// MCP command handler for importing assets (FBX, textures) into the project

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Handles asset import commands via MCP.
 * Supports FBX import with configurable normals, materials, scale, and mesh combining.
 */
class UNREALMCP_API FEpicUnrealMCPAssetImportCommands
{
public:
	FEpicUnrealMCPAssetImportCommands() = default;
	~FEpicUnrealMCPAssetImportCommands() = default;

	/** Route command to appropriate handler */
	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	/**
	 * Import an FBX file as a static mesh.
	 *
	 * Params:
	 *   source_file (string, required) - Absolute path to FBX file on disk
	 *   destination_path (string, required) - Content path (e.g., "/Game/Meshes")
	 *   asset_name (string, optional) - Override asset name
	 *   combine_meshes (bool, default true) - Combine all mesh parts into one
	 *   compute_normals (bool, default true) - Compute normals (fixes missing smoothing groups)
	 *   import_materials (bool, default true) - Import/create materials from FBX
	 *   import_textures (bool, default true) - Import textures referenced by materials
	 *   auto_generate_collision (bool, default true) - Generate collision if none in FBX
	 *   scale_factor (float, default 1.0) - Uniform scale multiplier
	 *   build_nanite (bool, default false) - Enable Nanite on imported mesh
	 */
	TSharedPtr<FJsonObject> HandleImportFBX(const TSharedPtr<FJsonObject>& Params);
};
