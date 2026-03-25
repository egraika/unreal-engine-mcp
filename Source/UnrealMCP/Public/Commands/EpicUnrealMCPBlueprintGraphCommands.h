// EpicUnrealMCPBlueprintGraphCommands.h — Blueprint node/variable/function graph editing
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNREALMCP_API FEpicUnrealMCPBlueprintGraphCommands
{
public:
	FEpicUnrealMCPBlueprintGraphCommands() = default;
	~FEpicUnrealMCPBlueprintGraphCommands() = default;
	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);
private:
	TSharedPtr<FJsonObject> HandleAddBlueprintNode(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleConnectNodes(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteNode(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetNodeProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateVariable(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetBlueprintVariableProperties(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateFunction(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddFunctionInput(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddFunctionOutput(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteFunction(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRenameFunction(const TSharedPtr<FJsonObject>& Params);
};
