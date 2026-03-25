// EpicUnrealMCPRVTCommands.cpp — RVT management stub implementations
#include "Commands/EpicUnrealMCPRVTCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

TSharedPtr<FJsonObject> FEpicUnrealMCPRVTCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_rvt_info"))              return HandleGetRVTInfo(Params);
	else if (CommandType == TEXT("set_rvt_properties"))   return HandleSetRVTProperties(Params);
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown RVT command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPRVTCommands::HandleGetRVTInfo(const TSharedPtr<FJsonObject>& Params) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("get_rvt_info: Pending restoration")); }
TSharedPtr<FJsonObject> FEpicUnrealMCPRVTCommands::HandleSetRVTProperties(const TSharedPtr<FJsonObject>& Params) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("set_rvt_properties: Pending restoration")); }
