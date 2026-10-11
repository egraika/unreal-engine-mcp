#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for asset-management MCP commands.
 *
 * Commands:
 *   fixup_redirectors        — find ObjectRedirector assets under a content
 *                              path and call IAssetTools::FixupReferencers to
 *                              rewrite all references and (by default) delete
 *                              the stubs.
 *   delete_blueprint         — delete any UBlueprint asset (Actor/Pawn/HUD/
 *                              etc). Refuses widget blueprints by default;
 *                              callers should use delete_widget_blueprint for
 *                              those, or pass allow_widget=true to override.
 *   find_asset_referencers   — read-only: list assets that reference a given
 *                              asset, optionally transitively. Wraps
 *                              IAssetRegistry::GetReferencers.
 *   replace_asset_references — wraps ObjectTools::ConsolidateObjects to
 *                              redirect every reference of OldAsset to
 *                              NewAsset and (by default) delete OldAsset
 *                              afterwards. Leaves an ObjectRedirector behind
 *                              for any unloaded consumers — pair with
 *                              fixup_redirectors to clean those up.
 */
class UNREALMCP_API FEpicUnrealMCPAssetCommands
{
public:
	FEpicUnrealMCPAssetCommands();
	~FEpicUnrealMCPAssetCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleFixupRedirectors(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleFindAssetReferencers(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReplaceAssetReferences(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSearchAssetsFTS(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleWaitForAssetCompile(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleWaitForShaders(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetNavmeshBuildStatus(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSaveAssetChecked(const TSharedPtr<FJsonObject>& Params);
};
