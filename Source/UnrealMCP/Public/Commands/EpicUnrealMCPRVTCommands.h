#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for RuntimeVirtualTexture MCP commands.
 * Provides tools to inspect and modify URuntimeVirtualTexture assets
 * via the MCP bridge.
 */
class UNREALMCP_API FEpicUnrealMCPRVTCommands
{
public:
	FEpicUnrealMCPRVTCommands();
	~FEpicUnrealMCPRVTCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Command handlers
	TSharedPtr<FJsonObject> HandleGetRVTInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetRVTProperties(const TSharedPtr<FJsonObject>& Params);

	// Helpers
	FString MaterialTypeToString(int32 EnumValue);
	int32 StringToMaterialType(const FString& Str);
};
