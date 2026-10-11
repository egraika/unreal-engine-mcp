#include "Commands/EpicUnrealMCPStateTreeCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeFactory.h"
#include "StateTreeSchema.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeEditorNode.h"
#include "StateTreeNodeBase.h"
#include "StateTreeNodeClassCache.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeTypes.h"

#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"

#include "StructUtils/PropertyBag.h"
#include "StructUtils/InstancedStruct.h"
#include "PropertyBindingPath.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "JsonObjectConverter.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

// Forward declarations for file-scope helpers used before their definition.
static bool ApplyJsonToBag(FInstancedPropertyBag& Bag, const FName PropName,
	const TSharedPtr<FJsonValue>& Value, FString& OutError);
static TSharedPtr<FJsonObject> AddPropertyToBag(FInstancedPropertyBag& Bag,
	const FString& Name, const FString& Type, const FString& ValueType, FString& OutError);

// ============================================================================
// Command Dispatcher
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleCommand(
	const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	// Lifecycle
	if (CommandType == TEXT("create_state_tree")) { return HandleCreateStateTree(Params); }
	if (CommandType == TEXT("read_state_tree")) { return HandleReadStateTree(Params); }
	if (CommandType == TEXT("list_state_trees")) { return HandleListStateTrees(Params); }
	if (CommandType == TEXT("save_state_tree")) { return HandleSaveStateTree(Params); }
	if (CommandType == TEXT("delete_state_tree")) { return HandleDeleteStateTree(Params); }
	if (CommandType == TEXT("rename_state_tree")) { return HandleRenameStateTree(Params); }
	if (CommandType == TEXT("duplicate_state_tree")) { return HandleDuplicateStateTree(Params); }
	if (CommandType == TEXT("compile_state_tree")) { return HandleCompileStateTree(Params); }
	if (CommandType == TEXT("validate_state_tree")) { return HandleValidateStateTree(Params); }
	if (CommandType == TEXT("list_state_tree_schemas")) { return HandleListStateTreeSchemas(Params); }

	// Hierarchy
	if (CommandType == TEXT("add_subtree")) { return HandleAddSubTree(Params); }
	if (CommandType == TEXT("add_state_tree_state")) { return HandleAddStateTreeState(Params); }
	if (CommandType == TEXT("remove_state_tree_state")) { return HandleRemoveStateTreeState(Params); }
	if (CommandType == TEXT("rename_state_tree_state")) { return HandleRenameStateTreeState(Params); }
	if (CommandType == TEXT("move_state_tree_state")) { return HandleMoveStateTreeState(Params); }
	if (CommandType == TEXT("set_state_tree_state_property")) { return HandleSetStateTreeStateProperty(Params); }

	// Phase 2: Nodes
	if (CommandType == TEXT("add_state_task")) { return HandleAddStateTask(Params); }
	if (CommandType == TEXT("add_state_enter_condition")) { return HandleAddStateEnterCondition(Params); }
	if (CommandType == TEXT("add_state_consideration")) { return HandleAddStateConsideration(Params); }
	if (CommandType == TEXT("add_global_task")) { return HandleAddGlobalTask(Params); }
	if (CommandType == TEXT("add_evaluator")) { return HandleAddEvaluator(Params); }
	if (CommandType == TEXT("remove_state_tree_node")) { return HandleRemoveStateTreeNode(Params); }
	if (CommandType == TEXT("set_state_tree_node_property")) { return HandleSetStateTreeNodeProperty(Params); }

	// Phase 2: Transitions
	if (CommandType == TEXT("add_state_tree_transition")) { return HandleAddStateTreeTransition(Params); }
	if (CommandType == TEXT("remove_state_tree_transition")) { return HandleRemoveStateTreeTransition(Params); }
	if (CommandType == TEXT("set_state_tree_transition_property")) { return HandleSetStateTreeTransitionProperty(Params); }
	if (CommandType == TEXT("add_transition_condition")) { return HandleAddTransitionCondition(Params); }

	// Phase 3: Parameters + Bindings
	if (CommandType == TEXT("add_root_parameter")) { return HandleAddRootParameter(Params); }
	if (CommandType == TEXT("remove_root_parameter")) { return HandleRemoveRootParameter(Params); }
	if (CommandType == TEXT("set_root_parameter_value")) { return HandleSetRootParameterValue(Params); }
	if (CommandType == TEXT("add_property_binding")) { return HandleAddPropertyBinding(Params); }
	if (CommandType == TEXT("remove_property_binding")) { return HandleRemovePropertyBinding(Params); }
	if (CommandType == TEXT("list_property_bindings")) { return HandleListPropertyBindings(Params); }

	// Phase 4: Analysis
	if (CommandType == TEXT("analyze_state_tree")) { return HandleAnalyzeStateTree(Params); }

	// Phase 5: Node class discovery + state parameters + advanced bindings
	if (CommandType == TEXT("list_state_tree_node_classes")) { return HandleListStateTreeNodeClasses(Params); }
	if (CommandType == TEXT("add_state_parameter")) { return HandleAddStateParameter(Params); }
	if (CommandType == TEXT("remove_state_parameter")) { return HandleRemoveStateParameter(Params); }
	if (CommandType == TEXT("set_state_parameter_value")) { return HandleSetStateParameterValue(Params); }
	if (CommandType == TEXT("add_function_binding")) { return HandleAddFunctionBinding(Params); }
	if (CommandType == TEXT("add_output_binding")) { return HandleAddOutputBinding(Params); }

	// Phase 6: schema introspection, linked states, parameter overrides
	if (CommandType == TEXT("list_schema_context_data")) { return HandleListSchemaContextData(Params); }
	if (CommandType == TEXT("set_linked_state")) { return HandleSetLinkedState(Params); }
	if (CommandType == TEXT("set_state_parameter_override")) { return HandleSetStateParameterOverride(Params); }

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown StateTree command: %s"), *CommandType));
}

// ============================================================================
// Helpers
// ============================================================================

UStateTree* FEpicUnrealMCPStateTreeCommands::LoadStateTree(const FString& AssetPath, FString& OutError)
{
	if (AssetPath.IsEmpty())
	{
		OutError = TEXT("asset_path is empty");
		return nullptr;
	}
	if (!FEpicUnrealMCPCommonUtils::AssetExists(AssetPath))
	{
		OutError = FString::Printf(TEXT("Asset does not exist: %s"), *AssetPath);
		return nullptr;
	}
	UObject* Loaded = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UStateTree* StateTree = Cast<UStateTree>(Loaded);
	if (!StateTree)
	{
		OutError = FString::Printf(TEXT("Asset is not a UStateTree: %s"), *AssetPath);
		return nullptr;
	}
	return StateTree;
}

UStateTreeEditorData* FEpicUnrealMCPStateTreeCommands::GetEditorData(UStateTree* StateTree, FString& OutError)
{
	if (!StateTree)
	{
		OutError = TEXT("StateTree is null");
		return nullptr;
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (!EditorData)
	{
		OutError = FString::Printf(TEXT("StateTree has no EditorData: %s"), *StateTree->GetName());
		return nullptr;
	}
	return EditorData;
}

static UStateTreeState* FindStateInSubtree(UStateTreeState* Root, const FGuid& Id)
{
	if (!Root) { return nullptr; }
	if (Root->ID == Id) { return Root; }
	for (const TObjectPtr<UStateTreeState>& Child : Root->Children)
	{
		if (UStateTreeState* Found = FindStateInSubtree(Child, Id))
		{
			return Found;
		}
	}
	return nullptr;
}

static UStateTreeState* FindStateByPathSegments(TArrayView<const TObjectPtr<UStateTreeState>> States,
	TArrayView<const FString> Segments, int32 SegmentIdx)
{
	if (SegmentIdx >= Segments.Num()) { return nullptr; }
	const FString& Target = Segments[SegmentIdx];
	for (const TObjectPtr<UStateTreeState>& State : States)
	{
		if (!State) { continue; }
		if (State->Name.ToString().Equals(Target, ESearchCase::IgnoreCase))
		{
			if (SegmentIdx + 1 == Segments.Num())
			{
				return State;
			}
			if (UStateTreeState* Child = FindStateByPathSegments(State->Children, Segments, SegmentIdx + 1))
			{
				return Child;
			}
		}
	}
	return nullptr;
}

UStateTreeState* FEpicUnrealMCPStateTreeCommands::ResolveState(
	UStateTreeEditorData* EditorData, const FString& Identifier, FString& OutError)
{
	if (!EditorData)
	{
		OutError = TEXT("EditorData is null");
		return nullptr;
	}
	if (Identifier.IsEmpty())
	{
		OutError = TEXT("State identifier is empty");
		return nullptr;
	}

	// Try GUID first
	FGuid Guid;
	if (FGuid::Parse(Identifier, Guid))
	{
		for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
		{
			if (UStateTreeState* Found = FindStateInSubtree(SubTree, Guid))
			{
				return Found;
			}
		}
		OutError = FString::Printf(TEXT("No state with GUID %s"), *Identifier);
		return nullptr;
	}

	// Path form: "Root/Child/Grandchild"
	TArray<FString> Segments;
	Identifier.ParseIntoArray(Segments, TEXT("/"), true);
	if (Segments.Num() == 0)
	{
		OutError = TEXT("Invalid state path");
		return nullptr;
	}
	UStateTreeState* Result = FindStateByPathSegments(EditorData->SubTrees, Segments, 0);
	if (!Result)
	{
		OutError = FString::Printf(TEXT("No state matching path %s"), *Identifier);
	}
	return Result;
}

UStateTreeState* FEpicUnrealMCPStateTreeCommands::FindParentOf(
	UStateTreeEditorData* EditorData, const UStateTreeState* Target)
{
	if (!EditorData || !Target) { return nullptr; }
	// Top-level subtree has nullptr parent.
	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
	{
		if (SubTree == Target) { return nullptr; }
	}
	return Target->Parent;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::StateToJson(const UStateTreeState* State, bool bDetailed) const
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	if (!State) { return Obj; }

	Obj->SetStringField(TEXT("name"), State->Name.ToString());
	Obj->SetStringField(TEXT("id"), State->ID.ToString(EGuidFormats::DigitsWithHyphens));
	Obj->SetStringField(TEXT("type"), UEnum::GetDisplayValueAsText(State->Type).ToString());
	Obj->SetStringField(TEXT("selection_behavior"), UEnum::GetDisplayValueAsText(State->SelectionBehavior).ToString());
	Obj->SetBoolField(TEXT("enabled"), State->bEnabled);
	if (!State->Description.IsEmpty())
	{
		Obj->SetStringField(TEXT("description"), State->Description);
	}
	if (State->Tag.IsValid())
	{
		Obj->SetStringField(TEXT("tag"), State->Tag.ToString());
	}
	Obj->SetNumberField(TEXT("weight"), State->Weight);
	Obj->SetNumberField(TEXT("task_count"), State->Tasks.Num());
	Obj->SetNumberField(TEXT("enter_condition_count"), State->EnterConditions.Num());
	Obj->SetNumberField(TEXT("consideration_count"), State->Considerations.Num());
	Obj->SetNumberField(TEXT("transition_count"), State->Transitions.Num());

	if (bDetailed)
	{
		auto DumpNodes = [this](const TArray<FStateTreeEditorNode>& Nodes)
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const FStateTreeEditorNode& N : Nodes)
			{
				Out.Add(MakeShareable(new FJsonValueObject(EditorNodeToJson(N))));
			}
			return Out;
		};
		Obj->SetArrayField(TEXT("tasks"), DumpNodes(State->Tasks));
		Obj->SetArrayField(TEXT("enter_conditions"), DumpNodes(State->EnterConditions));
		Obj->SetArrayField(TEXT("considerations"), DumpNodes(State->Considerations));

		TArray<TSharedPtr<FJsonValue>> TransitionArr;
		for (const FStateTreeTransition& T : State->Transitions)
		{
			TransitionArr.Add(MakeShareable(new FJsonValueObject(TransitionToJson(T))));
		}
		Obj->SetArrayField(TEXT("transitions"), TransitionArr);

		if (State->Parameters.Parameters.GetNumPropertiesInBag() > 0)
		{
			Obj->SetStringField(TEXT("parameters_id"), State->Parameters.ID.ToString(EGuidFormats::DigitsWithHyphens));
			Obj->SetNumberField(TEXT("parameter_count"), State->Parameters.Parameters.GetNumPropertiesInBag());
		}
	}

	if (State->Children.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ChildArray;
		for (const TObjectPtr<UStateTreeState>& Child : State->Children)
		{
			if (Child)
			{
				ChildArray.Add(MakeShareable(new FJsonValueObject(StateToJson(Child, bDetailed))));
			}
		}
		Obj->SetArrayField(TEXT("children"), ChildArray);
	}
	return Obj;
}

void FEpicUnrealMCPStateTreeCommands::FinalizeEdit(UStateTree* StateTree, bool bAutoCompile)
{
	if (!StateTree) { return; }
	if (UPackage* Package = StateTree->GetOutermost())
	{
		Package->MarkPackageDirty();
	}
	UStateTreeEditingSubsystem::ValidateStateTree(StateTree);

	if (bAutoCompile)
	{
		FStateTreeCompilerLog Log;
		UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log);
	}
}

// ============================================================================
// create_state_tree
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleCreateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: name"));
	}

	const FString AssetName = Params->GetStringField(TEXT("name"));
	FString PackagePath = Params->HasField(TEXT("path"))
		? Params->GetStringField(TEXT("path"))
		: TEXT("/Game/StateTrees/");
	if (!PackagePath.EndsWith(TEXT("/"))) { PackagePath += TEXT("/"); }

	const FString FullPath = PackagePath + AssetName;
	if (FEpicUnrealMCPCommonUtils::AssetExists(FullPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset already exists: %s"), *FullPath));
	}

	// Schema class. Default to the AI Component schema if available.
	FString SchemaClassName = Params->HasField(TEXT("schema_class"))
		? Params->GetStringField(TEXT("schema_class"))
		: TEXT("StateTreeComponentSchema");

	UClass* SchemaClass = FEpicUnrealMCPCommonUtils::ResolveClass(SchemaClassName);
	if (!SchemaClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Could not resolve schema class: %s"), *SchemaClassName));
	}
	if (!SchemaClass->IsChildOf(UStateTreeSchema::StaticClass()))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Class '%s' is not a UStateTreeSchema subclass"), *SchemaClass->GetName()));
	}
	if (SchemaClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Schema class '%s' is abstract"), *SchemaClass->GetName()));
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
	Factory->SetSchemaClass(SchemaClass);

	UObject* NewAssetRaw = Factory->FactoryCreateNew(
		UStateTree::StaticClass(),
		Package,
		*AssetName,
		RF_Public | RF_Standalone | RF_Transactional,
		nullptr,
		GWarn);

	UStateTree* NewStateTree = Cast<UStateTree>(NewAssetRaw);
	if (!NewStateTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("StateTreeFactory failed to create asset (check schema compile)"));
	}

	FAssetRegistryModule::AssetCreated(NewStateTree);
	Package->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), FullPath);
	Result->SetStringField(TEXT("asset_name"), AssetName);
	Result->SetStringField(TEXT("schema_class"), SchemaClass->GetName());
	if (UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(NewStateTree->EditorData))
	{
		Result->SetNumberField(TEXT("subtree_count"), EditorData->SubTrees.Num());
		if (EditorData->SubTrees.Num() > 0 && EditorData->SubTrees[0])
		{
			Result->SetStringField(TEXT("root_state_id"),
				EditorData->SubTrees[0]->ID.ToString(EGuidFormats::DigitsWithHyphens));
		}
	}
	return Result;
}

// ============================================================================
// read_state_tree
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleReadStateTree(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString Err;
	UStateTree* StateTree = LoadStateTree(AssetPath, Err);
	if (!StateTree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	UStateTreeEditorData* EditorData = GetEditorData(StateTree, Err);
	if (!EditorData) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), StateTree->GetName());
	Result->SetStringField(TEXT("schema_class"),
		EditorData->Schema ? EditorData->Schema->GetClass()->GetName() : TEXT(""));
	Result->SetNumberField(TEXT("last_compiled_hash"), StateTree->LastCompiledEditorDataHash);

	const bool bDetailed = Params->HasField(TEXT("detailed")) && Params->GetBoolField(TEXT("detailed"));

	TArray<TSharedPtr<FJsonValue>> SubTreesJson;
	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
	{
		if (SubTree)
		{
			SubTreesJson.Add(MakeShareable(new FJsonValueObject(StateToJson(SubTree, bDetailed))));
		}
	}
	Result->SetArrayField(TEXT("subtrees"), SubTreesJson);

	Result->SetNumberField(TEXT("global_task_count"), EditorData->GlobalTasks.Num());
	Result->SetNumberField(TEXT("evaluator_count"), EditorData->Evaluators.Num());
	Result->SetStringField(TEXT("root_parameters_id"),
		EditorData->GetRootParametersGuid().ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetNumberField(TEXT("root_parameter_count"),
		EditorData->GetRootParametersPropertyBag().GetNumPropertiesInBag());

	if (bDetailed)
	{
		TArray<TSharedPtr<FJsonValue>> GT;
		for (const FStateTreeEditorNode& N : EditorData->GlobalTasks) { GT.Add(MakeShareable(new FJsonValueObject(EditorNodeToJson(N)))); }
		Result->SetArrayField(TEXT("global_tasks"), GT);
		TArray<TSharedPtr<FJsonValue>> Ev;
		for (const FStateTreeEditorNode& N : EditorData->Evaluators) { Ev.Add(MakeShareable(new FJsonValueObject(EditorNodeToJson(N)))); }
		Result->SetArrayField(TEXT("evaluators"), Ev);
	}
	return Result;
}

// ============================================================================
// list_state_trees
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleListStateTrees(const TSharedPtr<FJsonObject>& Params)
{
	const FString Directory = Params->HasField(TEXT("directory"))
		? Params->GetStringField(TEXT("directory"))
		: TEXT("/Game/");
	const int32 Limit = Params->HasField(TEXT("limit"))
		? static_cast<int32>(Params->GetNumberField(TEXT("limit")))
		: 200;

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Directory));
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UStateTree::StaticClass()->GetClassPathName());

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	const int32 Count = FMath::Min(Assets.Num(), Limit);
	TArray<TSharedPtr<FJsonValue>> AssetsJson;
	for (int32 i = 0; i < Count; ++i)
	{
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("asset_name"), Assets[i].AssetName.ToString());
		Entry->SetStringField(TEXT("asset_path"), Assets[i].PackageName.ToString());
		AssetsJson.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetNumberField(TEXT("total_found"), Assets.Num());
	Result->SetNumberField(TEXT("returned"), Count);
	Result->SetArrayField(TEXT("state_trees"), AssetsJson);
	return Result;
}

// ============================================================================
// save / delete / rename / duplicate
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleSaveStateTree(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const bool bOnlyIfDirty = !Params->HasField(TEXT("only_if_dirty"))
		|| Params->GetBoolField(TEXT("only_if_dirty"));
	const bool bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, bOnlyIfDirty);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bSaved);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleDeleteStateTree(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	if (!FEpicUnrealMCPCommonUtils::AssetExists(AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset does not exist: %s"), *AssetPath));
	}
	const bool bDeleted = UEditorAssetLibrary::DeleteAsset(AssetPath);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bDeleted);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetBoolField(TEXT("deleted"), bDeleted);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleRenameStateTree(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("source_path")) || !Params->HasField(TEXT("destination_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): source_path, destination_path"));
	}
	const FString SourcePath = Params->GetStringField(TEXT("source_path"));
	const FString DestPath = Params->GetStringField(TEXT("destination_path"));

	if (!FEpicUnrealMCPCommonUtils::AssetExists(SourcePath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Source asset does not exist: %s"), *SourcePath));
	}
	if (FEpicUnrealMCPCommonUtils::AssetExists(DestPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Destination already exists: %s"), *DestPath));
	}

	const bool bRenamed = UEditorAssetLibrary::RenameAsset(SourcePath, DestPath);
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bRenamed);
	Result->SetStringField(TEXT("source_path"), SourcePath);
	Result->SetStringField(TEXT("destination_path"), DestPath);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleDuplicateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("source_path")) || !Params->HasField(TEXT("destination_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): source_path, destination_path"));
	}
	const FString SourcePath = Params->GetStringField(TEXT("source_path"));
	const FString DestPath = Params->GetStringField(TEXT("destination_path"));

	if (!FEpicUnrealMCPCommonUtils::AssetExists(SourcePath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Source asset does not exist: %s"), *SourcePath));
	}
	if (FEpicUnrealMCPCommonUtils::AssetExists(DestPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Destination already exists: %s"), *DestPath));
	}

	UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Duplicated != nullptr);
	Result->SetStringField(TEXT("source_path"), SourcePath);
	Result->SetStringField(TEXT("destination_path"), DestPath);
	return Result;
}

// ============================================================================
// compile / validate
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleCompileStateTree(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString Err;
	UStateTree* StateTree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!StateTree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	FStateTreeCompilerLog Log;
	const bool bOk = UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bOk);
	Result->SetStringField(TEXT("asset_path"), StateTree->GetPathName());
	Result->SetNumberField(TEXT("last_compiled_hash"), StateTree->LastCompiledEditorDataHash);
	if (!bOk)
	{
		Result->SetStringField(TEXT("error"), TEXT("Compile reported failure; check Message Log"));
	}
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleValidateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString Err;
	UStateTree* StateTree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!StateTree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	UStateTreeEditingSubsystem::ValidateStateTree(StateTree);
	const uint32 Hash = UStateTreeEditingSubsystem::CalculateStateTreeHash(StateTree);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), StateTree->GetPathName());
	Result->SetNumberField(TEXT("editor_data_hash"), Hash);
	Result->SetBoolField(TEXT("is_compile_up_to_date"), Hash == StateTree->LastCompiledEditorDataHash);
	return Result;
}

// ============================================================================
// list_state_tree_schemas — runtime discovery of UStateTreeSchema subclasses
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleListStateTreeSchemas(const TSharedPtr<FJsonObject>& /*Params*/)
{
	TArray<TSharedPtr<FJsonValue>> SchemasJson;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Cls = *It;
		if (!Cls || !Cls->IsChildOf(UStateTreeSchema::StaticClass())) { continue; }
		if (Cls == UStateTreeSchema::StaticClass()) { continue; }
		if (Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown)) { continue; }

		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("name"), Cls->GetName());
		Entry->SetStringField(TEXT("path"), Cls->GetPathName());
		Entry->SetBoolField(TEXT("common"), Cls->HasMetaData(TEXT("CommonSchema")));
		SchemasJson.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), SchemasJson.Num());
	Result->SetArrayField(TEXT("schemas"), SchemasJson);
	return Result;
}

// ============================================================================
// add_subtree
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddSubTree(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, name"));
	}
	FString Err;
	UStateTree* StateTree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!StateTree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* EditorData = GetEditorData(StateTree, Err);
	if (!EditorData) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FName SubTreeName(*Params->GetStringField(TEXT("name")));
	UStateTreeState& NewSub = EditorData->AddSubTree(SubTreeName);
	NewSub.Type = EStateTreeStateType::Subtree;

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile"))
		&& Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(StateTree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("subtree_id"), NewSub.ID.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("name"), NewSub.Name.ToString());
	return Result;
}

// ============================================================================
// add_state_tree_state
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddStateTreeState(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("parent")) || !Params->HasField(TEXT("name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, parent, name"));
	}
	FString Err;
	UStateTree* StateTree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!StateTree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* EditorData = GetEditorData(StateTree, Err);
	if (!EditorData) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	UStateTreeState* Parent = ResolveState(EditorData, Params->GetStringField(TEXT("parent")), Err);
	if (!Parent) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	EStateTreeStateType StateType = EStateTreeStateType::State;
	if (Params->HasField(TEXT("state_type")))
	{
		const FString TypeStr = Params->GetStringField(TEXT("state_type"));
		const UEnum* Enum = StaticEnum<EStateTreeStateType>();
		const int64 Value = Enum ? Enum->GetValueByNameString(TypeStr) : INDEX_NONE;
		if (Value == INDEX_NONE)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Invalid state_type: %s"), *TypeStr));
		}
		StateType = static_cast<EStateTreeStateType>(Value);
	}

	const FName NewName(*Params->GetStringField(TEXT("name")));
	UStateTreeState& NewChild = Parent->AddChildState(NewName, StateType);

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile"))
		&& Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(StateTree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state_id"), NewChild.ID.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("name"), NewChild.Name.ToString());
	Result->SetStringField(TEXT("parent_id"), Parent->ID.ToString(EGuidFormats::DigitsWithHyphens));
	return Result;
}

// ============================================================================
// remove_state_tree_state
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleRemoveStateTreeState(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, state"));
	}
	FString Err;
	UStateTree* StateTree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!StateTree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* EditorData = GetEditorData(StateTree, Err);
	if (!EditorData) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	UStateTreeState* Target = ResolveState(EditorData, Params->GetStringField(TEXT("state")), Err);
	if (!Target) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	// If it's a top-level subtree, pull from SubTrees; otherwise from Parent->Children.
	int32 RemovedIdx = INDEX_NONE;
	if (Target->Parent == nullptr)
	{
		RemovedIdx = EditorData->SubTrees.IndexOfByKey(Target);
		if (RemovedIdx != INDEX_NONE) { EditorData->SubTrees.RemoveAt(RemovedIdx); }
	}
	else
	{
		RemovedIdx = Target->Parent->Children.IndexOfByKey(Target);
		if (RemovedIdx != INDEX_NONE) { Target->Parent->Children.RemoveAt(RemovedIdx); }
	}
	if (RemovedIdx == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Failed to locate state in its parent container"));
	}

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile"))
		&& Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(StateTree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_state_id"), Target->ID.ToString(EGuidFormats::DigitsWithHyphens));
	return Result;
}

// ============================================================================
// rename_state_tree_state
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleRenameStateTreeState(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state")) || !Params->HasField(TEXT("new_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, state, new_name"));
	}
	FString Err;
	UStateTree* StateTree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!StateTree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* EditorData = GetEditorData(StateTree, Err);
	if (!EditorData) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	UStateTreeState* Target = ResolveState(EditorData, Params->GetStringField(TEXT("state")), Err);
	if (!Target) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	Target->Modify();
	Target->Name = FName(*Params->GetStringField(TEXT("new_name")));

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile"))
		&& Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(StateTree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state_id"), Target->ID.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("new_name"), Target->Name.ToString());
	return Result;
}

// ============================================================================
// move_state_tree_state
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleMoveStateTreeState(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state")) || !Params->HasField(TEXT("new_parent")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, state, new_parent"));
	}
	FString Err;
	UStateTree* StateTree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!StateTree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* EditorData = GetEditorData(StateTree, Err);
	if (!EditorData) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	UStateTreeState* Target = ResolveState(EditorData, Params->GetStringField(TEXT("state")), Err);
	if (!Target) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* NewParent = ResolveState(EditorData, Params->GetStringField(TEXT("new_parent")), Err);
	if (!NewParent) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	if (Target == NewParent)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot move a state into itself"));
	}
	// Reject cycle: NewParent must not be a descendant of Target.
	for (UStateTreeState* Cursor = NewParent; Cursor; Cursor = Cursor->Parent)
	{
		if (Cursor == Target)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				TEXT("Cannot move a state into one of its descendants"));
		}
	}

	// Detach from current container.
	if (Target->Parent == nullptr)
	{
		EditorData->SubTrees.Remove(Target);
	}
	else
	{
		Target->Parent->Children.Remove(Target);
	}

	// Attach.
	int32 InsertIndex = NewParent->Children.Num();
	if (Params->HasField(TEXT("index")))
	{
		InsertIndex = FMath::Clamp(static_cast<int32>(Params->GetNumberField(TEXT("index"))),
			0, NewParent->Children.Num());
	}
	Target->Parent = NewParent;
	NewParent->Children.Insert(Target, InsertIndex);

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile"))
		&& Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(StateTree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state_id"), Target->ID.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("new_parent_id"), NewParent->ID.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetNumberField(TEXT("index"), InsertIndex);
	return Result;
}

// ============================================================================
// set_state_tree_state_property
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleSetStateTreeStateProperty(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state")) || !Params->HasField(TEXT("properties")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, state, properties"));
	}
	FString Err;
	UStateTree* StateTree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!StateTree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* EditorData = GetEditorData(StateTree, Err);
	if (!EditorData) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	UStateTreeState* Target = ResolveState(EditorData, Params->GetStringField(TEXT("state")), Err);
	if (!Target) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const TSharedPtr<FJsonObject> Props = Params->GetObjectField(TEXT("properties"));
	if (!Props.IsValid())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("properties must be a JSON object"));
	}

	Target->Modify();
	TArray<FString> Errors;
	int32 SuccessCount = 0;
	for (const auto& Pair : Props->Values)
	{
		FProperty* Prop = Target->GetClass()->FindPropertyByName(*Pair.Key);
		// UStateTreeState overrides PreEditChange(FEditPropertyChain&), which hides
		// the base UObject::PreEditChange(FProperty*). Qualify to call the base.
		if (Prop) { Target->UObject::PreEditChange(Prop); }

		FString PropErr;
		const bool bOk = FEpicUnrealMCPCommonUtils::SetObjectProperty(Target, FString(*Pair.Key), Pair.Value, PropErr);

		if (Prop)
		{
			FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
			Target->PostEditChangeProperty(Evt);
		}

		if (bOk)
		{
			++SuccessCount;
			// Surface partial-failure warnings (e.g., a TArray<FStruct> where one
			// element's TSoftObjectPtr or unregistered FGameplayTag failed) so they
			// don't get silently dropped on the success path.
			if (!PropErr.IsEmpty())
			{
				Errors.Add(FString::Printf(TEXT("%s [partial]: %s"), *Pair.Key, *PropErr));
			}
		}
		else { Errors.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *PropErr)); }
	}

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile"))
		&& Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(StateTree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Errors.Num() == 0);
	Result->SetNumberField(TEXT("updated"), SuccessCount);
	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : Errors) { ErrArr.Add(MakeShareable(new FJsonValueString(E))); }
		Result->SetArrayField(TEXT("errors"), ErrArr);
	}
	return Result;
}

// ============================================================================
// Phase 2+ helpers
// ============================================================================

UScriptStruct* FEpicUnrealMCPStateTreeCommands::ResolveScriptStruct(const FString& Name)
{
	if (Name.IsEmpty()) { return nullptr; }

	// Fully qualified path: "/Script/ModuleName.StructName"
	if (Name.StartsWith(TEXT("/Script/")))
	{
		if (UScriptStruct* Found = FindObject<UScriptStruct>(nullptr, *Name))
		{
			return Found;
		}
	}

	if (UScriptStruct* Found = FindFirstObject<UScriptStruct>(*Name, EFindFirstObjectOptions::ExactClass))
	{
		return Found;
	}
	// MCP users often drop the F prefix.
	if (!Name.StartsWith(TEXT("F")))
	{
		const FString Prefixed = FString(TEXT("F")) + Name;
		if (UScriptStruct* Found = FindFirstObject<UScriptStruct>(*Prefixed, EFindFirstObjectOptions::ExactClass))
		{
			return Found;
		}
	}
	return nullptr;
}

FInstancedPropertyBag* FEpicUnrealMCPStateTreeCommands::GetMutableRootParameterBag(
	UStateTreeEditorData* EditorData, FString& OutError)
{
	if (!EditorData) { OutError = TEXT("EditorData is null"); return nullptr; }
	FProperty* Prop = UStateTreeEditorData::StaticClass()->FindPropertyByName(TEXT("RootParameterPropertyBag"));
	if (!Prop)
	{
		OutError = TEXT("RootParameterPropertyBag property not found on UStateTreeEditorData");
		return nullptr;
	}
	return reinterpret_cast<FInstancedPropertyBag*>(Prop->ContainerPtrToValuePtr<void>(EditorData));
}

static bool ApplyJsonToStruct(UScriptStruct* StructType, void* StructData,
	const TSharedPtr<FJsonObject>& Values, UObject* Owner, TArray<FString>& OutErrors)
{
	if (!StructType || !StructData || !Values.IsValid()) { return true; }
	bool bAnyError = false;
	for (const auto& Pair : Values->Values)
	{
		FString Err;
		if (!FEpicUnrealMCPCommonUtils::SetStructField(StructType, StructData, FString(*Pair.Key), Pair.Value, Owner, Err))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *Err));
			bAnyError = true;
		}
	}
	return !bAnyError;
}

bool FEpicUnrealMCPStateTreeCommands::InitializeEditorNode(FStateTreeEditorNode& OutNode,
	const FString& StructPath,
	const TSharedPtr<FJsonObject>& NodeProperties,
	const TSharedPtr<FJsonObject>& InstanceProperties,
	UStateTreeEditorData* Owner,
	FString& OutError)
{
	UScriptStruct* NodeStruct = ResolveScriptStruct(StructPath);
	if (!NodeStruct)
	{
		OutError = FString::Printf(TEXT("Could not resolve UScriptStruct '%s'"), *StructPath);
		return false;
	}
	if (!NodeStruct->IsChildOf(FStateTreeNodeBase::StaticStruct()))
	{
		OutError = FString::Printf(TEXT("Struct '%s' is not a FStateTreeNodeBase derivative"), *NodeStruct->GetName());
		return false;
	}

	OutNode.Reset();
	OutNode.ID = FGuid::NewGuid();
	OutNode.Node.InitializeAs(NodeStruct);

	const FStateTreeNodeBase& NodeBase = OutNode.Node.Get<FStateTreeNodeBase>();
	if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(NodeBase.GetInstanceDataType()))
	{
		OutNode.Instance.InitializeAs(InstanceType);
	}
	if (const UScriptStruct* RuntimeType = Cast<const UScriptStruct>(NodeBase.GetExecutionRuntimeDataType()))
	{
		OutNode.ExecutionRuntimeData.InitializeAs(RuntimeType);
	}

	TArray<FString> Errors;
	if (NodeProperties.IsValid() && OutNode.Node.IsValid())
	{
		ApplyJsonToStruct(const_cast<UScriptStruct*>(OutNode.Node.GetScriptStruct()),
			OutNode.Node.GetMutableMemory(), NodeProperties, Owner, Errors);
	}
	if (InstanceProperties.IsValid() && OutNode.Instance.IsValid())
	{
		ApplyJsonToStruct(const_cast<UScriptStruct*>(OutNode.Instance.GetScriptStruct()),
			OutNode.Instance.GetMutableMemory(), InstanceProperties, Owner, Errors);
	}

	if (Errors.Num() > 0)
	{
		OutError = FString::Join(Errors, TEXT("; "));
		// Return true anyway — the node was created. Caller reports partial errors.
	}
	return true;
}

bool FEpicUnrealMCPStateTreeCommands::FindNodeByGuid(UStateTreeEditorData* EditorData,
	const FGuid& NodeGuid, FNodeLocation& OutLocation)
{
	if (!EditorData || !NodeGuid.IsValid()) { return false; }

	auto ScanArray = [&](TArray<FStateTreeEditorNode>& Arr, UStateTreeState* State, const TCHAR* Name) -> bool
	{
		for (int32 i = 0; i < Arr.Num(); ++i)
		{
			if (Arr[i].ID == NodeGuid)
			{
				OutLocation.Node = &Arr[i];
				OutLocation.OwnerState = State;
				OutLocation.Container = &Arr;
				OutLocation.Index = i;
				OutLocation.ContainerName = Name;
				return true;
			}
		}
		return false;
	};

	// Globals + Evaluators
	if (ScanArray(EditorData->GlobalTasks, nullptr, TEXT("GlobalTasks"))) { return true; }
	if (ScanArray(EditorData->Evaluators, nullptr, TEXT("Evaluators"))) { return true; }

	// Walk the state hierarchy
	TFunction<bool(UStateTreeState*)> Walk;
	Walk = [&](UStateTreeState* State) -> bool
	{
		if (!State) { return false; }
		if (ScanArray(State->Tasks, State, TEXT("Tasks"))) { return true; }
		if (ScanArray(State->EnterConditions, State, TEXT("EnterConditions"))) { return true; }
		if (ScanArray(State->Considerations, State, TEXT("Considerations"))) { return true; }

		for (FStateTreeTransition& Transition : State->Transitions)
		{
			for (int32 i = 0; i < Transition.Conditions.Num(); ++i)
			{
				if (Transition.Conditions[i].ID == NodeGuid)
				{
					OutLocation.Node = &Transition.Conditions[i];
					OutLocation.OwnerState = State;
					OutLocation.Container = &Transition.Conditions;
					OutLocation.Index = i;
					OutLocation.ContainerName = TEXT("TransitionConditions");
					OutLocation.OwnerTransition = &Transition;
					return true;
				}
			}
		}
		for (UStateTreeState* Child : State->Children) { if (Walk(Child)) { return true; } }
		return false;
	};

	for (UStateTreeState* SubTree : EditorData->SubTrees)
	{
		if (Walk(SubTree)) { return true; }
	}
	return false;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::EditorNodeToJson(const FStateTreeEditorNode& Node) const
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("id"), Node.ID.ToString(EGuidFormats::DigitsWithHyphens));

	if (const UScriptStruct* NodeStruct = Node.Node.GetScriptStruct())
	{
		Obj->SetStringField(TEXT("struct"), NodeStruct->GetName());
		Obj->SetStringField(TEXT("struct_path"), NodeStruct->GetPathName());
	}
	if (const UScriptStruct* InstStruct = Node.Instance.GetScriptStruct())
	{
		Obj->SetStringField(TEXT("instance_struct"), InstStruct->GetName());
		if (Node.Instance.IsValid())
		{
			TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
			FJsonObjectConverter::UStructToJsonObject(
				InstStruct, Node.Instance.GetMemory(), Fields, /*CheckFlags*/0, /*SkipFlags*/0);
			Obj->SetObjectField(TEXT("instance"), Fields);
		}
	}
	if (Node.InstanceObject)
	{
		Obj->SetStringField(TEXT("instance_object_class"), Node.InstanceObject->GetClass()->GetName());
	}
	Obj->SetNumberField(TEXT("expression_indent"), Node.ExpressionIndent);
	Obj->SetStringField(TEXT("expression_operand"),
		UEnum::GetDisplayValueAsText(Node.ExpressionOperand).ToString());
	return Obj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::TransitionToJson(const FStateTreeTransition& Transition) const
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("id"), Transition.ID.ToString(EGuidFormats::DigitsWithHyphens));
	Obj->SetStringField(TEXT("trigger"), UEnum::GetValueAsString(Transition.Trigger));
	Obj->SetStringField(TEXT("priority"), UEnum::GetValueAsString(Transition.Priority));
	Obj->SetBoolField(TEXT("enabled"), Transition.bTransitionEnabled);
	Obj->SetBoolField(TEXT("delayed"), Transition.bDelayTransition);
	Obj->SetNumberField(TEXT("delay_duration"), Transition.DelayDuration);
	Obj->SetNumberField(TEXT("delay_random_variance"), Transition.DelayRandomVariance);
#if WITH_EDITORONLY_DATA
	Obj->SetStringField(TEXT("link_type"), UEnum::GetValueAsString(Transition.State.LinkType));
	Obj->SetStringField(TEXT("target_state_id"), Transition.State.ID.ToString(EGuidFormats::DigitsWithHyphens));
	Obj->SetStringField(TEXT("target_state_name"), Transition.State.Name.ToString());
#endif
	if (Transition.RequiredEvent.Tag.IsValid())
	{
		Obj->SetStringField(TEXT("required_event_tag"), Transition.RequiredEvent.Tag.ToString());
	}
	if (Transition.Conditions.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> CondJson;
		for (const FStateTreeEditorNode& Cond : Transition.Conditions)
		{
			CondJson.Add(MakeShareable(new FJsonValueObject(EditorNodeToJson(Cond))));
		}
		Obj->SetArrayField(TEXT("conditions"), CondJson);
	}
	return Obj;
}

// Resolve a node container name to a pointer for "global:tasks" / "global:evaluators" style ids.
static TArray<FStateTreeEditorNode>* ResolveGlobalContainer(UStateTreeEditorData* EditorData, const FString& Name)
{
	if (Name.Equals(TEXT("global:tasks"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("globaltasks"), ESearchCase::IgnoreCase))
	{
		return &EditorData->GlobalTasks;
	}
	if (Name.Equals(TEXT("global:evaluators"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("evaluators"), ESearchCase::IgnoreCase))
	{
		return &EditorData->Evaluators;
	}
	return nullptr;
}

// ============================================================================
// Phase 2: shared add-node helper
// ============================================================================

namespace
{
	struct FAddNodeArgs
	{
		FString AssetPath;
		FString StateOrContainer;  // state GUID/path OR "global:tasks" / "global:evaluators"
		FString StructPath;
		TSharedPtr<FJsonObject> NodeProperties;
		TSharedPtr<FJsonObject> InstanceProperties;
		bool bAutoCompile = false;
	};

	bool ExtractAddNodeArgs(const TSharedPtr<FJsonObject>& Params, FAddNodeArgs& Out, FString& OutError)
	{
		if (!Params->HasField(TEXT("asset_path"))) { OutError = TEXT("Missing asset_path"); return false; }
		if (!Params->HasField(TEXT("struct_path"))) { OutError = TEXT("Missing struct_path"); return false; }
		Out.AssetPath = Params->GetStringField(TEXT("asset_path"));
		Out.StructPath = Params->GetStringField(TEXT("struct_path"));
		if (Params->HasField(TEXT("state"))) { Out.StateOrContainer = Params->GetStringField(TEXT("state")); }
		if (Params->HasField(TEXT("container"))) { Out.StateOrContainer = Params->GetStringField(TEXT("container")); }
		if (Params->HasField(TEXT("node_properties"))) { Out.NodeProperties = Params->GetObjectField(TEXT("node_properties")); }
		if (Params->HasField(TEXT("instance_properties"))) { Out.InstanceProperties = Params->GetObjectField(TEXT("instance_properties")); }
		Out.bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
		return true;
	}
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddStateTask(const TSharedPtr<FJsonObject>& Params)
{
	FAddNodeArgs Args; FString Err;
	if (!ExtractAddNodeArgs(Params, Args, Err)) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTree* Tree = LoadStateTree(Args.AssetPath, Err); if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err); if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Args.StateOrContainer, Err); if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	State->Modify();
	FStateTreeEditorNode& Slot = State->Tasks.AddDefaulted_GetRef();
	FString NodeErr;
	if (!InitializeEditorNodeWithCategory(Slot, EStateTreeNodeCategory::Task,
		Args.StructPath, Args.NodeProperties, Args.InstanceProperties, Data, NodeErr))
	{
		State->Tasks.Pop(); // rollback
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(NodeErr);
	}
	FinalizeEdit(Tree, Args.bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), Slot.ID.ToString(EGuidFormats::DigitsWithHyphens));
	if (!NodeErr.IsEmpty()) { Result->SetStringField(TEXT("property_errors"), NodeErr); }
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddStateEnterCondition(const TSharedPtr<FJsonObject>& Params)
{
	FAddNodeArgs Args; FString Err;
	if (!ExtractAddNodeArgs(Params, Args, Err)) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTree* Tree = LoadStateTree(Args.AssetPath, Err); if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err); if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Args.StateOrContainer, Err); if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	State->Modify();
	FStateTreeEditorNode& Slot = State->EnterConditions.AddDefaulted_GetRef();
	FString NodeErr;
	if (!InitializeEditorNodeWithCategory(Slot, EStateTreeNodeCategory::Condition,
		Args.StructPath, Args.NodeProperties, Args.InstanceProperties, Data, NodeErr))
	{
		State->EnterConditions.Pop();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(NodeErr);
	}
	FinalizeEdit(Tree, Args.bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), Slot.ID.ToString(EGuidFormats::DigitsWithHyphens));
	if (!NodeErr.IsEmpty()) { Result->SetStringField(TEXT("property_errors"), NodeErr); }
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddStateConsideration(const TSharedPtr<FJsonObject>& Params)
{
	FAddNodeArgs Args; FString Err;
	if (!ExtractAddNodeArgs(Params, Args, Err)) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTree* Tree = LoadStateTree(Args.AssetPath, Err); if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err); if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Args.StateOrContainer, Err); if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	State->Modify();
	FStateTreeEditorNode& Slot = State->Considerations.AddDefaulted_GetRef();
	FString NodeErr;
	if (!InitializeEditorNodeWithCategory(Slot, EStateTreeNodeCategory::Consideration,
		Args.StructPath, Args.NodeProperties, Args.InstanceProperties, Data, NodeErr))
	{
		State->Considerations.Pop();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(NodeErr);
	}
	FinalizeEdit(Tree, Args.bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), Slot.ID.ToString(EGuidFormats::DigitsWithHyphens));
	if (!NodeErr.IsEmpty()) { Result->SetStringField(TEXT("property_errors"), NodeErr); }
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddGlobalTask(const TSharedPtr<FJsonObject>& Params)
{
	FAddNodeArgs Args; FString Err;
	if (!ExtractAddNodeArgs(Params, Args, Err)) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTree* Tree = LoadStateTree(Args.AssetPath, Err); if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err); if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	Data->Modify();
	FStateTreeEditorNode& Slot = Data->GlobalTasks.AddDefaulted_GetRef();
	FString NodeErr;
	if (!InitializeEditorNodeWithCategory(Slot, EStateTreeNodeCategory::Task,
		Args.StructPath, Args.NodeProperties, Args.InstanceProperties, Data, NodeErr))
	{
		Data->GlobalTasks.Pop();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(NodeErr);
	}
	FinalizeEdit(Tree, Args.bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), Slot.ID.ToString(EGuidFormats::DigitsWithHyphens));
	if (!NodeErr.IsEmpty()) { Result->SetStringField(TEXT("property_errors"), NodeErr); }
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddEvaluator(const TSharedPtr<FJsonObject>& Params)
{
	FAddNodeArgs Args; FString Err;
	if (!ExtractAddNodeArgs(Params, Args, Err)) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTree* Tree = LoadStateTree(Args.AssetPath, Err); if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err); if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	Data->Modify();
	FStateTreeEditorNode& Slot = Data->Evaluators.AddDefaulted_GetRef();
	FString NodeErr;
	if (!InitializeEditorNodeWithCategory(Slot, EStateTreeNodeCategory::Evaluator,
		Args.StructPath, Args.NodeProperties, Args.InstanceProperties, Data, NodeErr))
	{
		Data->Evaluators.Pop();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(NodeErr);
	}
	FinalizeEdit(Tree, Args.bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), Slot.ID.ToString(EGuidFormats::DigitsWithHyphens));
	if (!NodeErr.IsEmpty()) { Result->SetStringField(TEXT("property_errors"), NodeErr); }
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleRemoveStateTreeNode(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("node_id")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field(s): asset_path, node_id"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	FGuid NodeGuid;
	if (!FGuid::Parse(Params->GetStringField(TEXT("node_id")), NodeGuid))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("node_id is not a valid GUID"));
	}

	FNodeLocation Loc;
	if (!FindNodeByGuid(Data, NodeGuid, Loc))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Node %s not found"), *NodeGuid.ToString()));
	}

	if (Loc.OwnerState) { Loc.OwnerState->Modify(); } else { Data->Modify(); }
	Loc.Container->RemoveAt(Loc.Index);

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_from"), Loc.ContainerName);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleSetStateTreeNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("node_id")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field(s): asset_path, node_id"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	FGuid NodeGuid;
	if (!FGuid::Parse(Params->GetStringField(TEXT("node_id")), NodeGuid))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("node_id is not a valid GUID"));
	}
	FNodeLocation Loc;
	if (!FindNodeByGuid(Data, NodeGuid, Loc))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Node %s not found"), *NodeGuid.ToString()));
	}

	TArray<FString> AllErrors;
	if (Loc.OwnerState) { Loc.OwnerState->Modify(); } else { Data->Modify(); }

	if (Params->HasField(TEXT("node_properties")))
	{
		const TSharedPtr<FJsonObject> Props = Params->GetObjectField(TEXT("node_properties"));
		if (Loc.Node->Node.IsValid() && Props.IsValid())
		{
			ApplyJsonToStruct(const_cast<UScriptStruct*>(Loc.Node->Node.GetScriptStruct()),
				Loc.Node->Node.GetMutableMemory(), Props, Data, AllErrors);
		}
	}
	if (Params->HasField(TEXT("instance_properties")))
	{
		const TSharedPtr<FJsonObject> Props = Params->GetObjectField(TEXT("instance_properties"));
		if (Loc.Node->Instance.IsValid() && Props.IsValid())
		{
			ApplyJsonToStruct(const_cast<UScriptStruct*>(Loc.Node->Instance.GetScriptStruct()),
				Loc.Node->Instance.GetMutableMemory(), Props, Data, AllErrors);
		}
	}
	// Scalar node fields (ExpressionIndent, ExpressionOperand).
	if (Params->HasField(TEXT("editor_node_properties")))
	{
		const TSharedPtr<FJsonObject> EProps = Params->GetObjectField(TEXT("editor_node_properties"));
		if (EProps.IsValid())
		{
			ApplyJsonToStruct(FStateTreeEditorNode::StaticStruct(),
				static_cast<void*>(Loc.Node), EProps, Data, AllErrors);
		}
	}

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), AllErrors.Num() == 0);
	if (AllErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : AllErrors) { ErrArr.Add(MakeShareable(new FJsonValueString(E))); }
		Result->SetArrayField(TEXT("errors"), ErrArr);
	}
	return Result;
}

// ============================================================================
// Phase 2: Transitions
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddStateTreeTransition(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field(s): asset_path, state"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Params->GetStringField(TEXT("state")), Err);
	if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	// Parse trigger
	EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
	if (Params->HasField(TEXT("trigger")))
	{
		const UEnum* EnumPtr = StaticEnum<EStateTreeTransitionTrigger>();
		const int64 V = EnumPtr ? EnumPtr->GetValueByNameString(Params->GetStringField(TEXT("trigger"))) : INDEX_NONE;
		if (V == INDEX_NONE) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid trigger")); }
		Trigger = static_cast<EStateTreeTransitionTrigger>(V);
	}
	// Parse link type
	EStateTreeTransitionType LinkType = EStateTreeTransitionType::GotoState;
	if (Params->HasField(TEXT("link_type")))
	{
		const UEnum* EnumPtr = StaticEnum<EStateTreeTransitionType>();
		const int64 V = EnumPtr ? EnumPtr->GetValueByNameString(Params->GetStringField(TEXT("link_type"))) : INDEX_NONE;
		if (V == INDEX_NONE) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid link_type")); }
		LinkType = static_cast<EStateTreeTransitionType>(V);
	}

	// Resolve target state (optional depending on link type)
	UStateTreeState* Target = nullptr;
	if (Params->HasField(TEXT("target_state")))
	{
		Target = ResolveState(Data, Params->GetStringField(TEXT("target_state")), Err);
		if (!Target) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	}

	State->Modify();
	FStateTreeTransition& Transition = State->AddTransition(Trigger, LinkType, Target);

	if (Params->HasField(TEXT("required_event_tag")))
	{
		const FString TagStr = Params->GetStringField(TEXT("required_event_tag"));
		if (!TagStr.IsEmpty())
		{
			Transition.RequiredEvent.Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), /*ErrorIfNotFound*/false);
		}
	}
	if (Params->HasField(TEXT("priority")))
	{
		const UEnum* EnumPtr = StaticEnum<EStateTreeTransitionPriority>();
		const int64 V = EnumPtr ? EnumPtr->GetValueByNameString(Params->GetStringField(TEXT("priority"))) : INDEX_NONE;
		if (V != INDEX_NONE) { Transition.Priority = static_cast<EStateTreeTransitionPriority>(V); }
	}
	if (Params->HasField(TEXT("delay_duration")))
	{
		Transition.bDelayTransition = true;
		Transition.DelayDuration = Params->GetNumberField(TEXT("delay_duration"));
	}
	if (Params->HasField(TEXT("delay_random_variance")))
	{
		Transition.DelayRandomVariance = Params->GetNumberField(TEXT("delay_random_variance"));
	}
	if (Params->HasField(TEXT("enabled")))
	{
		Transition.bTransitionEnabled = Params->GetBoolField(TEXT("enabled"));
	}

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("transition_id"), Transition.ID.ToString(EGuidFormats::DigitsWithHyphens));
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleRemoveStateTreeTransition(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state")) || !Params->HasField(TEXT("transition_id")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field(s): asset_path, state, transition_id"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Params->GetStringField(TEXT("state")), Err);
	if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	FGuid TransitionId;
	if (!FGuid::Parse(Params->GetStringField(TEXT("transition_id")), TransitionId))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("transition_id is not a valid GUID"));
	}

	const int32 Idx = State->Transitions.IndexOfByPredicate(
		[&](const FStateTreeTransition& T) { return T.ID == TransitionId; });
	if (Idx == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Transition %s not found on state %s"),
				*TransitionId.ToString(), *State->Name.ToString()));
	}

	State->Modify();
	State->Transitions.RemoveAt(Idx);

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleSetStateTreeTransitionProperty(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state"))
		|| !Params->HasField(TEXT("transition_id")) || !Params->HasField(TEXT("properties")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, state, transition_id, properties"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Params->GetStringField(TEXT("state")), Err);
	if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	FGuid TransitionId;
	if (!FGuid::Parse(Params->GetStringField(TEXT("transition_id")), TransitionId))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("transition_id is not a valid GUID"));
	}
	FStateTreeTransition* Transition = State->Transitions.FindByPredicate(
		[&](const FStateTreeTransition& T) { return T.ID == TransitionId; });
	if (!Transition)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Transition not found"));
	}

	State->Modify();
	TArray<FString> Errors;
	ApplyJsonToStruct(FStateTreeTransition::StaticStruct(),
		static_cast<void*>(Transition),
		Params->GetObjectField(TEXT("properties")),
		Data, Errors);

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Errors.Num() == 0);
	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : Errors) { ErrArr.Add(MakeShareable(new FJsonValueString(E))); }
		Result->SetArrayField(TEXT("errors"), ErrArr);
	}
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddTransitionCondition(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state"))
		|| !Params->HasField(TEXT("transition_id")) || !Params->HasField(TEXT("struct_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, state, transition_id, struct_path"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Params->GetStringField(TEXT("state")), Err);
	if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	FGuid TransitionId;
	if (!FGuid::Parse(Params->GetStringField(TEXT("transition_id")), TransitionId))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("transition_id is not a valid GUID"));
	}
	FStateTreeTransition* Transition = State->Transitions.FindByPredicate(
		[&](const FStateTreeTransition& T) { return T.ID == TransitionId; });
	if (!Transition) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Transition not found")); }

	const FString StructPath = Params->GetStringField(TEXT("struct_path"));
	TSharedPtr<FJsonObject> NodeProps = Params->HasField(TEXT("node_properties"))
		? Params->GetObjectField(TEXT("node_properties")) : nullptr;
	TSharedPtr<FJsonObject> InstProps = Params->HasField(TEXT("instance_properties"))
		? Params->GetObjectField(TEXT("instance_properties")) : nullptr;

	State->Modify();
	FStateTreeEditorNode& Slot = Transition->Conditions.AddDefaulted_GetRef();
	FString NodeErr;
	if (!InitializeEditorNodeWithCategory(Slot, EStateTreeNodeCategory::Condition,
		StructPath, NodeProps, InstProps, Data, NodeErr))
	{
		Transition->Conditions.Pop();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(NodeErr);
	}

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), Slot.ID.ToString(EGuidFormats::DigitsWithHyphens));
	if (!NodeErr.IsEmpty()) { Result->SetStringField(TEXT("property_errors"), NodeErr); }
	return Result;
}

// ============================================================================
// Phase 3: Root parameters + property bindings
// ============================================================================

static EPropertyBagPropertyType ParsePropertyBagType(const FString& Name)
{
	if (Name.Equals(TEXT("bool"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Bool; }
	if (Name.Equals(TEXT("byte"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Byte; }
	if (Name.Equals(TEXT("int"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("int32"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Int32; }
	if (Name.Equals(TEXT("int64"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Int64; }
	if (Name.Equals(TEXT("float"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Float; }
	if (Name.Equals(TEXT("double"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Double; }
	if (Name.Equals(TEXT("name"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Name; }
	if (Name.Equals(TEXT("string"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::String; }
	if (Name.Equals(TEXT("text"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Text; }
	if (Name.Equals(TEXT("enum"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Enum; }
	if (Name.Equals(TEXT("struct"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Struct; }
	if (Name.Equals(TEXT("object"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Object; }
	if (Name.Equals(TEXT("softobject"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::SoftObject; }
	if (Name.Equals(TEXT("class"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::Class; }
	if (Name.Equals(TEXT("softclass"), ESearchCase::IgnoreCase)) { return EPropertyBagPropertyType::SoftClass; }
	return EPropertyBagPropertyType::None;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddRootParameter(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("name")) || !Params->HasField(TEXT("type")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, name, type"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	FInstancedPropertyBag* Bag = GetMutableRootParameterBag(Data, Err);
	if (!Bag) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	Data->Modify();
	const FString ValueType = Params->HasField(TEXT("value_type")) ? Params->GetStringField(TEXT("value_type")) : FString();
	FString AddErr;
	TSharedPtr<FJsonObject> AddResult = AddPropertyToBag(*Bag,
		Params->GetStringField(TEXT("name")),
		Params->GetStringField(TEXT("type")),
		ValueType, AddErr);
	if (!AddResult) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(AddErr); }

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);
	return AddResult;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleRemoveRootParameter(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field(s): asset_path, name"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	FInstancedPropertyBag* Bag = GetMutableRootParameterBag(Data, Err);
	if (!Bag) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	Data->Modify();
	Bag->RemovePropertyByName(FName(*Params->GetStringField(TEXT("name"))));

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleSetRootParameterValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("name")) || !Params->HasField(TEXT("value")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field(s): asset_path, name, value"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	FInstancedPropertyBag* Bag = GetMutableRootParameterBag(Data, Err);
	if (!Bag) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	Data->Modify();
	FString ApplyErr;
	if (!ApplyJsonToBag(*Bag,
		FName(*Params->GetStringField(TEXT("name"))),
		Params->TryGetField(TEXT("value")),
		ApplyErr))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ApplyErr);
	}

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

static bool BuildBindingPath(const FGuid& StructID, const FString& PathStr,
	FPropertyBindingPath& OutPath, FString& OutError)
{
	OutPath.Reset();
	OutPath.SetStructID(StructID);
	if (!PathStr.IsEmpty() && !OutPath.FromString(PathStr))
	{
		OutError = FString::Printf(TEXT("Could not parse path '%s'"), *PathStr);
		return false;
	}
	OutPath.SetStructID(StructID);  // FromString clears StructID, set again.
	return true;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddPropertyBinding(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path"))
		|| !Params->HasField(TEXT("source_struct_id")) || !Params->HasField(TEXT("target_struct_id")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, source_struct_id, target_struct_id"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	FGuid SourceID, TargetID;
	if (!FGuid::Parse(Params->GetStringField(TEXT("source_struct_id")), SourceID)
		|| !FGuid::Parse(Params->GetStringField(TEXT("target_struct_id")), TargetID))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("struct_id parameters must be GUIDs"));
	}
	const FString SourcePathStr = Params->HasField(TEXT("source_path")) ? Params->GetStringField(TEXT("source_path")) : FString();
	const FString TargetPathStr = Params->HasField(TEXT("target_path")) ? Params->GetStringField(TEXT("target_path")) : FString();

	FPropertyBindingPath SourcePath, TargetPath;
	if (!BuildBindingPath(SourceID, SourcePathStr, SourcePath, Err)) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	if (!BuildBindingPath(TargetID, TargetPathStr, TargetPath, Err)) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	Data->Modify();
	Data->AddPropertyBinding(SourcePath, TargetPath);

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleRemovePropertyBinding(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("target_struct_id")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field(s): asset_path, target_struct_id"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	FGuid TargetID;
	if (!FGuid::Parse(Params->GetStringField(TEXT("target_struct_id")), TargetID))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("target_struct_id must be a GUID"));
	}
	const FString TargetPathStr = Params->HasField(TEXT("target_path")) ? Params->GetStringField(TEXT("target_path")) : FString();
	FPropertyBindingPath TargetPath;
	if (!BuildBindingPath(TargetID, TargetPathStr, TargetPath, Err)) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	Data->Modify();
	Data->GetPropertyEditorBindings()->RemoveBindings(TargetPath);

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleListPropertyBindings(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FStateTreeEditorPropertyBindings* Bindings = Data->GetPropertyEditorBindings();
	TArray<TSharedPtr<FJsonValue>> BindingsJson;
	if (Bindings)
	{
		for (const FStateTreePropertyPathBinding& B : Bindings->GetBindings())
		{
			TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
			Entry->SetStringField(TEXT("source_struct_id"), B.GetSourcePath().GetStructID().ToString(EGuidFormats::DigitsWithHyphens));
			Entry->SetStringField(TEXT("source_path"), B.GetSourcePath().ToString());
			Entry->SetStringField(TEXT("target_struct_id"), B.GetTargetPath().GetStructID().ToString(EGuidFormats::DigitsWithHyphens));
			Entry->SetStringField(TEXT("target_path"), B.GetTargetPath().ToString());
			BindingsJson.Add(MakeShareable(new FJsonValueObject(Entry)));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), BindingsJson.Num());
	Result->SetArrayField(TEXT("bindings"), BindingsJson);
	return Result;
}

// ============================================================================
// Phase 4: analyze_state_tree
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAnalyzeStateTree(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const bool bRunCompile = Params->HasField(TEXT("run_compile")) && Params->GetBoolField(TEXT("run_compile"));

	// Collect all known state GUIDs for transition-target validation.
	TSet<FGuid> AllStateIds;
	TMap<FGuid, FString> AllStateNames;
	TArray<FString> Issues;
	int32 TotalStates = 0;
	int32 TotalTasks = 0;
	int32 TotalEnterConds = 0;
	int32 TotalConsiderations = 0;
	int32 TotalTransitions = 0;

	TFunction<void(const UStateTreeState*)> Walk;
	Walk = [&](const UStateTreeState* State)
	{
		if (!State) { return; }
		++TotalStates;
		AllStateIds.Add(State->ID);
		AllStateNames.Add(State->ID, State->Name.ToString());
		TotalTasks += State->Tasks.Num();
		TotalEnterConds += State->EnterConditions.Num();
		TotalConsiderations += State->Considerations.Num();
		TotalTransitions += State->Transitions.Num();
		for (const UStateTreeState* Child : State->Children) { Walk(Child); }
	};
	for (const UStateTreeState* Root : Data->SubTrees) { Walk(Root); }

	// Validate transition targets.
	TArray<TSharedPtr<FJsonValue>> BrokenTransitions;
	TFunction<void(const UStateTreeState*, const FString&)> CheckTransitions;
	CheckTransitions = [&](const UStateTreeState* State, const FString& Path)
	{
		if (!State) { return; }
		const FString MyPath = Path.IsEmpty() ? State->Name.ToString() : Path + TEXT("/") + State->Name.ToString();
		for (const FStateTreeTransition& T : State->Transitions)
		{
#if WITH_EDITORONLY_DATA
			if (T.State.LinkType == EStateTreeTransitionType::GotoState && T.State.ID.IsValid()
				&& !AllStateIds.Contains(T.State.ID))
			{
				TSharedPtr<FJsonObject> B = MakeShareable(new FJsonObject);
				B->SetStringField(TEXT("state_path"), MyPath);
				B->SetStringField(TEXT("transition_id"), T.ID.ToString(EGuidFormats::DigitsWithHyphens));
				B->SetStringField(TEXT("broken_target_id"), T.State.ID.ToString(EGuidFormats::DigitsWithHyphens));
				B->SetStringField(TEXT("recorded_name"), T.State.Name.ToString());
				BrokenTransitions.Add(MakeShareable(new FJsonValueObject(B)));
			}
#endif
		}
		for (const UStateTreeState* Child : State->Children) { CheckTransitions(Child, MyPath); }
	};
	for (const UStateTreeState* Root : Data->SubTrees) { CheckTransitions(Root, FString()); }

	// Orphan bindings: target struct ID not present in the tree's known struct IDs.
	TSet<FGuid> KnownStructIds;
	KnownStructIds.Add(Data->GetRootParametersGuid());
	TFunction<void(const UStateTreeState*)> CollectNodeIds;
	CollectNodeIds = [&](const UStateTreeState* State)
	{
		if (!State) { return; }
		KnownStructIds.Add(State->ID);
		KnownStructIds.Add(State->Parameters.ID);
		for (const FStateTreeEditorNode& N : State->Tasks) { KnownStructIds.Add(N.ID); }
		for (const FStateTreeEditorNode& N : State->EnterConditions) { KnownStructIds.Add(N.ID); }
		for (const FStateTreeEditorNode& N : State->Considerations) { KnownStructIds.Add(N.ID); }
		for (const FStateTreeTransition& T : State->Transitions)
		{
			KnownStructIds.Add(T.ID);
			for (const FStateTreeEditorNode& N : T.Conditions) { KnownStructIds.Add(N.ID); }
		}
		for (const UStateTreeState* Child : State->Children) { CollectNodeIds(Child); }
	};
	for (const FStateTreeEditorNode& N : Data->GlobalTasks) { KnownStructIds.Add(N.ID); }
	for (const FStateTreeEditorNode& N : Data->Evaluators) { KnownStructIds.Add(N.ID); }
	for (const UStateTreeState* Root : Data->SubTrees) { CollectNodeIds(Root); }

	TArray<TSharedPtr<FJsonValue>> OrphanBindings;
	if (const FStateTreeEditorPropertyBindings* Bindings = Data->GetPropertyEditorBindings())
	{
		for (const FStateTreePropertyPathBinding& B : Bindings->GetBindings())
		{
			const FGuid SrcId = B.GetSourcePath().GetStructID();
			const FGuid DstId = B.GetTargetPath().GetStructID();
			const bool bSrcOrphan = SrcId.IsValid() && !KnownStructIds.Contains(SrcId);
			const bool bDstOrphan = DstId.IsValid() && !KnownStructIds.Contains(DstId);
			if (bSrcOrphan || bDstOrphan)
			{
				TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
				Obj->SetStringField(TEXT("source_struct_id"), SrcId.ToString(EGuidFormats::DigitsWithHyphens));
				Obj->SetStringField(TEXT("target_struct_id"), DstId.ToString(EGuidFormats::DigitsWithHyphens));
				Obj->SetStringField(TEXT("target_path"), B.GetTargetPath().ToString());
				Obj->SetBoolField(TEXT("source_orphan"), bSrcOrphan);
				Obj->SetBoolField(TEXT("target_orphan"), bDstOrphan);
				OrphanBindings.Add(MakeShareable(new FJsonValueObject(Obj)));
			}
		}
	}

	// Compile status.
	const uint32 CurrentHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree);
	const bool bUpToDate = CurrentHash == Tree->LastCompiledEditorDataHash;

	TArray<TSharedPtr<FJsonValue>> CompileMessages;
	bool bCompileSucceeded = true;
	if (bRunCompile)
	{
		FStateTreeCompilerLog Log;
		bCompileSucceeded = UStateTreeEditingSubsystem::CompileStateTree(Tree, Log);
		auto SeverityToString = [](EMessageSeverity::Type S) -> const TCHAR*
		{
			switch (S)
			{
			case EMessageSeverity::Error:              return TEXT("Error");
			case EMessageSeverity::PerformanceWarning: return TEXT("PerformanceWarning");
			case EMessageSeverity::Warning:            return TEXT("Warning");
			case EMessageSeverity::Info:               return TEXT("Info");
			default:                                   return TEXT("Unknown");
			}
		};
		for (const TSharedRef<FTokenizedMessage>& Msg : Log.ToTokenizedMessages())
		{
			TSharedPtr<FJsonObject> M = MakeShareable(new FJsonObject);
			M->SetStringField(TEXT("severity"), SeverityToString(Msg->GetSeverity()));
			M->SetStringField(TEXT("message"), Msg->ToText().ToString());
			CompileMessages.Add(MakeShareable(new FJsonValueObject(M)));
		}
	}

	// Build result.
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Tree->GetPathName());
	Result->SetStringField(TEXT("schema_class"),
		Data->Schema ? Data->Schema->GetClass()->GetName() : TEXT(""));

	TSharedPtr<FJsonObject> Counts = MakeShareable(new FJsonObject);
	Counts->SetNumberField(TEXT("subtrees"), Data->SubTrees.Num());
	Counts->SetNumberField(TEXT("states"), TotalStates);
	Counts->SetNumberField(TEXT("tasks"), TotalTasks);
	Counts->SetNumberField(TEXT("enter_conditions"), TotalEnterConds);
	Counts->SetNumberField(TEXT("considerations"), TotalConsiderations);
	Counts->SetNumberField(TEXT("transitions"), TotalTransitions);
	Counts->SetNumberField(TEXT("global_tasks"), Data->GlobalTasks.Num());
	Counts->SetNumberField(TEXT("evaluators"), Data->Evaluators.Num());
	Counts->SetNumberField(TEXT("root_parameters"),
		Data->GetRootParametersPropertyBag().GetNumPropertiesInBag());
	if (const FStateTreeEditorPropertyBindings* Bindings = Data->GetPropertyEditorBindings())
	{
		Counts->SetNumberField(TEXT("property_bindings"), Bindings->GetBindings().Num());
	}
	Result->SetObjectField(TEXT("counts"), Counts);

	TSharedPtr<FJsonObject> Compile = MakeShareable(new FJsonObject);
	Compile->SetBoolField(TEXT("editor_up_to_date"), bUpToDate);
	Compile->SetNumberField(TEXT("editor_data_hash"), CurrentHash);
	Compile->SetNumberField(TEXT("last_compiled_hash"), Tree->LastCompiledEditorDataHash);
	if (bRunCompile)
	{
		Compile->SetBoolField(TEXT("compile_succeeded"), bCompileSucceeded);
		Compile->SetArrayField(TEXT("messages"), CompileMessages);
	}
	Result->SetObjectField(TEXT("compile"), Compile);

	Result->SetArrayField(TEXT("broken_transitions"), BrokenTransitions);
	Result->SetArrayField(TEXT("orphan_bindings"), OrphanBindings);
	return Result;
}

// ============================================================================
// Phase 5 helpers: category-aware node init + complex property-bag values
// ============================================================================

namespace
{
	struct FNodeCategorySpec
	{
		const UScriptStruct* NativeBase;
		const UScriptStruct* BlueprintWrapper;
		UClass* BlueprintBaseClass;
		FName WrapperClassPropertyName;
	};

	FNodeCategorySpec GetCategorySpec(FEpicUnrealMCPStateTreeCommands::EStateTreeNodeCategory Category)
	{
		using E = FEpicUnrealMCPStateTreeCommands::EStateTreeNodeCategory;
		switch (Category)
		{
		case E::Task:
			return { FStateTreeTaskBase::StaticStruct(),
				FStateTreeBlueprintTaskWrapper::StaticStruct(),
				UStateTreeTaskBlueprintBase::StaticClass(),
				FName(TEXT("TaskClass")) };
		case E::Condition:
			return { FStateTreeConditionBase::StaticStruct(),
				FStateTreeBlueprintConditionWrapper::StaticStruct(),
				UStateTreeConditionBlueprintBase::StaticClass(),
				FName(TEXT("ConditionClass")) };
		case E::Consideration:
			return { FStateTreeConsiderationBase::StaticStruct(),
				FStateTreeBlueprintConsiderationWrapper::StaticStruct(),
				UStateTreeConsiderationBlueprintBase::StaticClass(),
				FName(TEXT("ConsiderationClass")) };
		case E::Evaluator:
			return { FStateTreeEvaluatorBase::StaticStruct(),
				FStateTreeBlueprintEvaluatorWrapper::StaticStruct(),
				UStateTreeEvaluatorBlueprintBase::StaticClass(),
				FName(TEXT("EvaluatorClass")) };
		}
		return { nullptr, nullptr, nullptr, NAME_None };
	}
}

bool FEpicUnrealMCPStateTreeCommands::InitializeEditorNodeWithCategory(
	FStateTreeEditorNode& OutNode,
	EStateTreeNodeCategory Category,
	const FString& StructOrClassPath,
	const TSharedPtr<FJsonObject>& NodeProperties,
	const TSharedPtr<FJsonObject>& InstanceProperties,
	UStateTreeEditorData* Owner,
	FString& OutError)
{
	const FNodeCategorySpec Spec = GetCategorySpec(Category);

	// First try as a native UScriptStruct. If it resolves and derives from the
	// expected base, use the existing native path.
	if (UScriptStruct* AsStruct = ResolveScriptStruct(StructOrClassPath))
	{
		if (Spec.NativeBase && AsStruct->IsChildOf(Spec.NativeBase))
		{
			return InitializeEditorNode(OutNode, StructOrClassPath, NodeProperties, InstanceProperties, Owner, OutError);
		}
	}

	// Fall through to UClass (Blueprint subclass) resolution.
	UClass* AsClass = FEpicUnrealMCPCommonUtils::ResolveClass(StructOrClassPath);
	if (!AsClass)
	{
		OutError = FString::Printf(TEXT("Could not resolve '%s' as UScriptStruct or UClass"), *StructOrClassPath);
		return false;
	}
	if (!Spec.BlueprintBaseClass || !AsClass->IsChildOf(Spec.BlueprintBaseClass))
	{
		OutError = FString::Printf(TEXT("Class '%s' is not a subclass of %s"),
			*AsClass->GetName(),
			Spec.BlueprintBaseClass ? *Spec.BlueprintBaseClass->GetName() : TEXT("<unknown>"));
		return false;
	}
	if (AsClass->HasAnyClassFlags(CLASS_Abstract))
	{
		OutError = FString::Printf(TEXT("Class '%s' is abstract"), *AsClass->GetName());
		return false;
	}

	OutNode.Reset();
	OutNode.ID = FGuid::NewGuid();
	// Wrapper struct is non-const UScriptStruct* for InitializeAs.
	OutNode.Node.InitializeAs(const_cast<UScriptStruct*>(Spec.BlueprintWrapper));

	// Set the wrapper's class pointer (e.g. TaskClass = AsClass) via reflection.
	if (OutNode.Node.IsValid())
	{
		if (FProperty* ClassProp = Spec.BlueprintWrapper->FindPropertyByName(Spec.WrapperClassPropertyName))
		{
			if (FClassProperty* CP = CastField<FClassProperty>(ClassProp))
			{
				CP->SetObjectPropertyValue_InContainer(OutNode.Node.GetMutableMemory(), AsClass);
			}
		}
	}

	// Allocate the BP instance object.
	OutNode.InstanceObject = NewObject<UObject>(Owner, AsClass, NAME_None, RF_Transactional);

	TArray<FString> Errors;
	if (NodeProperties.IsValid() && OutNode.Node.IsValid())
	{
		ApplyJsonToStruct(const_cast<UScriptStruct*>(OutNode.Node.GetScriptStruct()),
			OutNode.Node.GetMutableMemory(), NodeProperties, Owner, Errors);
	}
	if (InstanceProperties.IsValid() && OutNode.InstanceObject)
	{
		for (const auto& Pair : InstanceProperties->Values)
		{
			FString PropErr;
			const bool bOk = FEpicUnrealMCPCommonUtils::SetObjectProperty(OutNode.InstanceObject, FString(*Pair.Key), Pair.Value, PropErr);
			if (bOk)
			{
				if (!PropErr.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("%s [partial]: %s"), *Pair.Key, *PropErr));
				}
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *PropErr));
			}
		}
	}
	if (Errors.Num() > 0) { OutError = FString::Join(Errors, TEXT("; ")); }
	return true;
}

// Apply a JSON value to a FInstancedPropertyBag field, routing by the desc type.
static bool ApplyJsonToBag(FInstancedPropertyBag& Bag, const FName PropName,
	const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
	if (!BagStruct)
	{
		OutError = TEXT("Property bag is empty");
		return false;
	}
	const FPropertyBagPropertyDesc* Desc = BagStruct->GetPropertyDescs().FindByPredicate(
		[PropName](const FPropertyBagPropertyDesc& D) { return D.Name == PropName; });
	if (!Desc)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found in bag"), *PropName.ToString());
		return false;
	}

	switch (Desc->ValueType)
	{
	case EPropertyBagPropertyType::Bool:
		if (Value->Type == EJson::Boolean) { Bag.SetValueBool(PropName, Value->AsBool()); return true; }
		break;
	case EPropertyBagPropertyType::Byte:
		if (Value->Type == EJson::Number) { Bag.SetValueUInt32(PropName, static_cast<uint32>(Value->AsNumber())); return true; }
		break;
	case EPropertyBagPropertyType::Int32:
		if (Value->Type == EJson::Number) { Bag.SetValueInt32(PropName, static_cast<int32>(Value->AsNumber())); return true; }
		break;
	case EPropertyBagPropertyType::Int64:
		if (Value->Type == EJson::Number) { Bag.SetValueInt64(PropName, static_cast<int64>(Value->AsNumber())); return true; }
		break;
	case EPropertyBagPropertyType::UInt32:
		if (Value->Type == EJson::Number) { Bag.SetValueUInt32(PropName, static_cast<uint32>(Value->AsNumber())); return true; }
		break;
	case EPropertyBagPropertyType::UInt64:
		if (Value->Type == EJson::Number) { Bag.SetValueUInt64(PropName, static_cast<uint64>(Value->AsNumber())); return true; }
		break;
	case EPropertyBagPropertyType::Float:
		if (Value->Type == EJson::Number) { Bag.SetValueFloat(PropName, static_cast<float>(Value->AsNumber())); return true; }
		break;
	case EPropertyBagPropertyType::Double:
		if (Value->Type == EJson::Number) { Bag.SetValueDouble(PropName, Value->AsNumber()); return true; }
		break;
	case EPropertyBagPropertyType::Name:
		if (Value->Type == EJson::String) { Bag.SetValueName(PropName, FName(*Value->AsString())); return true; }
		break;
	case EPropertyBagPropertyType::String:
		if (Value->Type == EJson::String) { Bag.SetValueString(PropName, Value->AsString()); return true; }
		break;
	case EPropertyBagPropertyType::Text:
		if (Value->Type == EJson::String) { Bag.SetValueText(PropName, FText::FromString(Value->AsString())); return true; }
		break;
	case EPropertyBagPropertyType::Enum:
	{
		if (Value->Type == EJson::String)
		{
			const UEnum* Enum = Cast<UEnum>(Desc->ValueTypeObject);
			if (!Enum) { OutError = TEXT("Enum type missing on desc"); return false; }
			const int64 Val = Enum->GetValueByNameString(Value->AsString());
			if (Val == INDEX_NONE) { OutError = FString::Printf(TEXT("Enum value '%s' not found"), *Value->AsString()); return false; }
			Bag.SetValueEnum(PropName, static_cast<uint8>(Val), Enum);
			return true;
		}
		if (Value->Type == EJson::Number)
		{
			const UEnum* Enum = Cast<UEnum>(Desc->ValueTypeObject);
			if (!Enum) { OutError = TEXT("Enum type missing on desc"); return false; }
			Bag.SetValueEnum(PropName, static_cast<uint8>(Value->AsNumber()), Enum);
			return true;
		}
		break;
	}
	case EPropertyBagPropertyType::Struct:
	{
		const UScriptStruct* StructType = Cast<UScriptStruct>(Desc->ValueTypeObject);
		if (!StructType) { OutError = TEXT("Struct type missing on desc"); return false; }
		if (Value->Type != EJson::Object) { OutError = TEXT("Struct value requires JSON object"); return false; }
		// Allocate temporary struct instance, populate from JSON, set via SetValueStruct.
		TArray<uint8> Buffer;
		Buffer.SetNumZeroed(StructType->GetStructureSize());
		StructType->InitializeStruct(Buffer.GetData());
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (Obj.IsValid())
		{
			FJsonObjectConverter::JsonObjectToUStruct(
				Obj.ToSharedRef(), StructType, Buffer.GetData(), 0, 0);
		}
		Bag.SetValueStruct(PropName, FConstStructView(StructType, Buffer.GetData()));
		StructType->DestroyStruct(Buffer.GetData());
		return true;
	}
	case EPropertyBagPropertyType::Object:
	case EPropertyBagPropertyType::SoftObject:
	{
		if (Value->Type != EJson::String) { OutError = TEXT("Object value requires asset path string"); return false; }
		UObject* Asset = UEditorAssetLibrary::LoadAsset(Value->AsString());
		if (Desc->ValueType == EPropertyBagPropertyType::SoftObject)
		{
			// SoftObject stores a soft pointer; no direct setter for soft path strings,
			// but SetValueObject accepts the resolved object and is handled internally.
		}
		Bag.SetValueObject(PropName, Asset);
		return true;
	}
	case EPropertyBagPropertyType::Class:
	case EPropertyBagPropertyType::SoftClass:
	{
		if (Value->Type != EJson::String) { OutError = TEXT("Class value requires class name string"); return false; }
		UClass* ClassVal = FEpicUnrealMCPCommonUtils::ResolveClass(Value->AsString());
		Bag.SetValueClass(PropName, ClassVal);
		return true;
	}
	default:
		break;
	}
	OutError = FString::Printf(TEXT("JSON type mismatch for bag property '%s'"), *PropName.ToString());
	return false;
}

// Add or set on an FInstancedPropertyBag, used by both root params and state params.
static TSharedPtr<FJsonObject> AddPropertyToBag(
	FInstancedPropertyBag& Bag, const FString& Name, const FString& Type, const FString& ValueType,
	FString& OutError)
{
	const FName PropName(*Name);
	const EPropertyBagPropertyType TypeEnum = ParsePropertyBagType(Type);
	if (TypeEnum == EPropertyBagPropertyType::None)
	{
		OutError = FString::Printf(TEXT("Unsupported type '%s'"), *Type);
		return nullptr;
	}
	UObject* ValueTypeObject = nullptr;
	if (!ValueType.IsEmpty())
	{
		if (TypeEnum == EPropertyBagPropertyType::Struct)
		{
			ValueTypeObject = FindFirstObject<UScriptStruct>(*ValueType, EFindFirstObjectOptions::ExactClass);
		}
		else if (TypeEnum == EPropertyBagPropertyType::Enum)
		{
			ValueTypeObject = FindFirstObject<UEnum>(*ValueType, EFindFirstObjectOptions::ExactClass);
		}
		else
		{
			ValueTypeObject = FEpicUnrealMCPCommonUtils::ResolveClass(ValueType);
		}
	}
	Bag.AddProperty(PropName, TypeEnum, ValueTypeObject);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), PropName.ToString());
	return Result;
}

// ============================================================================
// Phase 5 handlers
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleListStateTreeNodeClasses(const TSharedPtr<FJsonObject>& Params)
{
	const FString CategoryStr = Params->HasField(TEXT("category"))
		? Params->GetStringField(TEXT("category")) : FString(TEXT("all"));

	FStateTreeEditorModule& Mod = FStateTreeEditorModule::GetModule();
	TSharedPtr<FStateTreeNodeClassCache> Cache = Mod.GetNodeClassCache();
	if (!Cache.IsValid())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Node class cache unavailable"));
	}

	// Register root bases so the cache populates.
	Cache->AddRootStruct(FStateTreeTaskBase::StaticStruct());
	Cache->AddRootStruct(FStateTreeConditionBase::StaticStruct());
	Cache->AddRootStruct(FStateTreeConsiderationBase::StaticStruct());
	Cache->AddRootStruct(FStateTreeEvaluatorBase::StaticStruct());
	Cache->AddRootClass(UStateTreeTaskBlueprintBase::StaticClass());
	Cache->AddRootClass(UStateTreeConditionBlueprintBase::StaticClass());
	Cache->AddRootClass(UStateTreeConsiderationBlueprintBase::StaticClass());
	Cache->AddRootClass(UStateTreeEvaluatorBlueprintBase::StaticClass());

	auto Collect = [&](TNotNull<const UStruct*> Base, const TCHAR* Label,
		TArray<TSharedPtr<FJsonValue>>& Out)
	{
		TArray<TSharedPtr<FStateTreeNodeClassData>> Classes;
		Cache->GetStructs(Base, Classes);
		for (const TSharedPtr<FStateTreeNodeClassData>& Data : Classes)
		{
			if (!Data.IsValid()) { continue; }
			UStruct* S = Data->GetStruct(/*bSilent*/true);
			if (!S) { continue; }
			if (S == Base) { continue; }
			TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
			Entry->SetStringField(TEXT("name"), S->GetName());
			Entry->SetStringField(TEXT("path"), S->GetPathName());
			Entry->SetStringField(TEXT("category"), Label);
			Entry->SetBoolField(TEXT("is_class"), Cast<UClass>(S) != nullptr);
			Out.Add(MakeShareable(new FJsonValueObject(Entry)));
		}
	};

	TArray<TSharedPtr<FJsonValue>> All;
	const bool bAll = CategoryStr.Equals(TEXT("all"), ESearchCase::IgnoreCase);
	if (bAll || CategoryStr.Equals(TEXT("task"), ESearchCase::IgnoreCase))
	{
		Collect(FStateTreeTaskBase::StaticStruct(), TEXT("task"), All);
		Collect(UStateTreeTaskBlueprintBase::StaticClass(), TEXT("task"), All);
	}
	if (bAll || CategoryStr.Equals(TEXT("condition"), ESearchCase::IgnoreCase))
	{
		Collect(FStateTreeConditionBase::StaticStruct(), TEXT("condition"), All);
		Collect(UStateTreeConditionBlueprintBase::StaticClass(), TEXT("condition"), All);
	}
	if (bAll || CategoryStr.Equals(TEXT("consideration"), ESearchCase::IgnoreCase))
	{
		Collect(FStateTreeConsiderationBase::StaticStruct(), TEXT("consideration"), All);
		Collect(UStateTreeConsiderationBlueprintBase::StaticClass(), TEXT("consideration"), All);
	}
	if (bAll || CategoryStr.Equals(TEXT("evaluator"), ESearchCase::IgnoreCase))
	{
		Collect(FStateTreeEvaluatorBase::StaticStruct(), TEXT("evaluator"), All);
		Collect(UStateTreeEvaluatorBlueprintBase::StaticClass(), TEXT("evaluator"), All);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), All.Num());
	Result->SetArrayField(TEXT("classes"), All);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddStateParameter(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state"))
		|| !Params->HasField(TEXT("name")) || !Params->HasField(TEXT("type")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, state, name, type"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Params->GetStringField(TEXT("state")), Err);
	if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	State->Modify();
	const FString ValueType = Params->HasField(TEXT("value_type")) ? Params->GetStringField(TEXT("value_type")) : FString();
	TSharedPtr<FJsonObject> AddResult = AddPropertyToBag(
		State->Parameters.Parameters,
		Params->GetStringField(TEXT("name")),
		Params->GetStringField(TEXT("type")),
		ValueType, Err);
	if (!AddResult) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);
	return AddResult;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleRemoveStateParameter(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state")) || !Params->HasField(TEXT("name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field(s): asset_path, state, name"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Params->GetStringField(TEXT("state")), Err);
	if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	State->Modify();
	State->Parameters.Parameters.RemovePropertyByName(FName(*Params->GetStringField(TEXT("name"))));

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleSetStateParameterValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state"))
		|| !Params->HasField(TEXT("name")) || !Params->HasField(TEXT("value")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, state, name, value"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Params->GetStringField(TEXT("state")), Err);
	if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	State->Modify();
	FString ApplyErr;
	const bool bApplied = ApplyJsonToBag(State->Parameters.Parameters,
		FName(*Params->GetStringField(TEXT("name"))),
		Params->TryGetField(TEXT("value")),
		ApplyErr);
	if (!bApplied) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ApplyErr); }

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddFunctionBinding(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("function_struct"))
		|| !Params->HasField(TEXT("target_struct_id")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, function_struct, target_struct_id"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	UScriptStruct* FunctionStruct = ResolveScriptStruct(Params->GetStringField(TEXT("function_struct")));
	if (!FunctionStruct)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Could not resolve function_struct"));
	}

	FGuid TargetID;
	if (!FGuid::Parse(Params->GetStringField(TEXT("target_struct_id")), TargetID))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("target_struct_id must be a GUID"));
	}
	const FString TargetPathStr = Params->HasField(TEXT("target_path")) ? Params->GetStringField(TEXT("target_path")) : FString();
	FPropertyBindingPath TargetPath;
	if (!BuildBindingPath(TargetID, TargetPathStr, TargetPath, Err))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);
	}

	// source_path_segments: array of FName strings for stepping into the function's output struct.
	TArray<FPropertyBindingPathSegment> Segments;
	if (Params->HasField(TEXT("source_path_segments")))
	{
		const TArray<TSharedPtr<FJsonValue>>& SegArr = Params->GetArrayField(TEXT("source_path_segments"));
		for (const TSharedPtr<FJsonValue>& V : SegArr)
		{
			if (V.IsValid() && V->Type == EJson::String)
			{
				Segments.Emplace(FName(*V->AsString()));
			}
		}
	}

	Data->Modify();
	FStateTreeEditorPropertyBindings* Bindings = Data->GetPropertyEditorBindings();
	if (!Bindings)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor bindings on EditorData"));
	}
	const FPropertyBindingPath Result = Bindings->AddFunctionBinding(FunctionStruct, Segments, TargetPath);

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("source_struct_id"), Result.GetStructID().ToString(EGuidFormats::DigitsWithHyphens));
	Out->SetStringField(TEXT("source_path"), Result.ToString());
	return Out;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleAddOutputBinding(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path"))
		|| !Params->HasField(TEXT("source_struct_id")) || !Params->HasField(TEXT("target_struct_id")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, source_struct_id, target_struct_id"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	FGuid SourceID, TargetID;
	if (!FGuid::Parse(Params->GetStringField(TEXT("source_struct_id")), SourceID)
		|| !FGuid::Parse(Params->GetStringField(TEXT("target_struct_id")), TargetID))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("struct_id parameters must be GUIDs"));
	}
	const FString SourcePathStr = Params->HasField(TEXT("source_path")) ? Params->GetStringField(TEXT("source_path")) : FString();
	const FString TargetPathStr = Params->HasField(TEXT("target_path")) ? Params->GetStringField(TEXT("target_path")) : FString();

	FPropertyBindingPath SourcePath, TargetPath;
	if (!BuildBindingPath(SourceID, SourcePathStr, SourcePath, Err)) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	if (!BuildBindingPath(TargetID, TargetPathStr, TargetPath, Err)) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	Data->Modify();
	FStateTreeEditorPropertyBindings* Bindings = Data->GetPropertyEditorBindings();
	if (!Bindings) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor bindings on EditorData")); }
	Bindings->AddOutputBinding(SourcePath, TargetPath);

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	return Out;
}

// ============================================================================
// Phase 6: Schema introspection, linked states, parameter overrides
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleListSchemaContextData(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("schema_class")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: schema_class"));
	}
	const FString SchemaName = Params->GetStringField(TEXT("schema_class"));
	UClass* SchemaClass = FEpicUnrealMCPCommonUtils::ResolveClass(SchemaName);
	if (!SchemaClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Could not resolve schema class '%s'"), *SchemaName));
	}
	if (!SchemaClass->IsChildOf(UStateTreeSchema::StaticClass()))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("'%s' is not a UStateTreeSchema subclass"), *SchemaClass->GetName()));
	}

	// Access the CDO to query virtual overrides without instantiating.
	const UStateTreeSchema* CDO = SchemaClass->GetDefaultObject<UStateTreeSchema>();
	if (!CDO) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get schema CDO")); }

	TArray<TSharedPtr<FJsonValue>> ContextJson;
	for (const FStateTreeExternalDataDesc& Desc : CDO->GetContextDataDescs())
	{
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("name"), Desc.Name.ToString());
		Entry->SetStringField(TEXT("struct"), Desc.Struct ? Desc.Struct->GetName() : TEXT(""));
		Entry->SetStringField(TEXT("struct_path"), Desc.Struct ? Desc.Struct->GetPathName() : TEXT(""));
		ContextJson.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("schema_class"), SchemaClass->GetName());
	Result->SetStringField(TEXT("schema_path"), SchemaClass->GetPathName());
	Result->SetArrayField(TEXT("context_data"), ContextJson);

	// Capability flags — gated on WITH_EDITOR but we're always in an editor build here.
	TSharedPtr<FJsonObject> Allows = MakeShareable(new FJsonObject);
	Allows->SetBoolField(TEXT("enter_conditions"), CDO->AllowEnterConditions());
	Allows->SetBoolField(TEXT("utility_considerations"), CDO->AllowUtilityConsiderations());
	Allows->SetBoolField(TEXT("evaluators"), CDO->AllowEvaluators());
	Allows->SetBoolField(TEXT("multiple_tasks"), CDO->AllowMultipleTasks());
	Allows->SetBoolField(TEXT("global_parameters"), CDO->AllowGlobalParameters());
	Allows->SetBoolField(TEXT("tasks_completion"), CDO->AllowTasksCompletion());
	Allows->SetBoolField(TEXT("scheduled_tick"), CDO->IsScheduledTickAllowed());
	Result->SetObjectField(TEXT("allows"), Allows);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleSetLinkedState(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field(s): asset_path, state"));
	}
	const bool bHasSubtree = Params->HasField(TEXT("target_subtree"));
	const bool bHasAsset = Params->HasField(TEXT("target_asset_path"));
	const bool bClear = Params->HasField(TEXT("clear")) && Params->GetBoolField(TEXT("clear"));
	if (!bClear && !bHasSubtree && !bHasAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Supply one of: target_subtree, target_asset_path, or clear=true"));
	}
	if (bHasSubtree && bHasAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("target_subtree and target_asset_path are mutually exclusive"));
	}

	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Params->GetStringField(TEXT("state")), Err);
	if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	State->Modify();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	if (bClear)
	{
		State->Type = EStateTreeStateType::State;
		State->LinkedAsset = nullptr;
		State->LinkedSubtree = FStateTreeStateLink();
		State->Parameters.ResetParametersAndOverrides();
	}
	else if (bHasSubtree)
	{
		UStateTreeState* Target = ResolveState(Data, Params->GetStringField(TEXT("target_subtree")), Err);
		if (!Target) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
		if (Target->Type != EStateTreeStateType::Subtree)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Target state '%s' is not a Subtree"), *Target->Name.ToString()));
		}
		FStateTreeStateLink Link;
#if WITH_EDITORONLY_DATA
		Link.LinkType = EStateTreeTransitionType::GotoState;
		Link.Name = Target->Name;
		Link.ID = Target->ID;
#endif
		State->Type = EStateTreeStateType::Linked;
		State->SetLinkedState(Link);
		Result->SetStringField(TEXT("target_subtree_id"), Target->ID.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else // bHasAsset
	{
		const FString TargetPath = Params->GetStringField(TEXT("target_asset_path"));
		UObject* Loaded = FEpicUnrealMCPCommonUtils::ResolveAsset(TargetPath);
		UStateTree* LinkedAsset = Cast<UStateTree>(Loaded);
		if (!LinkedAsset)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Could not load UStateTree from '%s'"), *TargetPath));
		}
		if (LinkedAsset == Tree)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot link a StateTree to itself"));
		}
		State->Type = EStateTreeStateType::LinkedAsset;
		State->SetLinkedStateAsset(LinkedAsset);
		Result->SetStringField(TEXT("target_asset_path"), LinkedAsset->GetPathName());
	}

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state_id"), State->ID.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("state_type"), UEnum::GetDisplayValueAsText(State->Type).ToString());
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStateTreeCommands::HandleSetStateParameterOverride(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("state"))
		|| !Params->HasField(TEXT("parameter_name")) || !Params->HasField(TEXT("overridden")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): asset_path, state, parameter_name, overridden"));
	}
	FString Err;
	UStateTree* Tree = LoadStateTree(Params->GetStringField(TEXT("asset_path")), Err);
	if (!Tree) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeEditorData* Data = GetEditorData(Tree, Err);
	if (!Data) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UStateTreeState* State = ResolveState(Data, Params->GetStringField(TEXT("state")), Err);
	if (!State) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FName ParamName(*Params->GetStringField(TEXT("parameter_name")));
	const bool bOverridden = Params->GetBoolField(TEXT("overridden"));

	// Look up the parameter's FGuid in the state's parameter bag.
	const UPropertyBag* BagStruct = State->Parameters.Parameters.GetPropertyBagStruct();
	if (!BagStruct)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State '%s' has no parameters"), *State->Name.ToString()));
	}
	const FPropertyBagPropertyDesc* Desc = BagStruct->GetPropertyDescs().FindByPredicate(
		[ParamName](const FPropertyBagPropertyDesc& D) { return D.Name == ParamName; });
	if (!Desc)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Parameter '%s' not found in state '%s'"),
				*ParamName.ToString(), *State->Name.ToString()));
	}

	State->Modify();
	State->SetParametersPropertyOverridden(Desc->ID, bOverridden);

	const bool bAutoCompile = Params->HasField(TEXT("auto_compile")) && Params->GetBoolField(TEXT("auto_compile"));
	FinalizeEdit(Tree, bAutoCompile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("parameter_name"), ParamName.ToString());
	Result->SetStringField(TEXT("parameter_id"), Desc->ID.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetBoolField(TEXT("overridden"), bOverridden);
	return Result;
}
