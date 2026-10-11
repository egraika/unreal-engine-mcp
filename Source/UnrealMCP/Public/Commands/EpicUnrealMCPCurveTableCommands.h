#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for CurveTable MCP commands.
 * Provides tools to list, read, create, update, and delete rows in
 * UCurveTable assets (both RichCurves and SimpleCurves modes).
 */
class UNREALMCP_API FEpicUnrealMCPCurveTableCommands
{
public:
	FEpicUnrealMCPCurveTableCommands();
	~FEpicUnrealMCPCurveTableCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleListCurveTables(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadCurveTable(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadCurveTableRow(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateCurveTable(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleUpdateCurveTableRow(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteCurveTableRow(const TSharedPtr<FJsonObject>& Params);
};
