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
};
