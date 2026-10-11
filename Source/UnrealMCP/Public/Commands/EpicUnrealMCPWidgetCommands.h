#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UWidgetBlueprint;

/**
 * Reconciles UWidgetBlueprint::WidgetVariableNameToGuidMap with the current
 * WidgetTree contents. Mirrors FWidgetBlueprintCompilerContext::ValidateAndFixUpVariableGuids
 * (drops orphan GUID entries, mints new GUIDs for widgets without one) so the
 * compiler's ensures at WidgetBlueprintCompiler.cpp:794 / :828 don't fire when
 * external code (Python, generic compile_blueprint) mutated the tree without
 * going through UWidgetBlueprint::OnVariableAdded. Safe to call on null.
 * Must run BEFORE FKismetEditorUtilities::CompileBlueprint on any WBP.
 */
UNREALMCP_API void EpicUnrealMCP_ReconcileWidgetBlueprintGuids(UWidgetBlueprint* WBP);

/**
 * Handler class for Widget Blueprint MCP commands.
 * Provides tools to inspect UWidgetBlueprint assets and their widget trees
 * via the MCP bridge.
 */
class UNREALMCP_API FEpicUnrealMCPWidgetCommands
{
public:
	FEpicUnrealMCPWidgetCommands();
	~FEpicUnrealMCPWidgetCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleAnalyzeWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetWidgetDetails(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddWidgetChild(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetWidgetProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveWidget(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSaveWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRenameWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDuplicateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListWidgetBlueprints(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleMoveWidget(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReorderWidgetChildren(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetWidgetSlotProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetWidgetRoot(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCompileWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListRequiredBindWidgets(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetWidgetClassProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetWidgetObjectProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateCommonStyleAsset(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetNamedSlotContent(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleWrapWidget(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetWidgetIsVariable(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReplaceWidgetClass(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> SerializeWidget(class UWidget* Widget, int32 Depth);
};
