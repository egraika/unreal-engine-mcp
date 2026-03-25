#include "Commands/EpicUnrealMCPAnimBlueprintCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Factories/AnimBlueprintFactory.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"

// AnimGraph node includes
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"

// AnimGraph node types for add_anim_node command
#include "AnimGraphNode_CopyPoseFromMesh.h"
#include "AnimGraphNode_AnimDynamics.h"

// ============================================================================
// Command Dispatcher
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleCommand(
	const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	// Phase 1: Lifecycle
	if (CommandType == TEXT("create_anim_blueprint"))
	{
		return HandleCreateAnimBlueprint(Params);
	}
	else if (CommandType == TEXT("read_anim_blueprint"))
	{
		return HandleReadAnimBlueprint(Params);
	}
	// Phase 2: State Machines
	else if (CommandType == TEXT("add_state_machine"))
	{
		return HandleAddStateMachine(Params);
	}
	else if (CommandType == TEXT("add_state"))
	{
		return HandleAddState(Params);
	}
	else if (CommandType == TEXT("add_transition"))
	{
		return HandleAddTransition(Params);
	}
	else if (CommandType == TEXT("set_default_state"))
	{
		return HandleSetDefaultState(Params);
	}
	else if (CommandType == TEXT("set_state_animation"))
	{
		return HandleSetStateAnimation(Params);
	}
	else if (CommandType == TEXT("set_transition_rule"))
	{
		return HandleSetTransitionRule(Params);
	}
	// Phase 3: Advanced
	else if (CommandType == TEXT("add_blend_space_player"))
	{
		return HandleAddBlendSpacePlayer(Params);
	}
	else if (CommandType == TEXT("connect_anim_nodes"))
	{
		return HandleConnectAnimNodes(Params);
	}
	else if (CommandType == TEXT("get_state_machine_info"))
	{
		return HandleGetStateMachineInfo(Params);
	}
	// Phase 4: AnimGraph Nodes
	else if (CommandType == TEXT("add_anim_node"))
	{
		return HandleAddAnimNode(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown AnimBlueprint command: %s"), *CommandType));
}

// ============================================================================
// Helpers
// ============================================================================

UAnimBlueprint* FEpicUnrealMCPAnimBlueprintCommands::LoadAnimBlueprint(
	const FString& NameOrPath, FString& OutError)
{
	// Try as asset path first
	FString AssetPath = NameOrPath;
	if (!AssetPath.StartsWith(TEXT("/")))
	{
		// Try common locations
		TArray<FString> SearchPaths = {
			FString::Printf(TEXT("/Game/AnimBlueprints/%s"), *NameOrPath),
			FString::Printf(TEXT("/Game/Blueprints/%s"), *NameOrPath),
			FString::Printf(TEXT("/Game/%s"), *NameOrPath)
		};

		for (const FString& Path : SearchPaths)
		{
			if (UEditorAssetLibrary::DoesAssetExist(Path))
			{
				AssetPath = Path;
				break;
			}
		}
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		OutError = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
		return nullptr;
	}

	UAnimBlueprint* ABP = Cast<UAnimBlueprint>(LoadedAsset);
	if (!ABP)
	{
		OutError = FString::Printf(TEXT("Asset is not an AnimBlueprint: %s (is %s)"),
			*AssetPath, *LoadedAsset->GetClass()->GetName());
		return nullptr;
	}

	return ABP;
}

UEdGraph* FEpicUnrealMCPAnimBlueprintCommands::FindAnimGraph(UAnimBlueprint* ABP)
{
	if (!ABP) return nullptr;

	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName("AnimGraph"))
		{
			return Graph;
		}
	}
	return nullptr;
}

UAnimGraphNode_StateMachine* FEpicUnrealMCPAnimBlueprintCommands::FindStateMachineByName(
	UAnimBlueprint* ABP, const FString& Name)
{
	UEdGraph* AnimGraph = FindAnimGraph(ABP);
	if (!AnimGraph) return nullptr;

	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
		if (SMNode && SMNode->EditorStateMachineGraph)
		{
			FString SMName = SMNode->GetStateMachineName();
			if (SMName == Name || SMNode->EditorStateMachineGraph->GetName() == Name)
			{
				return SMNode;
			}
		}
	}
	return nullptr;
}

UAnimStateNode* FEpicUnrealMCPAnimBlueprintCommands::FindStateByName(
	UAnimationStateMachineGraph* SMGraph, const FString& Name)
{
	if (!SMGraph) return nullptr;

	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
		if (StateNode && StateNode->GetStateName() == Name)
		{
			return StateNode;
		}
	}
	return nullptr;
}

// ============================================================================
// Phase 1: create_anim_blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleCreateAnimBlueprint(
	const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
	}

	FString SkeletonPath;
	if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'skeleton_path' parameter"));
	}

	FString PackagePath = TEXT("/Game/AnimBlueprints/");
	Params->TryGetStringField(TEXT("path"), PackagePath);
	if (!PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath += TEXT("/");
	}

	// Check if already exists
	FString FullPath = PackagePath + Name;
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("AnimBlueprint already exists: %s"), *FullPath));
	}

	// Load skeleton
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load skeleton: %s"), *SkeletonPath));
	}

	// Resolve parent class (default: UAnimInstance)
	TSubclassOf<UAnimInstance> ParentClass = UAnimInstance::StaticClass();
	FString ParentClassName;
	if (Params->TryGetStringField(TEXT("parent_class"), ParentClassName) && !ParentClassName.IsEmpty())
	{
		FString ClassName = ParentClassName;
		if (!ClassName.StartsWith(TEXT("U")))
		{
			ClassName = TEXT("U") + ClassName;
		}

		// Try common paths
		UClass* FoundClass = nullptr;
		TArray<FString> ClassPaths = {
			FString::Printf(TEXT("/Script/Engine.%s"), *ClassName),
			FString::Printf(TEXT("/Script/PRK.%s"), *ClassName),
			FString::Printf(TEXT("/Script/GMCExtended.%s"), *ClassName)
		};

		for (const FString& ClassPath : ClassPaths)
		{
			FoundClass = LoadClass<UAnimInstance>(nullptr, *ClassPath);
			if (FoundClass) break;
		}

		if (FoundClass)
		{
			ParentClass = FoundClass;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Could not find parent class '%s', defaulting to UAnimInstance"), *ParentClassName);
		}
	}

	// Create factory
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->ParentClass = ParentClass;
	Factory->TargetSkeleton = Skeleton;

	// Optional preview mesh
	FString PreviewMeshPath;
	if (Params->TryGetStringField(TEXT("preview_mesh"), PreviewMeshPath) && !PreviewMeshPath.IsEmpty())
	{
		USkeletalMesh* PreviewMesh = LoadObject<USkeletalMesh>(nullptr, *PreviewMeshPath);
		if (PreviewMesh)
		{
			Factory->PreviewSkeletalMesh = PreviewMesh;
		}
	}

	// Create the AnimBlueprint
	UPackage* Package = CreatePackage(*FullPath);
	UAnimBlueprint* NewABP = Cast<UAnimBlueprint>(
		Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package, *Name,
			RF_Standalone | RF_Public, nullptr, GWarn));

	if (!NewABP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create AnimBlueprint"));
	}

	FAssetRegistryModule::AssetCreated(NewABP);
	Package->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("path"), FullPath);
	Result->SetStringField(TEXT("skeleton"), SkeletonPath);
	Result->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	return Result;
}

// ============================================================================
// Phase 1: read_anim_blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleReadAnimBlueprint(
	const TSharedPtr<FJsonObject>& Params)
{
	FString NameOrPath;
	if (!Params->TryGetStringField(TEXT("name"), NameOrPath))
	{
		Params->TryGetStringField(TEXT("asset_path"), NameOrPath);
	}
	if (NameOrPath.IsEmpty())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' or 'asset_path' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(NameOrPath, Error);
	if (!ABP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), ABP->GetName());
	Result->SetStringField(TEXT("path"), ABP->GetPathName());

	// Skeleton
	if (ABP->TargetSkeleton)
	{
		Result->SetStringField(TEXT("skeleton"), ABP->TargetSkeleton->GetPathName());
	}

	// Parent class
	if (ABP->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), ABP->ParentClass->GetName());
	}

	// Variables
	TArray<TSharedPtr<FJsonValue>> VarsArray;
	for (const FBPVariableDescription& Var : ABP->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject);
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		VarsArray.Add(MakeShareable(new FJsonValueObject(VarObj)));
	}
	Result->SetArrayField(TEXT("variables"), VarsArray);

	// State machines
	TArray<TSharedPtr<FJsonValue>> SMArray;
	UEdGraph* AnimGraph = FindAnimGraph(ABP);
	if (AnimGraph)
	{
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
			if (SMNode && SMNode->EditorStateMachineGraph)
			{
				TSharedPtr<FJsonObject> SMObj = MakeShareable(new FJsonObject);
				SMObj->SetStringField(TEXT("name"), SMNode->GetStateMachineName());

				// List states
				TArray<TSharedPtr<FJsonValue>> StatesArray;
				UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;
				for (UEdGraphNode* SMChild : SMGraph->Nodes)
				{
					UAnimStateNode* StateNode = Cast<UAnimStateNode>(SMChild);
					if (StateNode)
					{
						StatesArray.Add(MakeShareable(new FJsonValueString(StateNode->GetStateName())));
					}
				}
				SMObj->SetArrayField(TEXT("states"), StatesArray);

				// Default state (connected to entry)
				if (SMGraph->EntryNode)
				{
					UEdGraphNode* DefaultNode = SMGraph->EntryNode->GetOutputNode();
					UAnimStateNode* DefaultState = Cast<UAnimStateNode>(DefaultNode);
					if (DefaultState)
					{
						SMObj->SetStringField(TEXT("default_state"), DefaultState->GetStateName());
					}
				}

				// List transitions
				TArray<TSharedPtr<FJsonValue>> TransArray;
				for (UEdGraphNode* SMChild : SMGraph->Nodes)
				{
					UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(SMChild);
					if (TransNode)
					{
						TSharedPtr<FJsonObject> TransObj = MakeShareable(new FJsonObject);
						UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
						UAnimStateNodeBase* NextState = TransNode->GetNextState();
						if (PrevState) TransObj->SetStringField(TEXT("from"), PrevState->GetStateName());
						if (NextState) TransObj->SetStringField(TEXT("to"), NextState->GetStateName());
						TransObj->SetNumberField(TEXT("crossfade_duration"), TransNode->CrossfadeDuration);
						TransObj->SetNumberField(TEXT("priority"), TransNode->PriorityOrder);
						TransObj->SetBoolField(TEXT("automatic_rule"), TransNode->bAutomaticRuleBasedOnSequencePlayerInState);
						TransArray.Add(MakeShareable(new FJsonValueObject(TransObj)));
					}
				}
				SMObj->SetArrayField(TEXT("transitions"), TransArray);

				SMArray.Add(MakeShareable(new FJsonValueObject(SMObj)));
			}
		}
	}
	Result->SetArrayField(TEXT("state_machines"), SMArray);

	// AnimGraph nodes (all types: Output Pose, CopyPoseFromMesh, AnimDynamics, etc.)
	TArray<TSharedPtr<FJsonValue>> AnimNodesArray;
	if (AnimGraph)
	{
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			// Skip state machines (already reported above)
			if (Cast<UAnimGraphNode_StateMachine>(Node)) continue;

			TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
			NodeObj->SetStringField(TEXT("node_id"), FString::FromInt(Node->GetUniqueID()));
			NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
			NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

			// List pins with connection info
			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
				PinObj->SetStringField(TEXT("name"), Pin->GetName());
				PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Output ? TEXT("output") : TEXT("input"));
				PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
				PinObj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
				if (Pin->LinkedTo.Num() > 0)
				{
					TArray<TSharedPtr<FJsonValue>> LinkedArray;
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (LinkedPin && LinkedPin->GetOwningNode())
						{
							LinkedArray.Add(MakeShareable(new FJsonValueString(
								FString::FromInt(LinkedPin->GetOwningNode()->GetUniqueID()))));
						}
					}
					PinObj->SetArrayField(TEXT("linked_to_nodes"), LinkedArray);
				}
				PinsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
			}
			NodeObj->SetArrayField(TEXT("pins"), PinsArray);

			AnimNodesArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
		}
		Result->SetNumberField(TEXT("anim_graph_node_count"), AnimGraph->Nodes.Num());
	}
	Result->SetArrayField(TEXT("anim_graph_nodes"), AnimNodesArray);

	return Result;
}

// ============================================================================
// Phase 2: add_state_machine
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleAddStateMachine(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	UEdGraph* AnimGraph = FindAnimGraph(ABP);
	if (!AnimGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("AnimGraph not found in AnimBlueprint"));
	}

	FString SMName = TEXT("New State Machine");
	Params->TryGetStringField(TEXT("name"), SMName);

	float PosX = 200.0f, PosY = 0.0f;
	if (Params->HasField(TEXT("pos_x"))) PosX = Params->GetNumberField(TEXT("pos_x"));
	if (Params->HasField(TEXT("pos_y"))) PosY = Params->GetNumberField(TEXT("pos_y"));

	// Create state machine node
	UAnimGraphNode_StateMachine* SMNode = NewObject<UAnimGraphNode_StateMachine>(AnimGraph);
	SMNode->NodePosX = static_cast<int32>(PosX);
	SMNode->NodePosY = static_cast<int32>(PosY);
	AnimGraph->AddNode(SMNode, /*bIsFromUI=*/false, /*bSelectNewNode=*/false);
	SMNode->PostPlacedNewNode();
	SMNode->AllocateDefaultPins();

	// Rename the state machine
	if (SMNode->EditorStateMachineGraph)
	{
		TSharedPtr<INameValidatorInterface> NameValidator = SMNode->MakeNameValidator();
		FBlueprintEditorUtils::RenameGraphWithSuggestion(SMNode->EditorStateMachineGraph, NameValidator, SMName);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("name"), SMNode->GetStateMachineName());
	Result->SetStringField(TEXT("node_id"), FString::FromInt(SMNode->GetUniqueID()));
	Result->SetNumberField(TEXT("pos_x"), SMNode->NodePosX);
	Result->SetNumberField(TEXT("pos_y"), SMNode->NodePosY);
	return Result;
}

// ============================================================================
// Phase 2: add_state
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleAddState(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString SMName;
	if (!Params->TryGetStringField(TEXT("state_machine_name"), SMName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
	}

	FString StateName;
	if (!Params->TryGetStringField(TEXT("state_name"), StateName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineByName(ABP, SMName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;

	// Check if state already exists
	if (FindStateByName(SMGraph, StateName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State '%s' already exists in '%s'"), *StateName, *SMName));
	}

	float PosX = 300.0f, PosY = 0.0f;
	if (Params->HasField(TEXT("pos_x"))) PosX = Params->GetNumberField(TEXT("pos_x"));
	if (Params->HasField(TEXT("pos_y"))) PosY = Params->GetNumberField(TEXT("pos_y"));

	// Create state node
	UAnimStateNode* StateNode = NewObject<UAnimStateNode>(SMGraph);
	StateNode->NodePosX = static_cast<int32>(PosX);
	StateNode->NodePosY = static_cast<int32>(PosY);
	SMGraph->AddNode(StateNode, /*bIsFromUI=*/false, /*bSelectNewNode=*/false);
	StateNode->PostPlacedNewNode();
	StateNode->AllocateDefaultPins();

	// Rename the state
	StateNode->OnRenameNode(StateName);

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("state_name"), StateNode->GetStateName());
	Result->SetStringField(TEXT("node_id"), FString::FromInt(StateNode->GetUniqueID()));
	Result->SetStringField(TEXT("state_machine"), SMName);
	return Result;
}

// ============================================================================
// Phase 2: add_transition
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleAddTransition(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString SMName;
	if (!Params->TryGetStringField(TEXT("state_machine_name"), SMName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
	}

	FString FromStateName, ToStateName;
	if (!Params->TryGetStringField(TEXT("from_state"), FromStateName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'from_state' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("to_state"), ToStateName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'to_state' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineByName(ABP, SMName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;

	UAnimStateNode* FromState = FindStateByName(SMGraph, FromStateName);
	if (!FromState)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State '%s' not found"), *FromStateName));
	}

	UAnimStateNode* ToState = FindStateByName(SMGraph, ToStateName);
	if (!ToState)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State '%s' not found"), *ToStateName));
	}

	// Create transition node
	UAnimStateTransitionNode* TransNode = NewObject<UAnimStateTransitionNode>(SMGraph);
	// Position between the two states
	TransNode->NodePosX = (FromState->NodePosX + ToState->NodePosX) / 2;
	TransNode->NodePosY = (FromState->NodePosY + ToState->NodePosY) / 2;
	SMGraph->AddNode(TransNode, /*bIsFromUI=*/false, /*bSelectNewNode=*/false);
	TransNode->PostPlacedNewNode();
	TransNode->AllocateDefaultPins();

	// Wire the transition between states
	TransNode->CreateConnections(FromState, ToState);

	// Set optional properties
	if (Params->HasField(TEXT("crossfade_duration")))
	{
		TransNode->CrossfadeDuration = static_cast<float>(Params->GetNumberField(TEXT("crossfade_duration")));
	}
	if (Params->HasField(TEXT("priority")))
	{
		TransNode->PriorityOrder = static_cast<int32>(Params->GetNumberField(TEXT("priority")));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("from_state"), FromStateName);
	Result->SetStringField(TEXT("to_state"), ToStateName);
	Result->SetStringField(TEXT("node_id"), FString::FromInt(TransNode->GetUniqueID()));
	Result->SetNumberField(TEXT("crossfade_duration"), TransNode->CrossfadeDuration);
	Result->SetNumberField(TEXT("priority"), TransNode->PriorityOrder);
	return Result;
}

// ============================================================================
// Phase 2: set_default_state
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleSetDefaultState(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString SMName;
	if (!Params->TryGetStringField(TEXT("state_machine_name"), SMName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
	}

	FString StateName;
	if (!Params->TryGetStringField(TEXT("state_name"), StateName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineByName(ABP, SMName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;

	UAnimStateNode* TargetState = FindStateByName(SMGraph, StateName);
	if (!TargetState)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State '%s' not found"), *StateName));
	}

	// Wire entry node to target state
	UAnimStateEntryNode* EntryNode = SMGraph->EntryNode;
	if (!EntryNode)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Entry node not found in state machine"));
	}

	UEdGraphPin* EntryOut = EntryNode->GetOutputPin();
	if (EntryOut)
	{
		EntryOut->BreakAllPinLinks();
		UEdGraphPin* StateIn = TargetState->GetInputPin();
		if (StateIn)
		{
			EntryOut->MakeLinkTo(StateIn);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("default_state"), StateName);
	Result->SetStringField(TEXT("state_machine"), SMName);
	return Result;
}

// ============================================================================
// Phase 2: set_state_animation
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleSetStateAnimation(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString SMName;
	if (!Params->TryGetStringField(TEXT("state_machine_name"), SMName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
	}

	FString StateName;
	if (!Params->TryGetStringField(TEXT("state_name"), StateName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
	}

	FString AnimPath;
	if (!Params->TryGetStringField(TEXT("animation_path"), AnimPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'animation_path' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineByName(ABP, SMName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimStateNode* StateNode = FindStateByName(SMNode->EditorStateMachineGraph, StateName);
	if (!StateNode)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State '%s' not found"), *StateName));
	}

	if (!StateNode->BoundGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State '%s' has no BoundGraph"), *StateName));
	}

	// Load animation asset
	UAnimSequenceBase* AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);
	if (!AnimAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load animation: %s"), *AnimPath));
	}

	UEdGraph* StateGraph = StateNode->BoundGraph;

	// Create sequence player node
	UAnimGraphNode_SequencePlayer* SeqPlayer = NewObject<UAnimGraphNode_SequencePlayer>(StateGraph);
	SeqPlayer->NodePosX = -300;
	SeqPlayer->NodePosY = 0;
	StateGraph->AddNode(SeqPlayer, /*bIsFromUI=*/false, /*bSelectNewNode=*/false);
	SeqPlayer->SetAnimationAsset(AnimAsset);
	SeqPlayer->AllocateDefaultPins();

	// Find the StateResult node and connect pose pins
	bool bConnected = false;
	UAnimGraphNode_StateResult* ResultNode = StateNode->GetResultNodeInsideState();
	if (ResultNode)
	{
		// Find pose output on sequence player and input on result
		UEdGraphPin* PoseOut = nullptr;
		for (UEdGraphPin* Pin : SeqPlayer->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				PoseOut = Pin;
				break;
			}
		}

		UEdGraphPin* ResultIn = nullptr;
		for (UEdGraphPin* Pin : ResultNode->Pins)
		{
			if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				ResultIn = Pin;
				break;
			}
		}

		if (PoseOut && ResultIn)
		{
			PoseOut->MakeLinkTo(ResultIn);
			bConnected = true;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Could not auto-connect pose pins. PoseOut=%s, ResultIn=%s"),
				PoseOut ? TEXT("found") : TEXT("null"),
				ResultIn ? TEXT("found") : TEXT("null"));
		}
	}

	// Handle loop option
	bool bLoop = true;
	if (Params->HasField(TEXT("loop")))
	{
		bLoop = Params->GetBoolField(TEXT("loop"));
	}
	SeqPlayer->Node.SetLoopAnimation(bLoop);

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("state"), StateName);
	Result->SetStringField(TEXT("animation"), AnimPath);
	Result->SetStringField(TEXT("node_id"), FString::FromInt(SeqPlayer->GetUniqueID()));
	Result->SetBoolField(TEXT("loop"), bLoop);
	Result->SetBoolField(TEXT("connected"), bConnected);
	return Result;
}

// ============================================================================
// Phase 2: set_transition_rule
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleSetTransitionRule(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString SMName;
	if (!Params->TryGetStringField(TEXT("state_machine_name"), SMName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
	}

	FString FromState, ToState;
	if (!Params->TryGetStringField(TEXT("from_state"), FromState))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'from_state' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("to_state"), ToState))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'to_state' parameter"));
	}

	FString RuleType = TEXT("automatic");
	Params->TryGetStringField(TEXT("rule_type"), RuleType);

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineByName(ABP, SMName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	// Find the transition between from_state and to_state
	UAnimStateTransitionNode* FoundTransition = nullptr;
	for (UEdGraphNode* Node : SMNode->EditorStateMachineGraph->Nodes)
	{
		UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node);
		if (TransNode)
		{
			UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
			UAnimStateNodeBase* NextState = TransNode->GetNextState();
			if (PrevState && NextState &&
				PrevState->GetStateName() == FromState &&
				NextState->GetStateName() == ToState)
			{
				FoundTransition = TransNode;
				break;
			}
		}
	}

	if (!FoundTransition)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Transition from '%s' to '%s' not found"), *FromState, *ToState));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	if (RuleType == TEXT("automatic"))
	{
		FoundTransition->bAutomaticRuleBasedOnSequencePlayerInState = true;

		float TriggerTime = -1.0f;
		if (Params->HasField(TEXT("automatic_rule_trigger_time")))
		{
			TriggerTime = static_cast<float>(Params->GetNumberField(TEXT("automatic_rule_trigger_time")));
		}
		FoundTransition->AutomaticRuleTriggerTime = TriggerTime;

		Result->SetStringField(TEXT("rule_type"), TEXT("automatic"));
		Result->SetNumberField(TEXT("trigger_time"), TriggerTime);
	}
	else if (RuleType == TEXT("time_remaining"))
	{
		FoundTransition->bAutomaticRuleBasedOnSequencePlayerInState = true;

		float TriggerTime = 0.0f;
		if (Params->HasField(TEXT("trigger_time")))
		{
			TriggerTime = static_cast<float>(Params->GetNumberField(TEXT("trigger_time")));
		}
		FoundTransition->AutomaticRuleTriggerTime = TriggerTime;

		Result->SetStringField(TEXT("rule_type"), TEXT("time_remaining"));
		Result->SetNumberField(TEXT("trigger_time"), TriggerTime);
	}
	else
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unsupported rule_type: %s. Use 'automatic' or 'time_remaining'"), *RuleType));
	}

	if (Params->HasField(TEXT("crossfade_duration")))
	{
		FoundTransition->CrossfadeDuration = static_cast<float>(Params->GetNumberField(TEXT("crossfade_duration")));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	Result->SetStringField(TEXT("from_state"), FromState);
	Result->SetStringField(TEXT("to_state"), ToState);
	Result->SetNumberField(TEXT("crossfade_duration"), FoundTransition->CrossfadeDuration);
	return Result;
}

// ============================================================================
// Phase 3: add_blend_space_player
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleAddBlendSpacePlayer(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString SMName;
	if (!Params->TryGetStringField(TEXT("state_machine_name"), SMName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
	}

	FString StateName;
	if (!Params->TryGetStringField(TEXT("state_name"), StateName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
	}

	FString BlendSpacePath;
	if (!Params->TryGetStringField(TEXT("blend_space_path"), BlendSpacePath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blend_space_path' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineByName(ABP, SMName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimStateNode* StateNode = FindStateByName(SMNode->EditorStateMachineGraph, StateName);
	if (!StateNode || !StateNode->BoundGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State '%s' not found or has no graph"), *StateName));
	}

	// Load blend space asset
	UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *BlendSpacePath);
	if (!BlendSpace)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load blend space: %s"), *BlendSpacePath));
	}

	UEdGraph* StateGraph = StateNode->BoundGraph;

	// Create blend space player node
	UAnimGraphNode_BlendSpacePlayer* BSPlayer = NewObject<UAnimGraphNode_BlendSpacePlayer>(StateGraph);
	BSPlayer->NodePosX = -300;
	BSPlayer->NodePosY = 0;
	StateGraph->AddNode(BSPlayer, /*bIsFromUI=*/false, /*bSelectNewNode=*/false);
	BSPlayer->SetAnimationAsset(BlendSpace);
	BSPlayer->AllocateDefaultPins();

	// Connect to StateResult
	UAnimGraphNode_StateResult* ResultNode = StateNode->GetResultNodeInsideState();
	if (ResultNode)
	{
		UEdGraphPin* PoseOut = nullptr;
		for (UEdGraphPin* Pin : BSPlayer->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				PoseOut = Pin;
				break;
			}
		}

		UEdGraphPin* ResultIn = nullptr;
		for (UEdGraphPin* Pin : ResultNode->Pins)
		{
			if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				ResultIn = Pin;
				break;
			}
		}

		if (PoseOut && ResultIn)
		{
			ResultIn->BreakAllPinLinks();
			PoseOut->MakeLinkTo(ResultIn);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("state"), StateName);
	Result->SetStringField(TEXT("blend_space"), BlendSpacePath);
	Result->SetStringField(TEXT("node_id"), FString::FromInt(BSPlayer->GetUniqueID()));
	return Result;
}

// ============================================================================
// Phase 3: connect_anim_nodes
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleConnectAnimNodes(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString SourceNodeId, TargetNodeId;
	if (!Params->TryGetStringField(TEXT("source_node_id"), SourceNodeId))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_node_id' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("target_node_id"), TargetNodeId))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_node_id' parameter"));
	}

	FString SourcePinName = TEXT("Pose");
	FString TargetPinName = TEXT("Result");
	Params->TryGetStringField(TEXT("source_pin"), SourcePinName);
	Params->TryGetStringField(TEXT("target_pin"), TargetPinName);

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	int32 SrcId = FCString::Atoi(*SourceNodeId);
	int32 TgtId = FCString::Atoi(*TargetNodeId);

	// Search all graphs for the nodes
	UEdGraphNode* SourceNode = nullptr;
	UEdGraphNode* TargetNode = nullptr;

	TArray<UEdGraph*> AllGraphs;
	ABP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->GetUniqueID() == SrcId) SourceNode = Node;
			if (Node->GetUniqueID() == TgtId) TargetNode = Node;
			if (SourceNode && TargetNode) break;
		}
		if (SourceNode && TargetNode) break;
	}

	if (!SourceNode)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Source node with id '%s' not found"), *SourceNodeId));
	}
	if (!TargetNode)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Target node with id '%s' not found"), *TargetNodeId));
	}

	// Find pins
	UEdGraphPin* SourcePin = FEpicUnrealMCPCommonUtils::FindPin(SourceNode, SourcePinName, EGPD_Output);
	UEdGraphPin* TargetPin = FEpicUnrealMCPCommonUtils::FindPin(TargetNode, TargetPinName, EGPD_Input);

	if (!SourcePin)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Source pin '%s' not found on node %s"), *SourcePinName, *SourceNodeId));
	}
	if (!TargetPin)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Target pin '%s' not found on node %s"), *TargetPinName, *TargetNodeId));
	}

	SourcePin->MakeLinkTo(TargetPin);

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("source_node"), SourceNodeId);
	Result->SetStringField(TEXT("source_pin"), SourcePinName);
	Result->SetStringField(TEXT("target_node"), TargetNodeId);
	Result->SetStringField(TEXT("target_pin"), TargetPinName);
	Result->SetBoolField(TEXT("connected"), true);
	return Result;
}

// ============================================================================
// Phase 3: get_state_machine_info
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleGetStateMachineInfo(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString SMName;
	if (!Params->TryGetStringField(TEXT("state_machine_name"), SMName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineByName(ABP, SMName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), SMNode->GetStateMachineName());

	// Default state
	if (SMGraph->EntryNode)
	{
		UEdGraphNode* DefaultNode = SMGraph->EntryNode->GetOutputNode();
		UAnimStateNode* DefaultState = Cast<UAnimStateNode>(DefaultNode);
		if (DefaultState)
		{
			Result->SetStringField(TEXT("default_state"), DefaultState->GetStateName());
		}
	}

	// States with details
	TArray<TSharedPtr<FJsonValue>> StatesArray;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
		if (!StateNode) continue;

		TSharedPtr<FJsonObject> StateObj = MakeShareable(new FJsonObject);
		StateObj->SetStringField(TEXT("name"), StateNode->GetStateName());
		StateObj->SetStringField(TEXT("node_id"), FString::FromInt(StateNode->GetUniqueID()));
		StateObj->SetNumberField(TEXT("pos_x"), StateNode->NodePosX);
		StateObj->SetNumberField(TEXT("pos_y"), StateNode->NodePosY);

		// Check what's inside the state graph
		if (StateNode->BoundGraph)
		{
			TArray<TSharedPtr<FJsonValue>> InnerNodesArray;
			for (UEdGraphNode* InnerNode : StateNode->BoundGraph->Nodes)
			{
				TSharedPtr<FJsonObject> InnerObj = MakeShareable(new FJsonObject);
				InnerObj->SetStringField(TEXT("class"), InnerNode->GetClass()->GetName());
				InnerObj->SetStringField(TEXT("node_id"), FString::FromInt(InnerNode->GetUniqueID()));

				// Check if it's an asset player with an animation
				if (UAnimGraphNode_SequencePlayer* SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(InnerNode))
				{
					UAnimationAsset* Asset = SeqPlayer->GetAnimationAsset();
					if (Asset)
					{
						InnerObj->SetStringField(TEXT("animation"), Asset->GetPathName());
					}
				}
				else if (UAnimGraphNode_BlendSpacePlayer* BSPlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(InnerNode))
				{
					UAnimationAsset* Asset = BSPlayer->GetAnimationAsset();
					if (Asset)
					{
						InnerObj->SetStringField(TEXT("blend_space"), Asset->GetPathName());
					}
				}

				InnerNodesArray.Add(MakeShareable(new FJsonValueObject(InnerObj)));
			}
			StateObj->SetArrayField(TEXT("graph_nodes"), InnerNodesArray);
		}

		StatesArray.Add(MakeShareable(new FJsonValueObject(StateObj)));
	}
	Result->SetArrayField(TEXT("states"), StatesArray);

	// Transitions with details
	TArray<TSharedPtr<FJsonValue>> TransArray;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node);
		if (!TransNode) continue;

		TSharedPtr<FJsonObject> TransObj = MakeShareable(new FJsonObject);
		UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
		UAnimStateNodeBase* NextState = TransNode->GetNextState();
		if (PrevState) TransObj->SetStringField(TEXT("from"), PrevState->GetStateName());
		if (NextState) TransObj->SetStringField(TEXT("to"), NextState->GetStateName());
		TransObj->SetStringField(TEXT("node_id"), FString::FromInt(TransNode->GetUniqueID()));
		TransObj->SetNumberField(TEXT("crossfade_duration"), TransNode->CrossfadeDuration);
		TransObj->SetNumberField(TEXT("priority"), TransNode->PriorityOrder);
		TransObj->SetBoolField(TEXT("automatic_rule"), TransNode->bAutomaticRuleBasedOnSequencePlayerInState);
		TransObj->SetNumberField(TEXT("automatic_trigger_time"), TransNode->AutomaticRuleTriggerTime);
		TransObj->SetBoolField(TEXT("bidirectional"), TransNode->Bidirectional);
		TransObj->SetBoolField(TEXT("disabled"), TransNode->bDisabled);

		TransArray.Add(MakeShareable(new FJsonValueObject(TransObj)));
	}
	Result->SetArrayField(TEXT("transitions"), TransArray);

	return Result;
}

// ============================================================================
// Phase 4: add_anim_node (CopyPoseFromMesh, AnimDynamics)
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleAddAnimNode(
	const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString NodeType;
	if (!Params->TryGetStringField(TEXT("node_type"), NodeType))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_type' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(AssetPath, Error);
	if (!ABP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
	}

	UEdGraph* AnimGraph = FindAnimGraph(ABP);
	if (!AnimGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("AnimGraph not found in AnimBlueprint"));
	}

	int32 PosX = Params->HasField(TEXT("pos_x")) ? static_cast<int32>(Params->GetNumberField(TEXT("pos_x"))) : 0;
	int32 PosY = Params->HasField(TEXT("pos_y")) ? static_cast<int32>(Params->GetNumberField(TEXT("pos_y"))) : 0;

	UAnimGraphNode_Base* NewNode = nullptr;

	if (NodeType.Equals(TEXT("CopyPoseFromMesh"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_CopyPoseFromMesh* Node = NewObject<UAnimGraphNode_CopyPoseFromMesh>(AnimGraph);

		Node->Node.bUseAttachedParent = Params->HasField(TEXT("use_attached_parent"))
			? Params->GetBoolField(TEXT("use_attached_parent")) : true;
		Node->Node.bCopyCurves = Params->HasField(TEXT("copy_curves"))
			? Params->GetBoolField(TEXT("copy_curves")) : true;

		NewNode = Node;
	}
	else if (NodeType.Equals(TEXT("AnimDynamics"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_AnimDynamics* Node = NewObject<UAnimGraphNode_AnimDynamics>(AnimGraph);

		// Bound bone
		FString BoundBone;
		if (Params->TryGetStringField(TEXT("bound_bone"), BoundBone))
		{
			Node->Node.BoundBone.BoneName = FName(*BoundBone);
		}

		// Chain end
		FString ChainEnd;
		if (Params->TryGetStringField(TEXT("chain_end"), ChainEnd))
		{
			Node->Node.ChainEnd.BoneName = FName(*ChainEnd);
			Node->Node.bChain = true;
		}

		// Explicit chain toggle
		if (Params->HasField(TEXT("is_chain")))
		{
			Node->Node.bChain = Params->GetBoolField(TEXT("is_chain"));
		}

		// Physics parameters
		if (Params->HasField(TEXT("gravity_scale")))
		{
			Node->Node.GravityScale = static_cast<float>(Params->GetNumberField(TEXT("gravity_scale")));
		}
		if (Params->HasField(TEXT("linear_damping")))
		{
			Node->Node.LinearDampingOverride = static_cast<float>(Params->GetNumberField(TEXT("linear_damping")));
		}
		if (Params->HasField(TEXT("angular_damping")))
		{
			Node->Node.AngularDampingOverride = static_cast<float>(Params->GetNumberField(TEXT("angular_damping")));
		}

		NewNode = Node;
	}
	else
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unknown anim node type: %s. Supported: CopyPoseFromMesh, AnimDynamics"), *NodeType));
	}

	// Position, add to graph, allocate pins
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	AnimGraph->AddNode(NewNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
	NewNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	// Build response with node_id for use with connect_anim_nodes
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("node_type"), NodeType);
	Result->SetStringField(TEXT("node_id"), FString::FromInt(NewNode->GetUniqueID()));
	Result->SetNumberField(TEXT("pos_x"), PosX);
	Result->SetNumberField(TEXT("pos_y"), PosY);

	// List output pin names for connection reference
	TArray<TSharedPtr<FJsonValue>> PinNames;
	for (UEdGraphPin* Pin : NewNode->Pins)
	{
		TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
		PinObj->SetStringField(TEXT("name"), Pin->GetName());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Output ? TEXT("output") : TEXT("input"));
		PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		PinNames.Add(MakeShareable(new FJsonValueObject(PinObj)));
	}
	Result->SetArrayField(TEXT("pins"), PinNames);

	return Result;
}
