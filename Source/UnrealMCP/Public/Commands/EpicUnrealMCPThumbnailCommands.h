#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Thumbnail generation MCP commands.
 * Uses USceneCaptureComponent2D + UTextureRenderTarget2D for offscreen rendering
 * of static/skeletal meshes into transparent PNG icons.
 *
 * Relies on the current editor level's lighting (studio level recommended).
 * Mesh is spawned at origin, captured with ShowOnlyList for clean alpha.
 */
class UNREALMCP_API FEpicUnrealMCPThumbnailCommands
{
public:
	FEpicUnrealMCPThumbnailCommands();
	~FEpicUnrealMCPThumbnailCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Command handlers
	TSharedPtr<FJsonObject> HandleGenerateThumbnail(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGenerateThumbnails(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleScanAssetsForThumbnails(const TSharedPtr<FJsonObject>& Params);

	// Core rendering
	struct FThumbnailSettings
	{
		int32 Resolution = 256;
		bool bTransparent = true;
		FString SaveDirectory = TEXT("/Game/PRK/UI/Icons");
		float CameraFOV = 30.0f;
		float CameraPitch = -15.0f;
		float CameraYaw = 90.0f;
		bool bAmbientLightOnly = false;
		bool bExportPNG = false;
		FString ExportDiskPath;
	};

	FThumbnailSettings ParseSettings(const TSharedPtr<FJsonObject>& Params) const;

	bool RenderThumbnail(const FString& MeshPath, const FThumbnailSettings& Settings, FString& OutSavedPath);
	bool RenderThumbnailViaTGP(const FString& AssetPath, const FThumbnailSettings& Settings, FString& OutSavedPath);
	FString ResolveMeshPath(const FString& AssetPath) const;
	void ScanDirectoryForMeshes(const FString& DirectoryPath, bool bIncludeStatic, bool bIncludeSkeletal, TArray<FString>& OutPaths) const;

	// Console variable overrides for clean capture
	void DisablePostProcessFeatures();
	void RestorePostProcessFeatures();
	TMap<FString, FString> SavedCVarValues;
};
