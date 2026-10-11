#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UMaterial;
class UMaterialFunction;
class UMaterialExpression;

/**
 * Handler class for Material MCP commands.
 *
 * READ surface (inspection of base materials, functions, stats):
 *   - get_material_expressions / get_material_connections
 *   - get_material_function_info / get_material_stats
 *
 * WRITE surface (graph editing with pre-flight validation, FScopedTransaction,
 * and post-condition verification — see top-of-cpp for the contract):
 *   - material_create_expression / material_delete_expression
 *   - material_connect_expressions / material_disconnect_input
 *   - material_connect_property
 *   - material_set_expression_position / material_set_expression_property
 *   - material_recompile / material_update_function / material_save
 *   - material_batch_edit (atomic multi-op transaction)
 *
 * Every WRITE command targets either a UMaterial (material_path) or a
 * UMaterialFunction (function_path), never both. Helpers internally route
 * to the appropriate UMaterialEditingLibrary call.
 */
class UNREALMCP_API FEpicUnrealMCPMaterialCommands
{
public:
	FEpicUnrealMCPMaterialCommands();
	~FEpicUnrealMCPMaterialCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// --- READ handlers ---
	TSharedPtr<FJsonObject> HandleGetMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialConnections(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialFunctionInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialStats(const TSharedPtr<FJsonObject>& Params);

	// --- WRITE handlers ---
	TSharedPtr<FJsonObject> HandleCreateExpression(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteExpression(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleConnectExpressions(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleConnectProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDisconnectInput(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetExpressionPosition(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetExpressionProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRecompile(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleUpdateFunction(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSave(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleBatchEdit(const TSharedPtr<FJsonObject>& Params);

	// --- READ helpers ---
	TSharedPtr<FJsonObject> SerializeExpression(UMaterialExpression* Expr, int32 Index, const TMap<UMaterialExpression*, int32>& IndexMap);
	void ExtractSpecializedProperties(UMaterialExpression* Expr, TSharedPtr<FJsonObject>& OutProps);
};
