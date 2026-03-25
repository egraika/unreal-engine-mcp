#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UMaterialExpression;

/**
 * Handler class for Material analysis MCP commands.
 * Provides tools to inspect base UMaterial expression graphs,
 * material output connections, UMaterialFunction internals,
 * and material stats/properties.
 */
class UNREALMCP_API FEpicUnrealMCPMaterialCommands
{
public:
	FEpicUnrealMCPMaterialCommands();
	~FEpicUnrealMCPMaterialCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Command handlers
	TSharedPtr<FJsonObject> HandleGetMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialConnections(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialFunctionInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialStats(const TSharedPtr<FJsonObject>& Params);

	// Helpers
	TSharedPtr<FJsonObject> SerializeExpression(UMaterialExpression* Expr, int32 Index, const TMap<UMaterialExpression*, int32>& IndexMap);
	void ExtractSpecializedProperties(UMaterialExpression* Expr, TSharedPtr<FJsonObject>& OutProps);
};
