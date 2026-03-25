// EpicUnrealMCPMaterialCommands.cpp — Material analysis stub implementations
#include "Commands/EpicUnrealMCPMaterialCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_material_expressions"))      return HandleGetMaterialExpressions(Params);
	else if (CommandType == TEXT("get_material_connections")) return HandleGetMaterialConnections(Params);
	else if (CommandType == TEXT("get_material_function_info")) return HandleGetMaterialFunctionInfo(Params);
	else if (CommandType == TEXT("get_material_stats"))       return HandleGetMaterialStats(Params);
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown Material command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleGetMaterialExpressions(const TSharedPtr<FJsonObject>& Params) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("get_material_expressions: Pending restoration")); }
TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleGetMaterialConnections(const TSharedPtr<FJsonObject>& Params) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("get_material_connections: Pending restoration")); }
TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleGetMaterialFunctionInfo(const TSharedPtr<FJsonObject>& Params) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("get_material_function_info: Pending restoration")); }
TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleGetMaterialStats(const TSharedPtr<FJsonObject>& Params) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("get_material_stats: Pending restoration")); }
