#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for DataTable MCP commands.
 * Provides tools to read UDataTable assets and individual rows
 * via the MCP bridge.
 */
class UNREALMCP_API FEpicUnrealMCPDataTableCommands
{
public:
	FEpicUnrealMCPDataTableCommands();
	~FEpicUnrealMCPDataTableCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleReadDataTable(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadDataTableRow(const TSharedPtr<FJsonObject>& Params);
};
