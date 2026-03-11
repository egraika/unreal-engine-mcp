#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for World/Level analysis MCP commands.
 * Provides tools to inspect world info, streaming levels, and per-level
 * actor breakdowns via the MCP bridge.
 */
class UNREALMCP_API FEpicUnrealMCPWorldCommands
{
public:
	FEpicUnrealMCPWorldCommands();
	~FEpicUnrealMCPWorldCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleGetWorldInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetLevelDetails(const TSharedPtr<FJsonObject>& Params);
};
