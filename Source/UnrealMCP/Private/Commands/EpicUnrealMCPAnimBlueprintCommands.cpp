#include "Commands/EpicUnrealMCPAnimBlueprintCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_Base.h"
#include "Factories/AnimBlueprintFactory.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "JsonObjectConverter.h"

// AnimGraph node includes
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_BlendSpaceEvaluator.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"

// K2 node types for transition-rule introspection
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/KismetMathLibrary.h" // For Not_PreBool when negating simplified transition rules

// AnimGraph node types for add_anim_node command
#include "AnimGraphNode_CopyPoseFromMesh.h"
#include "AnimGraphNode_AnimDynamics.h"
#include "AnimGraphNode_RigidBody.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "AnimGraphNode_ApplyAdditive.h"
#include "AnimGraphNode_DeadBlending.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_Mirror.h"
#include "Animation/MirrorDataTable.h"

// Animation Warping plugin nodes
#include "AnimGraph/AnimGraphNode_OrientationWarping.h"
#include "AnimGraph/AnimGraphNode_StrideWarping.h"

// ============================================================================
// Serialization helpers (file-scope). Shared by read_anim_blueprint and
// get_state_machine_info so state internals and transition rule logic are
// exposed without duplicating per-handler traversal code.
// ============================================================================

namespace
{
	const TCHAR* RootMotionModeToString(ERootMotionMode::Type Mode)
	{
		switch (Mode)
		{
			case ERootMotionMode::NoRootMotionExtraction:   return TEXT("NoRootMotionExtraction");
			case ERootMotionMode::IgnoreRootMotion:         return TEXT("IgnoreRootMotion");
			case ERootMotionMode::RootMotionFromEverything: return TEXT("RootMotionFromEverything");
			case ERootMotionMode::RootMotionFromMontagesOnly: return TEXT("RootMotionFromMontagesOnly");
			default:                                         return TEXT("Unknown");
		}
	}

	// Serialize the inner FAnimNode_* struct of a UAnimGraphNode_Base via reflection.
	// Covers settings that aren't surfaced as graph pins — e.g. StrideWarping's Mode,
	// LocomotionSpeed, MinRootMotionSpeedThreshold; BlendSpacePlayer sync groups; etc.
	TSharedPtr<FJsonObject> SerializeAnimNodeProperties(UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
		if (!AnimNode) return Obj;

		FStructProperty* NodeProp = AnimNode->GetFNodeProperty();
		if (!NodeProp || !NodeProp->Struct) return Obj;

		const void* NodePtr = NodeProp->ContainerPtrToValuePtr<void>(AnimNode);
		if (!NodePtr) return Obj;

		// CPF_Edit filter keeps only designer-visible properties; skip editor-only debug draw
		// and transient runtime state (those aren't marked Edit).
		FJsonObjectConverter::UStructToJsonObject(
			NodeProp->Struct,
			NodePtr,
			Obj.ToSharedRef(),
			CPF_Edit, 0);
		return Obj;
	}

	TArray<TSharedPtr<FJsonValue>> SerializePinLinks(const UEdGraphPin* Pin)
	{
		TArray<TSharedPtr<FJsonValue>> LinksArray;
		if (!Pin) return LinksArray;
		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
			TSharedPtr<FJsonObject> LinkObj = MakeShareable(new FJsonObject);
			LinkObj->SetStringField(TEXT("source_node_id"),
				FString::FromInt(LinkedPin->GetOwningNode()->GetUniqueID()));
			LinkObj->SetStringField(TEXT("source_pin"), LinkedPin->GetName());
			LinksArray.Add(MakeShareable(new FJsonValueObject(LinkObj)));
		}
		return LinksArray;
	}

	TSharedPtr<FJsonObject> SerializeInnerAnimNode(UEdGraphNode* InnerNode)
	{
		TSharedPtr<FJsonObject> InnerObj = MakeShareable(new FJsonObject);
		if (!InnerNode) return InnerObj;

		InnerObj->SetStringField(TEXT("class"), InnerNode->GetClass()->GetName());
		InnerObj->SetStringField(TEXT("node_id"), FString::FromInt(InnerNode->GetUniqueID()));

		// K2Node_CallFunction nodes inside state graphs (e.g. PlayRate suppliers,
		// dynamic blendspace selectors). Surface the function name + owner so callers
		// can verify the node is calling the expected helper (e.g. GetActiveLocomotionBS
		// vs CurrentLocomotionBS direct read).
		if (UK2Node_CallFunction* CallFn = Cast<UK2Node_CallFunction>(InnerNode))
		{
			InnerObj->SetStringField(TEXT("function"),
				CallFn->FunctionReference.GetMemberName().ToString());
			if (const UClass* Owner = CallFn->FunctionReference.GetMemberParentClass())
			{
				InnerObj->SetStringField(TEXT("function_owner"), Owner->GetName());
			}
		}
		else if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(InnerNode))
		{
			InnerObj->SetStringField(TEXT("variable"), VarGet->GetVarNameString());
			const FString Scope = VarGet->VariableReference.GetMemberScopeName();
			if (!Scope.IsEmpty())
			{
				InnerObj->SetStringField(TEXT("scope"), Scope);
			}
		}

		if (UAnimGraphNode_SequencePlayer* SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(InnerNode))
		{
			if (UAnimationAsset* Asset = SeqPlayer->GetAnimationAsset())
			{
				InnerObj->SetStringField(TEXT("animation"), Asset->GetPathName());
			}
		}
		else if (UAnimGraphNode_SequenceEvaluator* SeqEval = Cast<UAnimGraphNode_SequenceEvaluator>(InnerNode))
		{
			if (UAnimationAsset* Asset = SeqEval->GetAnimationAsset())
			{
				InnerObj->SetStringField(TEXT("animation"), Asset->GetPathName());
			}
			InnerObj->SetStringField(TEXT("player_role"), TEXT("evaluator"));
		}
		else if (UAnimGraphNode_BlendSpacePlayer* BSPlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(InnerNode))
		{
			if (UAnimationAsset* Asset = BSPlayer->GetAnimationAsset())
			{
				InnerObj->SetStringField(TEXT("blend_space"), Asset->GetPathName());
			}
		}
		else if (UAnimGraphNode_BlendSpaceEvaluator* BSEval = Cast<UAnimGraphNode_BlendSpaceEvaluator>(InnerNode))
		{
			if (UAnimationAsset* Asset = BSEval->GetAnimationAsset())
			{
				InnerObj->SetStringField(TEXT("blend_space"), Asset->GetPathName());
			}
			InnerObj->SetStringField(TEXT("player_role"), TEXT("evaluator"));
		}
		else if (UAnimGraphNode_StateMachine* NestedSM = Cast<UAnimGraphNode_StateMachine>(InnerNode))
		{
			InnerObj->SetStringField(TEXT("nested_state_machine"), NestedSM->GetStateMachineName());
		}

		TArray<TSharedPtr<FJsonValue>> InputPinsArray;
		for (UEdGraphPin* Pin : InnerNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) continue;
			if (Pin->LinkedTo.Num() == 0) continue;
			TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
			PinObj->SetStringField(TEXT("name"), Pin->GetName());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PinObj->SetArrayField(TEXT("linked_from"), SerializePinLinks(Pin));
			InputPinsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
		}
		if (InputPinsArray.Num() > 0)
		{
			InnerObj->SetArrayField(TEXT("input_pins"), InputPinsArray);
		}

		// Reflected inner FAnimNode_* struct properties — covers non-pin config on nested
		// graph nodes (e.g. StrideWarping inside a state's bound graph).
		TSharedPtr<FJsonObject> PropsObj = SerializeAnimNodeProperties(InnerNode);
		if (PropsObj->Values.Num() > 0)
		{
			InnerObj->SetObjectField(TEXT("properties"), PropsObj);
		}

		return InnerObj;
	}

	TSharedPtr<FJsonObject> SerializeRuleGraph(UEdGraph* RuleGraph)
	{
		TSharedPtr<FJsonObject> RuleObj = MakeShareable(new FJsonObject);
		if (!RuleGraph)
		{
			RuleObj->SetNumberField(TEXT("total_nodes"), 0);
			return RuleObj;
		}

		TArray<TSharedPtr<FJsonValue>> VariableReads;
		TArray<TSharedPtr<FJsonValue>> FunctionCalls;
		TArray<TSharedPtr<FJsonValue>> NodesArray;
		TMap<FString, int32> ClassCountMap;

		for (UEdGraphNode* Node : RuleGraph->Nodes)
		{
			if (!Node) continue;
			ClassCountMap.FindOrAdd(Node->GetClass()->GetName())++;

			// Node-id index so callers can target individual rule-graph nodes
			// (delete elapsed-time conjuncts, replace expressions, etc.)
			TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
			NodeObj->SetStringField(TEXT("node_id"), FString::FromInt(Node->GetUniqueID()));
			NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			NodesArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));

			if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node))
			{
				TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject);
				VarObj->SetStringField(TEXT("node_id"), FString::FromInt(VarGet->GetUniqueID()));
				VarObj->SetStringField(TEXT("variable"), VarGet->GetVarNameString());
				const FString Scope = VarGet->VariableReference.GetMemberScopeName();
				if (!Scope.IsEmpty())
				{
					VarObj->SetStringField(TEXT("scope"), Scope);
				}
				VariableReads.Add(MakeShareable(new FJsonValueObject(VarObj)));
			}
			else if (UK2Node_CallFunction* CallFn = Cast<UK2Node_CallFunction>(Node))
			{
				TSharedPtr<FJsonObject> FnObj = MakeShareable(new FJsonObject);
				FnObj->SetStringField(TEXT("node_id"), FString::FromInt(CallFn->GetUniqueID()));
				FnObj->SetStringField(TEXT("function"),
					CallFn->FunctionReference.GetMemberName().ToString());
				if (const UClass* Owner = CallFn->FunctionReference.GetMemberParentClass())
				{
					FnObj->SetStringField(TEXT("owner"), Owner->GetName());
				}
				FunctionCalls.Add(MakeShareable(new FJsonValueObject(FnObj)));
			}
		}

		TArray<TSharedPtr<FJsonValue>> NodeClassCounts;
		for (const TPair<FString, int32>& Kv : ClassCountMap)
		{
			TSharedPtr<FJsonObject> KvObj = MakeShareable(new FJsonObject);
			KvObj->SetStringField(TEXT("class"), Kv.Key);
			KvObj->SetNumberField(TEXT("count"), Kv.Value);
			NodeClassCounts.Add(MakeShareable(new FJsonValueObject(KvObj)));
		}

		RuleObj->SetArrayField(TEXT("variable_reads"), VariableReads);
		RuleObj->SetArrayField(TEXT("function_calls"), FunctionCalls);
		RuleObj->SetArrayField(TEXT("node_class_counts"), NodeClassCounts);
		RuleObj->SetArrayField(TEXT("nodes"), NodesArray);
		RuleObj->SetNumberField(TEXT("total_nodes"), RuleGraph->Nodes.Num());
		return RuleObj;
	}

	TSharedPtr<FJsonObject> SerializeState(UAnimStateNode* StateNode)
	{
		TSharedPtr<FJsonObject> StateObj = MakeShareable(new FJsonObject);
		if (!StateNode) return StateObj;

		StateObj->SetStringField(TEXT("name"), StateNode->GetStateName());
		StateObj->SetStringField(TEXT("node_id"), FString::FromInt(StateNode->GetUniqueID()));
		StateObj->SetNumberField(TEXT("pos_x"), StateNode->NodePosX);
		StateObj->SetNumberField(TEXT("pos_y"), StateNode->NodePosY);

		if (!StateNode->BoundGraph)
		{
			return StateObj;
		}

		TArray<TSharedPtr<FJsonValue>> InnerNodesArray;
		FString ResultSourceId;
		for (UEdGraphNode* InnerNode : StateNode->BoundGraph->Nodes)
		{
			if (!InnerNode) continue;
			InnerNodesArray.Add(MakeShareable(new FJsonValueObject(SerializeInnerAnimNode(InnerNode))));

			if (UAnimGraphNode_StateResult* Result = Cast<UAnimGraphNode_StateResult>(InnerNode))
			{
				for (UEdGraphPin* Pin : Result->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Input) continue;
					if (Pin->LinkedTo.Num() == 0) continue;
					if (const UEdGraphPin* SrcPin = Pin->LinkedTo[0])
					{
						if (SrcPin->GetOwningNode())
						{
							ResultSourceId = FString::FromInt(SrcPin->GetOwningNode()->GetUniqueID());
						}
					}
					break;
				}
			}
		}
		StateObj->SetArrayField(TEXT("graph_nodes"), InnerNodesArray);
		if (!ResultSourceId.IsEmpty())
		{
			StateObj->SetStringField(TEXT("result_source_node_id"), ResultSourceId);
		}
		return StateObj;
	}

	// ============================================================================
	// EventGraph / FunctionGraph (non-AnimGraph) introspection
	//
	// AnimBPs run two distinct kinds of K2 graphs that read_anim_blueprint did not
	// previously surface: the Ubergraph (where BlueprintUpdateAnimation +
	// BlueprintThreadSafeUpdateAnimation events live) and user-authored Function
	// graphs (entry-point K2Node_FunctionEntry). The patterns we need to expose
	// are exec chains starting from those entry points — e.g. the Inertialization
	// request handshake `ConsumePendingInertializationRequest → Branch → RequestInertialization`.
	// ============================================================================

	UEdGraphPin* FindExecOutputPin(UEdGraphNode* Node)
	{
		if (!Node) return nullptr;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output &&
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	// Capture the per-node summary fields used by the exec-chain trace. Kept
	// separate from TraceExecChain so the same shape can be emitted for branch
	// condition sources (which are data nodes, not exec nodes).
	TSharedPtr<FJsonObject> SerializeChainNode(UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		if (!Node) return Obj;
		Obj->SetStringField(TEXT("node_id"), FString::FromInt(Node->GetUniqueID()));
		Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

		if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			Obj->SetStringField(TEXT("function"),
				Call->FunctionReference.GetMemberName().ToString());
			if (const UClass* Owner = Call->FunctionReference.GetMemberParentClass())
			{
				Obj->SetStringField(TEXT("function_owner"), Owner->GetName());
			}
		}
		else if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
		{
			Obj->SetStringField(TEXT("event"), Event->GetFunctionName().ToString());
			if (const UClass* Owner = Event->EventReference.GetMemberParentClass())
			{
				Obj->SetStringField(TEXT("event_owner"), Owner->GetName());
			}
		}
		else if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node))
		{
			Obj->SetStringField(TEXT("variable"), VarGet->GetVarNameString());
		}
		return Obj;
	}

	// Recursively trace exec from a starting node. Branches recurse into both
	// then/else outputs and capture the condition-source node so callers can
	// verify gating without a second query. Depth-limited + visited-set guarded
	// against cycles (e.g. ForEachLoop self-references).
	TSharedPtr<FJsonValue> TraceExecChain(UEdGraphNode* Node, int32 RemainingDepth, TSet<int32>& Visited)
	{
		if (!Node)
		{
			return MakeShareable(new FJsonValueNull);
		}
		if (RemainingDepth <= 0)
		{
			TSharedPtr<FJsonObject> CutObj = MakeShareable(new FJsonObject);
			CutObj->SetBoolField(TEXT("depth_cut"), true);
			CutObj->SetStringField(TEXT("at_node_id"), FString::FromInt(Node->GetUniqueID()));
			return MakeShareable(new FJsonValueObject(CutObj));
		}
		const int32 NodeId = (int32)Node->GetUniqueID();
		if (Visited.Contains(NodeId))
		{
			TSharedPtr<FJsonObject> CycleObj = MakeShareable(new FJsonObject);
			CycleObj->SetStringField(TEXT("cycle_to"), FString::FromInt(NodeId));
			return MakeShareable(new FJsonValueObject(CycleObj));
		}
		Visited.Add(NodeId);

		TSharedPtr<FJsonObject> Obj = SerializeChainNode(Node);

		if (UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Node))
		{
			if (UEdGraphPin* CondPin = Branch->GetConditionPin())
			{
				if (CondPin->LinkedTo.Num() > 0 && CondPin->LinkedTo[0] && CondPin->LinkedTo[0]->GetOwningNode())
				{
					Obj->SetObjectField(TEXT("condition_source"),
						SerializeChainNode(CondPin->LinkedTo[0]->GetOwningNode()));
				}
			}
			if (UEdGraphPin* ThenPin = Branch->GetThenPin())
			{
				if (ThenPin->LinkedTo.Num() > 0 && ThenPin->LinkedTo[0] && ThenPin->LinkedTo[0]->GetOwningNode())
				{
					Obj->SetField(TEXT("then"),
						TraceExecChain(ThenPin->LinkedTo[0]->GetOwningNode(), RemainingDepth - 1, Visited));
				}
			}
			if (UEdGraphPin* ElsePin = Branch->GetElsePin())
			{
				if (ElsePin->LinkedTo.Num() > 0 && ElsePin->LinkedTo[0] && ElsePin->LinkedTo[0]->GetOwningNode())
				{
					Obj->SetField(TEXT("else"),
						TraceExecChain(ElsePin->LinkedTo[0]->GetOwningNode(), RemainingDepth - 1, Visited));
				}
			}
			return MakeShareable(new FJsonValueObject(Obj));
		}

		// For any other exec node, follow the first exec output (covers CallFunction,
		// Sequence pin 0, ForEach Body, etc.). Sequence-style multi-then chains lose
		// pins 1+ here; that is acceptable for the inertialization signal but should
		// be revisited if Sequence-aware tracing becomes useful.
		if (UEdGraphPin* Out = FindExecOutputPin(Node))
		{
			if (Out->LinkedTo.Num() > 0 && Out->LinkedTo[0] && Out->LinkedTo[0]->GetOwningNode())
			{
				Obj->SetField(TEXT("next"),
					TraceExecChain(Out->LinkedTo[0]->GetOwningNode(), RemainingDepth - 1, Visited));
			}
		}
		return MakeShareable(new FJsonValueObject(Obj));
	}

	// Build the per-graph entry-point list: K2Node_Event for ubergraph pages,
	// K2Node_FunctionEntry for function graphs. Each entry includes the exec chain
	// rooted at its first exec-output linked node.
	TSharedPtr<FJsonObject> SerializeK2Graph(UEdGraph* Graph, bool bIsFunctionGraph, int32 MaxDepth)
	{
		TSharedPtr<FJsonObject> GraphObj = MakeShareable(new FJsonObject);
		if (!Graph)
		{
			GraphObj->SetNumberField(TEXT("total_nodes"), 0);
			return GraphObj;
		}

		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetNumberField(TEXT("total_nodes"), Graph->Nodes.Num());

		TArray<TSharedPtr<FJsonValue>> EntriesArray;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;

			UEdGraphNode* EntryNode = nullptr;
			TSharedPtr<FJsonObject> EntryObj = MakeShareable(new FJsonObject);

			if (!bIsFunctionGraph)
			{
				if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
				{
					EntryNode = Event;
					EntryObj->SetStringField(TEXT("entry_kind"), TEXT("event"));
					EntryObj->SetStringField(TEXT("event"), Event->GetFunctionName().ToString());
					if (const UClass* Owner = Event->EventReference.GetMemberParentClass())
					{
						EntryObj->SetStringField(TEXT("event_owner"), Owner->GetName());
					}
					EntryObj->SetBoolField(TEXT("is_override"), Event->bOverrideFunction);
				}
			}
			else
			{
				if (UK2Node_FunctionEntry* FnEntry = Cast<UK2Node_FunctionEntry>(Node))
				{
					EntryNode = FnEntry;
					EntryObj->SetStringField(TEXT("entry_kind"), TEXT("function_entry"));
					const FName FnName = FnEntry->FunctionReference.GetMemberName();
					EntryObj->SetStringField(TEXT("function"),
						FnName.IsNone() ? Graph->GetName() : FnName.ToString());
					if (const UClass* Owner = FnEntry->FunctionReference.GetMemberParentClass())
					{
						EntryObj->SetStringField(TEXT("function_owner"), Owner->GetName());
					}
				}
			}

			if (!EntryNode) continue;

			EntryObj->SetStringField(TEXT("node_id"), FString::FromInt(EntryNode->GetUniqueID()));

			if (UEdGraphPin* Out = FindExecOutputPin(EntryNode))
			{
				if (Out->LinkedTo.Num() > 0 && Out->LinkedTo[0] && Out->LinkedTo[0]->GetOwningNode())
				{
					TSet<int32> Visited;
					EntryObj->SetField(TEXT("exec_chain"),
						TraceExecChain(Out->LinkedTo[0]->GetOwningNode(), MaxDepth, Visited));
				}
				else
				{
					EntryObj->SetBoolField(TEXT("exec_chain_empty"), true);
				}
			}

			EntriesArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
		}

		GraphObj->SetArrayField(TEXT("entries"), EntriesArray);
		return GraphObj;
	}

	// Derived signal: detect the standard Inertialization request handshake.
	// Pattern: any K2 graph contains a CallFunction `ConsumePendingInertializationRequest`
	// whose ReturnValue (bool) feeds a Branch whose Then-pin reaches a CallFunction
	// `RequestInertialization`. Returns the discovered node_ids so callers can
	// open the exact location in-editor without a second query.
	void EvaluateInertializationSignal(const TArray<UEdGraph*>& Graphs, TSharedPtr<FJsonObject> OutSignal)
	{
		bool bHasConsume = false;
		bool bHasRequest = false;
		bool bChainWired = false;
		FString ConsumeNodeId;
		FString BranchNodeId;
		FString RequestNodeId;

		// First pass: locate the Consume call. There is only one in well-formed AnimBPs.
		UK2Node_CallFunction* ConsumeNode = nullptr;
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
				if (!Call) continue;
				const FName FnName = Call->FunctionReference.GetMemberName();
				if (FnName == FName(TEXT("ConsumePendingInertializationRequest")))
				{
					bHasConsume = true;
					ConsumeNode = Call;
					ConsumeNodeId = FString::FromInt(Call->GetUniqueID());
				}
				else if (FnName == FName(TEXT("RequestInertialization")))
				{
					bHasRequest = true;
					if (RequestNodeId.IsEmpty())
					{
						RequestNodeId = FString::FromInt(Call->GetUniqueID());
					}
				}
			}
		}

		// Second pass: confirm Consume's bool return drives a Branch whose Then
		// reaches a RequestInertialization call. We follow exec from the Branch
		// through ordinary call/sequence chains; depth is bounded.
		if (ConsumeNode)
		{
			UEdGraphPin* ReturnPin = nullptr;
			for (UEdGraphPin* Pin : ConsumeNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output &&
					Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
				{
					ReturnPin = Pin;
					break;
				}
			}

			UK2Node_IfThenElse* Branch = nullptr;
			if (ReturnPin)
			{
				for (UEdGraphPin* LinkedPin : ReturnPin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
					if (UK2Node_IfThenElse* Cand = Cast<UK2Node_IfThenElse>(LinkedPin->GetOwningNode()))
					{
						if (LinkedPin == Cand->GetConditionPin())
						{
							Branch = Cand;
							BranchNodeId = FString::FromInt(Cand->GetUniqueID());
							break;
						}
					}
				}
			}

			if (Branch)
			{
				// Walk Then-pin downstream up to a small depth, looking for a
				// CallFunction named RequestInertialization. Stops at the first
				// match — multi-call setups still resolve cleanly because we only
				// need existence proof.
				UEdGraphPin* ThenPin = Branch->GetThenPin();
				const int32 MaxDepth = 16;
				TSet<int32> Visited;
				UEdGraphNode* Cursor = (ThenPin && ThenPin->LinkedTo.Num() > 0 && ThenPin->LinkedTo[0])
					? ThenPin->LinkedTo[0]->GetOwningNode() : nullptr;
				int32 Depth = 0;
				while (Cursor && Depth < MaxDepth)
				{
					const int32 CursorId = (int32)Cursor->GetUniqueID();
					if (Visited.Contains(CursorId)) break;
					Visited.Add(CursorId);

					if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Cursor))
					{
						if (CallNode->FunctionReference.GetMemberName() == FName(TEXT("RequestInertialization")))
						{
							bChainWired = true;
							RequestNodeId = FString::FromInt(CallNode->GetUniqueID());
							break;
						}
					}

					UEdGraphPin* OutPin = FindExecOutputPin(Cursor);
					Cursor = (OutPin && OutPin->LinkedTo.Num() > 0 && OutPin->LinkedTo[0])
						? OutPin->LinkedTo[0]->GetOwningNode() : nullptr;
					++Depth;
				}
			}
		}

		OutSignal->SetBoolField(TEXT("has_consume_call"), bHasConsume);
		OutSignal->SetBoolField(TEXT("has_request_call"), bHasRequest);
		OutSignal->SetBoolField(TEXT("consume_drives_branch_then_to_request"), bChainWired);
		if (!ConsumeNodeId.IsEmpty()) OutSignal->SetStringField(TEXT("consume_node_id"), ConsumeNodeId);
		if (!BranchNodeId.IsEmpty())  OutSignal->SetStringField(TEXT("branch_node_id"), BranchNodeId);
		if (!RequestNodeId.IsEmpty()) OutSignal->SetStringField(TEXT("request_node_id"), RequestNodeId);
	}
}

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
	else if (CommandType == TEXT("disconnect_anim_nodes"))
	{
		return HandleDisconnectAnimNodes(Params);
	}
	else if (CommandType == TEXT("set_pin_default_value"))
	{
		return HandleSetPinDefaultValue(Params);
	}
	else if (CommandType == TEXT("set_transition_property"))
	{
		return HandleSetTransitionProperty(Params);
	}
	else if (CommandType == TEXT("simplify_transition_rule"))
	{
		return HandleSimplifyTransitionRule(Params);
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
	else if (CommandType == TEXT("delete_anim_node"))
	{
		return HandleDeleteAnimNode(Params);
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
			if (FEpicUnrealMCPCommonUtils::AssetExists(Path))
			{
				AssetPath = Path;
				break;
			}
		}
	}

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
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

UEdGraph* FEpicUnrealMCPAnimBlueprintCommands::ResolveAnimGraphTarget(
	UAnimBlueprint* ABP, const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	OutError.Reset();
	if (!ABP)
	{
		OutError = TEXT("ResolveAnimGraphTarget: null AnimBlueprint");
		return nullptr;
	}

	FString StateMachine, StateName;
	const bool bHasSM = Params.IsValid() && Params->TryGetStringField(TEXT("state_machine"), StateMachine) && !StateMachine.IsEmpty();
	const bool bHasState = Params.IsValid() && Params->TryGetStringField(TEXT("state"), StateName) && !StateName.IsEmpty();

	// Default: top-level AnimGraph (FunctionGraph "AnimGraph").
	if (!bHasSM && !bHasState)
	{
		UEdGraph* AnimGraph = FindAnimGraph(ABP);
		if (!AnimGraph)
		{
			OutError = TEXT("AnimGraph not found in AnimBlueprint");
		}
		return AnimGraph;
	}

	// Targeting a state requires both SM and state names.
	if (bHasState && !bHasSM)
	{
		OutError = TEXT("Param 'state' requires 'state_machine' to disambiguate which SM owns the state");
		return nullptr;
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineByName(ABP, StateMachine);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		OutError = FString::Printf(TEXT("State machine '%s' not found"), *StateMachine);
		return nullptr;
	}

	if (!bHasState)
	{
		// Targeting the SM transition graph itself (rarely useful for anim nodes;
		// returned for completeness so callers can introspect/structure ops).
		return SMNode->EditorStateMachineGraph;
	}

	UAnimStateNode* StateNode = FindStateByName(SMNode->EditorStateMachineGraph, StateName);
	if (!StateNode)
	{
		OutError = FString::Printf(TEXT("State '%s' not found in state machine '%s'"), *StateName, *StateMachine);
		return nullptr;
	}
	if (!StateNode->BoundGraph)
	{
		OutError = FString::Printf(TEXT("State '%s' has no BoundGraph"), *StateName);
		return nullptr;
	}
	return StateNode->BoundGraph;
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
	if (FEpicUnrealMCPCommonUtils::AssetExists(FullPath))
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

	// Class-default RootMotionMode (not visible on any node — only on the AnimInstance CDO).
	// RootMotionFromEverything vs RootMotionFromMontagesOnly is the most common locomotion
	// skating / double-apply bug in GMC projects, so surface it at the top level.
	if (UClass* GenClass = ABP->GeneratedClass)
	{
		if (const UAnimInstance* CDO = Cast<UAnimInstance>(GenClass->GetDefaultObject()))
		{
			Result->SetStringField(TEXT("root_motion_mode"), RootMotionModeToString(CDO->RootMotionMode));
		}
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

				// List states (flat names) + detailed state objects with inner nodes
				TArray<TSharedPtr<FJsonValue>> StatesArray;
				TArray<TSharedPtr<FJsonValue>> StatesDetailArray;
				UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;
				for (UEdGraphNode* SMChild : SMGraph->Nodes)
				{
					UAnimStateNode* StateNode = Cast<UAnimStateNode>(SMChild);
					if (StateNode)
					{
						StatesArray.Add(MakeShareable(new FJsonValueString(StateNode->GetStateName())));
						StatesDetailArray.Add(MakeShareable(new FJsonValueObject(SerializeState(StateNode))));
					}
				}
				SMObj->SetArrayField(TEXT("states"), StatesArray);
				SMObj->SetArrayField(TEXT("states_detail"), StatesDetailArray);

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

				// List transitions including rule graph contents
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
						TransObj->SetObjectField(TEXT("rule"), SerializeRuleGraph(TransNode->GetBoundGraph()));
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

			// Reflected inner FAnimNode_* struct properties (configuration that lives on the
			// runtime node but isn't exposed as graph pins — e.g. StrideWarping.Mode).
			NodeObj->SetObjectField(TEXT("properties"), SerializeAnimNodeProperties(Node));

			AnimNodesArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
		}
		Result->SetNumberField(TEXT("anim_graph_node_count"), AnimGraph->Nodes.Num());
	}
	Result->SetArrayField(TEXT("anim_graph_nodes"), AnimNodesArray);

	// EventGraph + non-AnimGraph FunctionGraphs: BlueprintUpdateAnimation,
	// BlueprintThreadSafeUpdateAnimation, BlueprintInitializeAnimation, and any
	// user-authored functions. The AnimGraph itself is excluded — it is already
	// surfaced above via anim_graph_nodes / state_machines and sending it back
	// through the K2 trace would only add noise.
	const int32 ChainDepth = 64;

	TArray<TSharedPtr<FJsonValue>> UbergraphArray;
	for (UEdGraph* Page : ABP->UbergraphPages)
	{
		if (!Page) continue;
		UbergraphArray.Add(MakeShareable(new FJsonValueObject(SerializeK2Graph(Page, /*bIsFunctionGraph*/ false, ChainDepth))));
	}

	TArray<TSharedPtr<FJsonValue>> FunctionGraphArray;
	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (!Graph) continue;
		if (Graph->GetFName() == FName("AnimGraph")) continue;
		FunctionGraphArray.Add(MakeShareable(new FJsonValueObject(SerializeK2Graph(Graph, /*bIsFunctionGraph*/ true, ChainDepth))));
	}

	TSharedPtr<FJsonObject> EventGraphObj = MakeShareable(new FJsonObject);
	EventGraphObj->SetArrayField(TEXT("ubergraph_pages"), UbergraphArray);
	EventGraphObj->SetArrayField(TEXT("function_graphs"), FunctionGraphArray);
	Result->SetObjectField(TEXT("event_graph"), EventGraphObj);

	// Derived signal: confirm Inertialization request handshake is wired up.
	// Without this chain, an Inertialization node in the AnimGraph receives zero
	// requests at runtime — which is the diagnostic the caller is verifying.
	TArray<UEdGraph*> AllK2Graphs;
	for (UEdGraph* Page : ABP->UbergraphPages) { if (Page) AllK2Graphs.Add(Page); }
	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() != FName("AnimGraph")) AllK2Graphs.Add(Graph);
	}
	TSharedPtr<FJsonObject> InertSignal = MakeShareable(new FJsonObject);
	EvaluateInertializationSignal(AllK2Graphs, InertSignal);
	Result->SetObjectField(TEXT("inertialization_signal"), InertSignal);

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
	if (!Params->TryGetStringField(TEXT("asset_path"), BlueprintName))
	{
		if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
		}
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

	// Connect through the graph schema, not a raw MakeLinkTo: the AnimGraph schema
	// auto-spawns space-conversion nodes (LocalToComponentSpace / ComponentToLocalSpace)
	// when a local-space pose pin meets a component-space one (e.g. wiring into a
	// RigidBody node). A raw link between mismatched pose spaces compiles with fatal
	// per-link type errors.
	bool bConnected = false;
	if (const UEdGraphSchema* Schema = SourceNode->GetGraph()->GetSchema())
	{
		bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);
	}
	if (!bConnected)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Schema rejected connection %s.%s -> %s.%s"),
				*SourceNodeId, *SourcePinName, *TargetNodeId, *TargetPinName));
	}

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
// disconnect_anim_nodes — break a specific link between two pins. Required when
// inserting a node mid-chain so the old direct A->C link doesn't linger alongside
// the new A->B->C path (UE input pose pins reject multi-source on compile, but
// stale links accumulate during graph edits unless explicitly broken).
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleDisconnectAnimNodes(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("asset_path"), BlueprintName))
	{
		if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
		}
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

	const bool bWasLinked = SourcePin->LinkedTo.Contains(TargetPin);
	SourcePin->BreakLinkTo(TargetPin);

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("source_node"), SourceNodeId);
	Result->SetStringField(TEXT("source_pin"), SourcePinName);
	Result->SetStringField(TEXT("target_node"), TargetNodeId);
	Result->SetStringField(TEXT("target_pin"), TargetPinName);
	Result->SetBoolField(TEXT("was_linked"), bWasLinked);
	Result->SetBoolField(TEXT("disconnected"), true);
	return Result;
}

// ============================================================================
// set_pin_default_value — set the literal default on an unconnected input pin.
// Useful for wiring enum literals (e.g. EPRKBowPhase::Aimed -> EnumEquality.B)
// or scalar/string defaults without needing to add a literal node + connect it.
// For struct pins (Vector, Rotator), pass `default_value` as the JSON-string form
// UE uses internally (e.g. "(X=1,Y=0,Z=0)"). For object/asset pins, pass an
// asset path and the resolved object will be assigned to DefaultObject.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleSetPinDefaultValue(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("asset_path"), BlueprintName))
	{
		if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
		}
	}

	FString NodeId;
	if (!Params->TryGetStringField(TEXT("node_id"), NodeId))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
	}

	FString PinName;
	if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));
	}

	FString DefaultValue;
	if (!Params->TryGetStringField(TEXT("default_value"), DefaultValue))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'default_value' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	int32 NumericId = FCString::Atoi(*NodeId);

	UEdGraphNode* TargetNode = nullptr;
	TArray<UEdGraph*> AllGraphs;
	ABP->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->GetUniqueID() == NumericId) { TargetNode = Node; break; }
		}
		if (TargetNode) break;
	}

	if (!TargetNode)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Node with id '%s' not found"), *NodeId));
	}

	UEdGraphPin* Pin = FEpicUnrealMCPCommonUtils::FindPin(TargetNode, PinName, EGPD_Input);
	if (!Pin)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Input pin '%s' not found on node %s"), *PinName, *NodeId));
	}

	if (Pin->LinkedTo.Num() > 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Pin '%s' is connected; disconnect before setting default"), *PinName));
	}

	// Object pins use DefaultObject; everything else uses DefaultValue (UE schema).
	const FName Category = Pin->PinType.PinCategory;
	const bool bIsObjectPin =
		Category == UEdGraphSchema_K2::PC_Object ||
		Category == UEdGraphSchema_K2::PC_Class ||
		Category == UEdGraphSchema_K2::PC_SoftObject ||
		Category == UEdGraphSchema_K2::PC_SoftClass ||
		Category == UEdGraphSchema_K2::PC_Interface;

	if (bIsObjectPin)
	{
		UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *DefaultValue);
		if (!Loaded)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to load object at path '%s'"), *DefaultValue));
		}
		Pin->DefaultObject = Loaded;
		Pin->DefaultValue = FString();
	}
	else
	{
		Pin->DefaultValue = DefaultValue;
		Pin->DefaultObject = nullptr;
	}

	// Schema notification so the editor refreshes pin display + autogen literal.
	if (const UEdGraphSchema* Schema = TargetNode->GetGraph() ? TargetNode->GetGraph()->GetSchema() : nullptr)
	{
		Schema->TrySetDefaultValue(*Pin, Pin->DefaultValue);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("pin_name"), PinName);
	Result->SetStringField(TEXT("default_value"), Pin->DefaultValue);
	if (Pin->DefaultObject)
	{
		Result->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
	}
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// ============================================================================
// set_transition_property — set a scalar field on a transition node identified
// by from_state + to_state (or transition_node_id). Doesn't touch the rule
// graph. Supports CrossfadeDuration, PriorityOrder, Bidirectional, bDisabled,
// AutomaticRuleTriggerTime, bAutomaticRuleBasedOnSequencePlayerInState.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleSetTransitionProperty(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("asset_path"), BlueprintName))
	{
		if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
		}
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}
	if (!Params->HasField(TEXT("property_value")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
	}
	TSharedPtr<FJsonValue> Value = Params->Values.FindRef(TEXT("property_value"));

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	// Locate the transition: by node_id, or by state_machine + from/to state names.
	UAnimStateTransitionNode* Trans = nullptr;
	FString NodeIdStr;
	if (Params->TryGetStringField(TEXT("transition_node_id"), NodeIdStr))
	{
		const int32 Id = FCString::Atoi(*NodeIdStr);
		TArray<UEdGraph*> AllGraphs;
		ABP->GetAllGraphs(AllGraphs);
		for (UEdGraph* G : AllGraphs)
		{
			for (UEdGraphNode* N : G->Nodes)
			{
				if (N->GetUniqueID() == Id)
				{
					Trans = Cast<UAnimStateTransitionNode>(N);
					if (Trans) break;
				}
			}
			if (Trans) break;
		}
	}
	else
	{
		FString SMName, FromState, ToState;
		if (!Params->TryGetStringField(TEXT("state_machine_name"), SMName) ||
			!Params->TryGetStringField(TEXT("from_state"), FromState) ||
			!Params->TryGetStringField(TEXT("to_state"), ToState))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				TEXT("Provide either 'transition_node_id' or 'state_machine_name'+'from_state'+'to_state'"));
		}
		UAnimGraphNode_StateMachine* SMNode = FindStateMachineByName(ABP, SMName);
		if (!SMNode || !SMNode->EditorStateMachineGraph)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		}
		for (UEdGraphNode* N : SMNode->EditorStateMachineGraph->Nodes)
		{
			UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(N);
			if (!T) continue;
			UAnimStateNodeBase* Prev = T->GetPreviousState();
			UAnimStateNodeBase* Next = T->GetNextState();
			if (Prev && Next && Prev->GetStateName() == FromState && Next->GetStateName() == ToState)
			{
				Trans = T;
				break;
			}
		}
	}

	if (!Trans)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Transition not found"));
	}

	// Apply the property. Lower-case match so callers can pass either form.
	bool bSet = false;
	if (PropertyName.Equals(TEXT("crossfade_duration"), ESearchCase::IgnoreCase) ||
		PropertyName.Equals(TEXT("CrossfadeDuration"), ESearchCase::IgnoreCase))
	{
		Trans->CrossfadeDuration = static_cast<float>(Value->AsNumber());
		bSet = true;
	}
	else if (PropertyName.Equals(TEXT("priority"), ESearchCase::IgnoreCase) ||
		PropertyName.Equals(TEXT("PriorityOrder"), ESearchCase::IgnoreCase))
	{
		Trans->PriorityOrder = static_cast<int32>(Value->AsNumber());
		bSet = true;
	}
	else if (PropertyName.Equals(TEXT("bidirectional"), ESearchCase::IgnoreCase) ||
		PropertyName.Equals(TEXT("Bidirectional"), ESearchCase::IgnoreCase))
	{
		Trans->Bidirectional = Value->AsBool();
		bSet = true;
	}
	else if (PropertyName.Equals(TEXT("disabled"), ESearchCase::IgnoreCase) ||
		PropertyName.Equals(TEXT("bDisabled"), ESearchCase::IgnoreCase))
	{
		Trans->bDisabled = Value->AsBool();
		bSet = true;
	}
	else if (PropertyName.Equals(TEXT("automatic_rule_trigger_time"), ESearchCase::IgnoreCase) ||
		PropertyName.Equals(TEXT("AutomaticRuleTriggerTime"), ESearchCase::IgnoreCase))
	{
		Trans->AutomaticRuleTriggerTime = static_cast<float>(Value->AsNumber());
		bSet = true;
	}
	else if (PropertyName.Equals(TEXT("automatic_rule"), ESearchCase::IgnoreCase) ||
		PropertyName.Equals(TEXT("bAutomaticRuleBasedOnSequencePlayerInState"), ESearchCase::IgnoreCase))
	{
		Trans->bAutomaticRuleBasedOnSequencePlayerInState = Value->AsBool();
		bSet = true;
	}

	if (!bSet)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unknown transition property '%s'. Supported: crossfade_duration, priority, bidirectional, disabled, automatic_rule_trigger_time, automatic_rule"), *PropertyName));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("transition_node_id"), FString::FromInt(Trans->GetUniqueID()));
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// ============================================================================
// simplify_transition_rule — replace the rule's bound graph with a single bool
// variable test. Common case: stripping `bool && elapsed_time > X` conjuncts
// down to just `bool`. Args: blueprint_name, state_machine_name, from_state,
// to_state, variable_name, optional `negate` (bool, default false — wraps in
// NOT for "NOT bIsAirborne"-style rules).
//
// Wipes everything in the rule's BoundGraph except the TransitionResult node,
// adds a K2Node_VariableGet (and optionally a Not), and connects to
// TransitionResult.bCanEnterTransition.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleSimplifyTransitionRule(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("asset_path"), BlueprintName))
	{
		if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
		}
	}

	FString FromState, ToState, VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
	}
	bool bNegate = false;
	Params->TryGetBoolField(TEXT("negate"), bNegate);

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(BlueprintName, Error);
	if (!ABP) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);

	// Locate the transition: by node id (the only unambiguous handle when two transitions share a
	// from/to pair — add_transition returns it as node_id), or by state machine + state names.
	UAnimStateTransitionNode* Trans = nullptr;
	FString NodeIdStr;
	if (Params->TryGetStringField(TEXT("transition_node_id"), NodeIdStr))
	{
		const int32 Id = FCString::Atoi(*NodeIdStr);
		TArray<UEdGraph*> AllGraphs;
		ABP->GetAllGraphs(AllGraphs);
		for (UEdGraph* G : AllGraphs)
		{
			for (UEdGraphNode* N : G->Nodes)
			{
				if (N->GetUniqueID() == Id)
				{
					Trans = Cast<UAnimStateTransitionNode>(N);
					if (Trans) break;
				}
			}
			if (Trans) break;
		}
		if (!Trans)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Transition node id %s not found"), *NodeIdStr));
		}
	}
	else
	{
		FString SMName;
		if (!Params->TryGetStringField(TEXT("state_machine_name"), SMName) ||
			!Params->TryGetStringField(TEXT("from_state"), FromState) ||
			!Params->TryGetStringField(TEXT("to_state"), ToState))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				TEXT("Provide either 'transition_node_id' or 'state_machine_name'+'from_state'+'to_state'"));
		}
		UAnimGraphNode_StateMachine* SMNode = FindStateMachineByName(ABP, SMName);
		if (!SMNode || !SMNode->EditorStateMachineGraph)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("State machine '%s' not found"), *SMName));
		}

		int32 MatchCount = 0;
		for (UEdGraphNode* N : SMNode->EditorStateMachineGraph->Nodes)
		{
			UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(N);
			if (!T) continue;
			UAnimStateNodeBase* Prev = T->GetPreviousState();
			UAnimStateNodeBase* Next = T->GetNextState();
			if (Prev && Next && Prev->GetStateName() == FromState && Next->GetStateName() == ToState)
			{
				if (!Trans) Trans = T;
				++MatchCount;
			}
		}
		if (!Trans)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Transition '%s' -> '%s' not found"), *FromState, *ToState));
		}
		// This call wipes the rule graph. With two transitions on one pair, picking the first would
		// silently destroy the other's rule — refuse and ask for the node id.
		if (MatchCount > 1)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("%d transitions match '%s' -> '%s' — pass 'transition_node_id' to pick one"),
				MatchCount, *FromState, *ToState));
		}
	}
	if (UAnimStateNodeBase* Prev = Trans->GetPreviousState()) FromState = Prev->GetStateName();
	if (UAnimStateNodeBase* Next = Trans->GetNextState()) ToState = Next->GetStateName();

	UEdGraph* RuleGraph = Trans->GetBoundGraph();
	if (!RuleGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Transition has no rule graph"));
	}

	// Find the TransitionResult node — preserved across the rewrite.
	UAnimGraphNode_TransitionResult* ResultNode = nullptr;
	for (UEdGraphNode* N : RuleGraph->Nodes)
	{
		ResultNode = Cast<UAnimGraphNode_TransitionResult>(N);
		if (ResultNode) break;
	}
	if (!ResultNode)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Rule graph has no TransitionResult node"));
	}

	// Resolve the BlueprintGeneratedClass member named VariableName so we can
	// initialize K2Node_VariableGet.VariableReference. Self-context is what the
	// existing rule nodes use (confirmed in dumps).
	UClass* SelfClass = ABP->GeneratedClass ? ABP->GeneratedClass.Get() : nullptr;
	if (!SelfClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("AnimBlueprint has no generated class — compile first"));
	}
	FProperty* VariableProp = FindFProperty<FProperty>(SelfClass, *VariableName);
	if (!VariableProp)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Variable '%s' not found on %s"), *VariableName, *SelfClass->GetName()));
	}

	// Wipe everything from the rule graph except the TransitionResult node.
	TArray<UEdGraphNode*> ToRemove;
	for (UEdGraphNode* N : RuleGraph->Nodes)
	{
		if (N != ResultNode) ToRemove.Add(N);
	}
	for (UEdGraphNode* N : ToRemove)
	{
		RuleGraph->RemoveNode(N);
	}

	// Disconnect any stale links on the TransitionResult input pin.
	UEdGraphPin* ResultPin = nullptr;
	for (UEdGraphPin* Pin : ResultNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			ResultPin = Pin;
			break;
		}
	}
	if (!ResultPin)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("TransitionResult has no input pin"));
	}
	ResultPin->BreakAllPinLinks();

	// Add the VariableGet, mark as self-context.
	UK2Node_VariableGet* VarGet = NewObject<UK2Node_VariableGet>(RuleGraph);
	VarGet->VariableReference.SetSelfMember(VariableProp->GetFName());
	VarGet->NodePosX = ResultNode->NodePosX - 240;
	VarGet->NodePosY = ResultNode->NodePosY;
	RuleGraph->AddNode(VarGet, /*bIsFromUI=*/false, /*bSelectNewNode=*/false);
	VarGet->PostPlacedNewNode();
	VarGet->AllocateDefaultPins();

	UEdGraphPin* VarOutPin = nullptr;
	for (UEdGraphPin* Pin : VarGet->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinName == VariableProp->GetFName())
		{
			VarOutPin = Pin;
			break;
		}
	}
	if (!VarOutPin)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("VariableGet has no output pin matching '%s' (wrong type?)"), *VariableName));
	}

	if (bNegate)
	{
		// Insert a NOT_PreBool call between VariableGet and TransitionResult.
		UK2Node_CallFunction* NotNode = NewObject<UK2Node_CallFunction>(RuleGraph);
		UFunction* NotFn = UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(TEXT("Not_PreBool")));
		if (!NotFn)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Not_PreBool function not found in KismetMathLibrary"));
		}
		NotNode->FunctionReference.SetExternalMember(NotFn->GetFName(), UKismetMathLibrary::StaticClass());
		NotNode->NodePosX = ResultNode->NodePosX - 120;
		NotNode->NodePosY = ResultNode->NodePosY;
		RuleGraph->AddNode(NotNode, false, false);
		NotNode->PostPlacedNewNode();
		NotNode->AllocateDefaultPins();

		UEdGraphPin* NotIn = nullptr;
		UEdGraphPin* NotOut = nullptr;
		for (UEdGraphPin* P : NotNode->Pins)
		{
			if (P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean) NotIn = P;
			else if (P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean) NotOut = P;
		}
		if (!NotIn || !NotOut)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Not_PreBool pins not resolved"));
		}
		VarOutPin->MakeLinkTo(NotIn);
		NotOut->MakeLinkTo(ResultPin);
	}
	else
	{
		VarOutPin->MakeLinkTo(ResultPin);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("from_state"), FromState);
	Result->SetStringField(TEXT("to_state"), ToState);
	Result->SetStringField(TEXT("variable"), VariableName);
	Result->SetBoolField(TEXT("negated"), bNegate);
	Result->SetStringField(TEXT("transition_node_id"), FString::FromInt(Trans->GetUniqueID()));
	Result->SetStringField(TEXT("variable_get_node_id"), FString::FromInt(VarGet->GetUniqueID()));
	Result->SetBoolField(TEXT("success"), true);
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

	// States with details (inner nodes, result wiring)
	TArray<TSharedPtr<FJsonValue>> StatesArray;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
		if (!StateNode) continue;
		StatesArray.Add(MakeShareable(new FJsonValueObject(SerializeState(StateNode))));
	}
	Result->SetArrayField(TEXT("states"), StatesArray);

	// Transitions with details + rule graph introspection
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
		TransObj->SetObjectField(TEXT("rule"), SerializeRuleGraph(TransNode->GetBoundGraph()));

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

	// AnimGraph here may be the top-level AnimGraph or a state's inner BoundGraph
	// when state_machine + state are supplied. Both are UAnimationGraph subclasses
	// and accept UAnimGraphNode_* via the AnimGraph schema.
	UEdGraph* AnimGraph = ResolveAnimGraphTarget(ABP, Params, Error);
	if (!AnimGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
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
	else if (NodeType.Equals(TEXT("RigidBody"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_RigidBody* Node = NewObject<UAnimGraphNode_RigidBody>(AnimGraph);

		// Optional physics-asset override (defaults to the mesh's own physics asset when empty).
		FString PhysAssetPath;
		if (Params->TryGetStringField(TEXT("physics_asset"), PhysAssetPath) && !PhysAssetPath.IsEmpty())
		{
			UPhysicsAsset* PhysAsset = LoadObject<UPhysicsAsset>(nullptr, *PhysAssetPath);
			if (!PhysAsset)
			{
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("RigidBody: physics asset not found: %s"), *PhysAssetPath));
			}
			Node->Node.OverridePhysicsAsset = PhysAsset;
		}

		// simulation_space: "ComponentSpace" (default) | "WorldSpace" | "BaseBoneSpace"
		FString SimSpace;
		if (Params->TryGetStringField(TEXT("simulation_space"), SimSpace))
		{
			if (SimSpace.Equals(TEXT("WorldSpace"), ESearchCase::IgnoreCase))         Node->Node.SimulationSpace = ESimulationSpace::WorldSpace;
			else if (SimSpace.Equals(TEXT("BaseBoneSpace"), ESearchCase::IgnoreCase)) Node->Node.SimulationSpace = ESimulationSpace::BaseBoneSpace;
			else                                                                       Node->Node.SimulationSpace = ESimulationSpace::ComponentSpace;
		}

		if (Params->HasField(TEXT("override_world_gravity_z")))
		{
			Node->Node.bOverrideWorldGravity = true;
			Node->Node.OverrideWorldGravity = FVector(0, 0, Params->GetNumberField(TEXT("override_world_gravity_z")));
		}

		NewNode = Node;
	}
	else if (NodeType.Equals(TEXT("ApplyAdditive"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_ApplyAdditive* Node = NewObject<UAnimGraphNode_ApplyAdditive>(AnimGraph);

		if (Params->HasField(TEXT("alpha")))
		{
			Node->Node.Alpha = static_cast<float>(Params->GetNumberField(TEXT("alpha")));
		}
		if (Params->HasField(TEXT("lod_threshold")))
		{
			Node->Node.LODThreshold = static_cast<int32>(Params->GetNumberField(TEXT("lod_threshold")));
		}

		NewNode = Node;
	}
	else if (NodeType.Equals(TEXT("DeadBlending"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_DeadBlending* Node = NewObject<UAnimGraphNode_DeadBlending>(AnimGraph);

		// DeadBlending properties are mostly private, configured via details panel.
		// The node is typically placed with defaults and tuned in-editor.

		NewNode = Node;
	}
	else if (NodeType.Equals(TEXT("Slot"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_Slot* Node = NewObject<UAnimGraphNode_Slot>(AnimGraph);

		FString SlotName;
		if (Params->TryGetStringField(TEXT("slot_name"), SlotName))
		{
			Node->Node.SlotName = FName(*SlotName);
		}

		NewNode = Node;
	}
	else if (NodeType.Equals(TEXT("LayeredBoneBlend"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_LayeredBoneBlend* Node = NewObject<UAnimGraphNode_LayeredBoneBlend>(AnimGraph);
		NewNode = Node;
	}
	else if (NodeType.Equals(TEXT("SaveCachedPose"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_SaveCachedPose* Node = NewObject<UAnimGraphNode_SaveCachedPose>(AnimGraph);

		FString CacheName;
		if (Params->TryGetStringField(TEXT("cache_name"), CacheName))
		{
			Node->CacheName = CacheName;
		}

		NewNode = Node;
	}
	else if (NodeType.Equals(TEXT("UseCachedPose"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_UseCachedPose* Node = NewObject<UAnimGraphNode_UseCachedPose>(AnimGraph);
		NewNode = Node;
	}
	else if (NodeType.Equals(TEXT("Mirror"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_Mirror* Node = NewObject<UAnimGraphNode_Mirror>(AnimGraph);

		// Optional: pre-bind the MirrorDataTable asset on creation. Without this the
		// node defaults to nullptr and the runtime mirror won't apply until set in details.
		FString MirrorTablePath;
		if (Params->TryGetStringField(TEXT("mirror_data_table_path"), MirrorTablePath))
		{
			if (UMirrorDataTable* MirrorTable = LoadObject<UMirrorDataTable>(nullptr, *MirrorTablePath))
			{
				Node->Node.SetMirrorDataTable(MirrorTable);
			}
		}

		NewNode = Node;
	}
	else if (NodeType.Equals(TEXT("OrientationWarping"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_OrientationWarping* Node = NewObject<UAnimGraphNode_OrientationWarping>(AnimGraph);

		// Rotation axis
		FString AxisStr;
		if (Params->TryGetStringField(TEXT("rotation_axis"), AxisStr))
		{
			if (AxisStr.Equals(TEXT("X"), ESearchCase::IgnoreCase)) Node->Node.RotationAxis = EAxis::X;
			else if (AxisStr.Equals(TEXT("Y"), ESearchCase::IgnoreCase)) Node->Node.RotationAxis = EAxis::Y;
			else if (AxisStr.Equals(TEXT("Z"), ESearchCase::IgnoreCase)) Node->Node.RotationAxis = EAxis::Z;
		}

		if (Params->HasField(TEXT("rotation_interp_speed")))
		{
			Node->Node.RotationInterpSpeed = static_cast<float>(Params->GetNumberField(TEXT("rotation_interp_speed")));
		}
		if (Params->HasField(TEXT("distributed_bone_orientation_alpha")))
		{
			Node->Node.DistributedBoneOrientationAlpha = static_cast<float>(Params->GetNumberField(TEXT("distributed_bone_orientation_alpha")));
		}

		// Spine bones
		const TArray<TSharedPtr<FJsonValue>>* SpineBonesArray = nullptr;
		if (Params->TryGetArrayField(TEXT("spine_bones"), SpineBonesArray))
		{
			for (const auto& BoneValue : *SpineBonesArray)
			{
				FBoneReference BoneRef;
				BoneRef.BoneName = FName(*BoneValue->AsString());
				Node->Node.SpineBones.Add(BoneRef);
			}
		}

		// IK foot root bone
		FString IKFootRoot;
		if (Params->TryGetStringField(TEXT("ik_foot_root_bone"), IKFootRoot))
		{
			Node->Node.IKFootRootBone.BoneName = FName(*IKFootRoot);
		}

		// IK foot bones
		const TArray<TSharedPtr<FJsonValue>>* IKFootArray = nullptr;
		if (Params->TryGetArrayField(TEXT("ik_foot_bones"), IKFootArray))
		{
			for (const auto& BoneValue : *IKFootArray)
			{
				FBoneReference BoneRef;
				BoneRef.BoneName = FName(*BoneValue->AsString());
				Node->Node.IKFootBones.Add(BoneRef);
			}
		}

		NewNode = Node;
	}
	else if (NodeType.Equals(TEXT("StrideWarping"), ESearchCase::IgnoreCase))
	{
		UAnimGraphNode_StrideWarping* Node = NewObject<UAnimGraphNode_StrideWarping>(AnimGraph);

		// Pelvis bone
		FString PelvisBone;
		if (Params->TryGetStringField(TEXT("pelvis_bone"), PelvisBone))
		{
			Node->Node.PelvisBone.BoneName = FName(*PelvisBone);
		}

		// IK foot root bone
		FString IKFootRoot;
		if (Params->TryGetStringField(TEXT("ik_foot_root_bone"), IKFootRoot))
		{
			Node->Node.IKFootRootBone.BoneName = FName(*IKFootRoot);
		}

		// Foot definitions - array of objects with ik_foot_bone, fk_foot_bone, thigh_bone
		const TArray<TSharedPtr<FJsonValue>>* FootDefsArray = nullptr;
		if (Params->TryGetArrayField(TEXT("foot_definitions"), FootDefsArray))
		{
			for (const auto& FootVal : *FootDefsArray)
			{
				const TSharedPtr<FJsonObject>* FootObjPtr = nullptr;
				if (FootVal->TryGetObject(FootObjPtr) && FootObjPtr && FootObjPtr->IsValid())
				{
					FStrideWarpingFootDefinition FootDef;
					FString BoneName;
					if ((*FootObjPtr)->TryGetStringField(TEXT("ik_foot_bone"), BoneName))
					{
						FootDef.IKFootBone.BoneName = FName(*BoneName);
					}
					if ((*FootObjPtr)->TryGetStringField(TEXT("fk_foot_bone"), BoneName))
					{
						FootDef.FKFootBone.BoneName = FName(*BoneName);
					}
					if ((*FootObjPtr)->TryGetStringField(TEXT("thigh_bone"), BoneName))
					{
						FootDef.ThighBone.BoneName = FName(*BoneName);
					}
					Node->Node.FootDefinitions.Add(FootDef);
				}
			}
		}

		if (Params->HasField(TEXT("stride_scale")))
		{
			Node->Node.StrideScale = static_cast<float>(Params->GetNumberField(TEXT("stride_scale")));
		}

		NewNode = Node;
	}
	else
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unknown anim node type: %s. Supported: CopyPoseFromMesh, AnimDynamics, RigidBody, ApplyAdditive, DeadBlending, Slot, LayeredBoneBlend, SaveCachedPose, UseCachedPose, Mirror, OrientationWarping, StrideWarping"), *NodeType));
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

// ============================================================================
// Phase 4: delete_anim_node
//
// Counterpart to add_anim_node. Matches the int UniqueID returned by
// add_anim_node (the generic delete_node helper matches on NodeGuid/Name and
// can't reach state-inner BoundGraphs, so it isn't usable for nodes that this
// API hands back). Honors the same state_machine/state targeting params as
// add_anim_node so callers can delete from a state's inner pose graph.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimBlueprintCommands::HandleDeleteAnimNode(
	const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		if (!Params->TryGetStringField(TEXT("blueprint_name"), AssetPath))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
		}
	}

	FString NodeIdStr;
	if (!Params->TryGetStringField(TEXT("node_id"), NodeIdStr))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
	}

	FString Error;
	UAnimBlueprint* ABP = LoadAnimBlueprint(AssetPath, Error);
	if (!ABP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
	}

	UEdGraph* TargetGraph = ResolveAnimGraphTarget(ABP, Params, Error);
	if (!TargetGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
	}

	// Distinguish int UniqueID (returned by add_anim_node) from a NodeGuid hex
	// string. A guid starting with digits would otherwise pass Atoi and falsely
	// match a UniqueID, so require the whole string to be numeric.
	bool bIdIsNumeric = !NodeIdStr.IsEmpty();
	for (const TCHAR Ch : NodeIdStr)
	{
		if (!FChar::IsDigit(Ch)) { bIdIsNumeric = false; break; }
	}
	const int32 NodeUniqueId = bIdIsNumeric ? FCString::Atoi(*NodeIdStr) : 0;

	UEdGraphNode* TargetNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;
		if (bIdIsNumeric && Node->GetUniqueID() == NodeUniqueId) { TargetNode = Node; break; }
		if (Node->NodeGuid.ToString().Equals(NodeIdStr, ESearchCase::IgnoreCase)) { TargetNode = Node; break; }
		if (Node->GetName().Equals(NodeIdStr, ESearchCase::IgnoreCase)) { TargetNode = Node; break; }
	}

	if (!TargetNode)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Node id '%s' not found in target graph '%s'"),
				*NodeIdStr, *TargetGraph->GetName()));
	}

	const FString DeletedGuid = TargetNode->NodeGuid.ToString();
	TargetNode->BreakAllNodeLinks();
	TargetGraph->RemoveNode(TargetNode);
	TargetGraph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("deleted_node_id"), NodeIdStr);
	Result->SetStringField(TEXT("deleted_node_guid"), DeletedGuid);
	Result->SetStringField(TEXT("graph"), TargetGraph->GetName());
	return Result;
}
