// EpicUnrealMCPMaterialCommands.h — Material analysis and inspection
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNREALMCP_API FEpicUnrealMCPMaterialCommands
{
public:
	FEpicUnrealMCPMaterialCommands() = default;
	~FEpicUnrealMCPMaterialCommands() = default;
	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);
private:
	TSharedPtr<FJsonObject> HandleGetMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialConnections(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialFunctionInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialStats(const TSharedPtr<FJsonObject>& Params);
};
