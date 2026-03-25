#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for DataTable and CurveTable MCP commands.
 * Provides tools to read/update UDataTable and UCurveTable assets
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
	TSharedPtr<FJsonObject> HandleReadCurveTable(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleUpdateCurveTable(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateCurveTable(const TSharedPtr<FJsonObject>& Params);
};
