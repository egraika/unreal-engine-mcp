#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UStateTree;
class UStateTreeEditorData;
class UStateTreeState;
struct FStateTreeEditorNode;
struct FStateTreeTransition;
struct FInstancedPropertyBag;
struct FPropertyBindingPath;

/**
 * Handler class for StateTree MCP commands (Phases 1-4).
 *
 * Phase 1 — asset lifecycle + state hierarchy scaffolding.
 * Phase 2 — tasks / conditions / considerations / transitions / global tasks / evaluators.
 * Phase 3 — root parameters + property bindings.
 * Phase 4 — analyze_state_tree (structural diagnostics).
 *
 * State addressing:
 *  - Accepts either an FGuid string or a "/"-delimited path like "Root/Combat/Attack".
 *  - Path segments match UStateTreeState::Name; first case-insensitive match wins.
 *
 * Node addressing:
 *  - Nodes (tasks, conditions, considerations, transitions, global items) are addressed by FGuid.
 *  - GUIDs are returned from every add_* call.
 */
class UNREALMCP_API FEpicUnrealMCPStateTreeCommands
{
public:
	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	/**
	 * Node category for routing between native C++ struct nodes and Blueprint subclasses.
	 * Public so the anonymous-namespace category-spec table in the .cpp can reference it.
	 */
	enum class EStateTreeNodeCategory : uint8
	{
		Task,
		Condition,
		Consideration,
		Evaluator,
	};

private:
	// --- Phase 1: Asset lifecycle ---
	TSharedPtr<FJsonObject> HandleCreateStateTree(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadStateTree(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListStateTrees(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSaveStateTree(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteStateTree(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRenameStateTree(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDuplicateStateTree(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCompileStateTree(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleValidateStateTree(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListStateTreeSchemas(const TSharedPtr<FJsonObject>& Params);

	// --- Phase 1: Hierarchy manipulation ---
	TSharedPtr<FJsonObject> HandleAddSubTree(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddStateTreeState(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveStateTreeState(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRenameStateTreeState(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleMoveStateTreeState(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetStateTreeStateProperty(const TSharedPtr<FJsonObject>& Params);

	// --- Phase 2: Nodes (tasks, conditions, considerations) ---
	TSharedPtr<FJsonObject> HandleAddStateTask(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddStateEnterCondition(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddStateConsideration(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddGlobalTask(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddEvaluator(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveStateTreeNode(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetStateTreeNodeProperty(const TSharedPtr<FJsonObject>& Params);

	// --- Phase 2: Transitions ---
	TSharedPtr<FJsonObject> HandleAddStateTreeTransition(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveStateTreeTransition(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetStateTreeTransitionProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddTransitionCondition(const TSharedPtr<FJsonObject>& Params);

	// --- Phase 3: Root parameters + property bindings ---
	TSharedPtr<FJsonObject> HandleAddRootParameter(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveRootParameter(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetRootParameterValue(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddPropertyBinding(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemovePropertyBinding(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListPropertyBindings(const TSharedPtr<FJsonObject>& Params);

	// --- Phase 4: Diagnostics ---
	TSharedPtr<FJsonObject> HandleAnalyzeStateTree(const TSharedPtr<FJsonObject>& Params);

	// --- Phase 5: Node class discovery ---
	TSharedPtr<FJsonObject> HandleListStateTreeNodeClasses(const TSharedPtr<FJsonObject>& Params);

	// --- Phase 5: Per-state parameters ---
	TSharedPtr<FJsonObject> HandleAddStateParameter(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveStateParameter(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetStateParameterValue(const TSharedPtr<FJsonObject>& Params);

	// --- Phase 5: Advanced bindings ---
	TSharedPtr<FJsonObject> HandleAddFunctionBinding(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddOutputBinding(const TSharedPtr<FJsonObject>& Params);

	// --- Phase 6: Schema introspection, linked states, parameter overrides ---
	TSharedPtr<FJsonObject> HandleListSchemaContextData(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetLinkedState(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetStateParameterOverride(const TSharedPtr<FJsonObject>& Params);

	// --- Helpers ---
	UStateTree* LoadStateTree(const FString& AssetPath, FString& OutError);
	UStateTreeEditorData* GetEditorData(UStateTree* StateTree, FString& OutError);

	/** Resolve an identifier that is either an FGuid string or a "/"-delimited state path. */
	UStateTreeState* ResolveState(UStateTreeEditorData* EditorData, const FString& Identifier, FString& OutError);

	/** Return the parent that owns the given state, or nullptr if it is a top-level subtree. */
	UStateTreeState* FindParentOf(UStateTreeEditorData* EditorData, const UStateTreeState* Target);

	/** Serialize a state (and recursively its children) to JSON. When bDetailed, includes full task/condition/consideration/transition arrays. */
	TSharedPtr<FJsonObject> StateToJson(const UStateTreeState* State, bool bDetailed = false) const;

	/**
	 * Apply post-edit bookkeeping after mutating a state's owning container
	 * (mark dirty, run ValidateStateTree, optional auto-compile).
	 */
	void FinalizeEdit(UStateTree* StateTree, bool bAutoCompile);

	/** Resolve a UScriptStruct by short name or "/Script/Module.Struct" path. Returns nullptr on failure. */
	UScriptStruct* ResolveScriptStruct(const FString& Name);

	/**
	 * Initialize an editor node from a path that may be a UScriptStruct (native) or a
	 * UClass derived from a Blueprint base (UStateTreeTaskBlueprintBase etc.).
	 * For BP classes, wraps into the appropriate FStateTreeBlueprint*Wrapper and
	 * allocates InstanceObject = NewObject<BpClass>(Owner).
	 */
	bool InitializeEditorNodeWithCategory(FStateTreeEditorNode& OutNode,
		EStateTreeNodeCategory Category,
		const FString& StructOrClassPath,
		const TSharedPtr<FJsonObject>& NodeProperties,
		const TSharedPtr<FJsonObject>& InstanceProperties,
		UStateTreeEditorData* Owner,
		FString& OutError);

	/** Obtain mutable reference to the private RootParameterPropertyBag via reflection. */
	FInstancedPropertyBag* GetMutableRootParameterBag(UStateTreeEditorData* EditorData, FString& OutError);

	/**
	 * Initialize an FStateTreeEditorNode of the given struct type, applying the provided
	 * properties object to the Node and/or Instance struct. Assigns a new FGuid ID.
	 * Returns true on success.
	 */
	bool InitializeEditorNode(FStateTreeEditorNode& OutNode,
		const FString& StructPath,
		const TSharedPtr<FJsonObject>& NodeProperties,
		const TSharedPtr<FJsonObject>& InstanceProperties,
		UStateTreeEditorData* Owner,
		FString& OutError);

	/** Find a node by GUID across tasks/conditions/considerations/transitions/globals/evaluators. Returns the owning container slot info. */
	struct FNodeLocation
	{
		FStateTreeEditorNode* Node = nullptr;
		UStateTreeState* OwnerState = nullptr;  // null for global/evaluator slots
		TArray<FStateTreeEditorNode>* Container = nullptr;
		int32 Index = INDEX_NONE;
		FString ContainerName;  // "Tasks" / "EnterConditions" / "Considerations" / "TransitionConditions" / "GlobalTasks" / "Evaluators"
		FStateTreeTransition* OwnerTransition = nullptr;  // set when inside a transition's conditions
	};
	bool FindNodeByGuid(UStateTreeEditorData* EditorData, const FGuid& NodeGuid, FNodeLocation& OutLocation);

	/** Serialize an FStateTreeEditorNode to JSON (ID, struct name, instance struct name, field dump). */
	TSharedPtr<FJsonObject> EditorNodeToJson(const FStateTreeEditorNode& Node) const;

	/** Serialize an FStateTreeTransition to JSON. */
	TSharedPtr<FJsonObject> TransitionToJson(const FStateTreeTransition& Transition) const;
};
