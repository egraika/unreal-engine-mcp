#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for DataAsset MCP commands.
 * Provides tools to read and list UDataAsset (and general UObject) assets
 * via the MCP bridge.
 */
class UNREALMCP_API FEpicUnrealMCPDataAssetCommands
{
public:
	FEpicUnrealMCPDataAssetCommands();
	~FEpicUnrealMCPDataAssetCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListDataAssets(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateDataAsset(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleUpdateDataAsset(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetDataAssetProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetDataAssetProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDescribeDataAssetClass(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListDataAssetClasses(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSaveDataAsset(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteDataAsset(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRenameDataAsset(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDuplicateDataAsset(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleArrayRemove(const TSharedPtr<FJsonObject>& Params);
};
