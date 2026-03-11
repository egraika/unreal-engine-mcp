#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for MaterialParameterCollection MCP commands.
 * Provides tools to inspect and modify UMaterialParameterCollection assets
 * via the MCP bridge.
 */
class UNREALMCP_API FEpicUnrealMCPMPCCommands
{
public:
	FEpicUnrealMCPMPCCommands();
	~FEpicUnrealMCPMPCCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleGetMPCParameters(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMPCParameters(const TSharedPtr<FJsonObject>& Params);
};
