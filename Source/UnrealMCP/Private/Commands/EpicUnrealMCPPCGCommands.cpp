#include "Commands/EpicUnrealMCPPCGCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGSettings.h"
#include "PCGComponent.h"
#include "PCGData.h"
#include "PCGManagedResource.h"
#include "PCGWorldActor.h"
#include "Grid/PCGPartitionActor.h"
#include "Subsystems/PCGSubsystem.h"
#include "RuntimeGen/PCGGenSourceManager.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorLoaderAdapter.h"
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include <type_traits>
#endif

namespace
{
#if WITH_EDITOR

	UPCGGraph* LoadPCGGraph(const FString& AssetPath, FString& OutError)
	{
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		if (!Loaded)
		{
			OutError = FString::Printf(TEXT("Failed to load asset '%s'."), *AssetPath);
			return nullptr;
		}
		UPCGGraph* Graph = Cast<UPCGGraph>(Loaded);
		if (!Graph)
		{
			OutError = FString::Printf(TEXT("Asset '%s' is %s, not a UPCGGraph."),
				*AssetPath, *Loaded->GetClass()->GetName());
			return nullptr;
		}
		return Graph;
	}

	// The stable, rename-proof token for addressing a node in other tools:
	// "input"/"output" for the implicit boundary nodes, else the UObject name
	// (GetName()). NodeTitle is cosmetic and auto-regenerates — never address by it.
	FString NodeToken(const UPCGGraph& Graph, const UPCGNode* Node)
	{
		if (!Node) { return TEXT("?"); }
		if (Node == Graph.GetInputNode())  { return TEXT("input"); }
		if (Node == Graph.GetOutputNode()) { return TEXT("output"); }
		return Node->GetName();
	}

	// Resolve a node by an addressing token. Stable forms (preferred):
	//   "input" / "output"   implicit boundary nodes
	//   "#N" or "index:N"     position in GetNodes() — stable within a snapshot
	//   <UObject name>        GetName(), stable for the node's lifetime
	// Fragile fallback (flagged in OutResolvedBy):
	//   <NodeTitle>           cosmetic title — auto-renamed by PCG; avoid.
	// OutResolvedBy is set to "boundary" | "index" | "fname" | "title".
	UPCGNode* FindNodeByName(UPCGGraph& Graph, const FString& Name, FString& OutError, FString* OutResolvedBy = nullptr)
	{
		auto SetResolved = [OutResolvedBy](const TCHAR* How)
		{
			if (OutResolvedBy) { *OutResolvedBy = How; }
		};

		const FString Trimmed = Name.TrimStartAndEnd();
		const FString Lower = Trimmed.ToLower();
		if (Lower == TEXT("input"))  { SetResolved(TEXT("boundary")); return Graph.GetInputNode(); }
		if (Lower == TEXT("output")) { SetResolved(TEXT("boundary")); return Graph.GetOutputNode(); }

		// Index addressing: "#N" or "index:N".
		FString IndexStr;
		if (Trimmed.StartsWith(TEXT("#")))           { IndexStr = Trimmed.RightChop(1); }
		else if (Lower.StartsWith(TEXT("index:")))   { IndexStr = Trimmed.RightChop(6); }
		if (!IndexStr.IsEmpty() && IndexStr.IsNumeric())
		{
			const int32 Idx = FCString::Atoi(*IndexStr);
			const TArray<UPCGNode*>& Nodes = Graph.GetNodes();
			if (Nodes.IsValidIndex(Idx) && Nodes[Idx])
			{
				SetResolved(TEXT("index"));
				return Nodes[Idx];
			}
			OutError = FString::Printf(TEXT("PCG node index %d out of range (graph has %d nodes)."), Idx, Graph.GetNodes().Num());
			return nullptr;
		}

		const FName NameFN(*Trimmed);
		for (UPCGNode* N : Graph.GetNodes())
		{
			if (N && N->GetFName() == NameFN)
			{
				SetResolved(TEXT("fname"));
				return N;
			}
		}

		// Authored-title fallback — fragile, since PCG regenerates titles. Flagged.
		for (UPCGNode* N : Graph.GetNodes())
		{
			if (N && N->NodeTitle == NameFN)
			{
				SetResolved(TEXT("title"));
				return N;
			}
		}

		// Diagnostic: list stable identifiers (index + name + title) so the caller
		// can re-address by something that won't break.
		TArray<FString> Existing;
		const TArray<UPCGNode*>& Nodes = Graph.GetNodes();
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			if (Nodes[i])
			{
				Existing.Add(FString::Printf(TEXT("#%d=%s(title:%s)"), i, *Nodes[i]->GetName(), *Nodes[i]->NodeTitle.ToString()));
			}
		}
		OutError = FString::Printf(TEXT("PCG node '%s' not found. Address by #index or stable name. Existing: [%s]. Use 'input'/'output' for boundary nodes."),
			*Name, *FString::Join(Existing, TEXT(", ")));
		return nullptr;
	}

	// Serialize a single edge endpoint as {node, pin} using the stable node token.
	TSharedPtr<FJsonObject> SerializePinEndpoint(const UPCGGraph& Graph, const UPCGPin* Pin)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("node"), Pin ? NodeToken(Graph, Pin->Node) : TEXT("?"));
		Obj->SetStringField(TEXT("pin"), Pin ? Pin->Properties.Label.ToString() : TEXT("?"));
		return Obj;
	}

	// Serialize a pin including the endpoints it actually connects to (the wiring
	// the old edge_count-only output hid).
	TSharedPtr<FJsonObject> SerializePin(const UPCGGraph& Graph, UPCGPin& Pin)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("label"), Pin.Properties.Label.ToString());
		P->SetNumberField(TEXT("edge_count"), Pin.Edges.Num());

		TArray<TSharedPtr<FJsonValue>> Connections;
		for (UPCGEdge* Edge : Pin.Edges)
		{
			if (!Edge) { continue; }
			const UPCGPin* Other = Edge->GetOtherPin(&Pin);
			Connections.Add(MakeShared<FJsonValueObject>(SerializePinEndpoint(Graph, Other)));
		}
		P->SetArrayField(TEXT("connections"), Connections);
		return P;
	}

	// Index is the node's position in GetNodes() (INDEX_NONE for boundary nodes).
	// When Graph is provided, pins include their resolved connection endpoints.
	TSharedPtr<FJsonObject> SerializeNode(const UPCGNode& Node, const UPCGGraph* Graph = nullptr, int32 Index = INDEX_NONE)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Node.GetName());
		Obj->SetNumberField(TEXT("index"), Index);
		Obj->SetStringField(TEXT("path"), Node.GetPathName());
		Obj->SetStringField(TEXT("title"), Node.NodeTitle.ToString());
		// The handle callers MUST use for other tools (rename-proof).
		Obj->SetStringField(TEXT("token"), Graph ? NodeToken(*Graph, &Node) : Node.GetName());
		if (UPCGSettings* Settings = Node.GetSettings())
		{
			Obj->SetStringField(TEXT("settings_class"), Settings->GetClass()->GetPathName());
		}
#if WITH_EDITORONLY_DATA
		Obj->SetNumberField(TEXT("pos_x"), Node.PositionX);
		Obj->SetNumberField(TEXT("pos_y"), Node.PositionY);
		Obj->SetStringField(TEXT("comment"), Node.NodeComment);
#endif

		TArray<TSharedPtr<FJsonValue>> InputPins;
		for (UPCGPin* Pin : Node.GetInputPins())
		{
			if (!Pin) { continue; }
			if (Graph)
			{
				InputPins.Add(MakeShared<FJsonValueObject>(SerializePin(*Graph, *Pin)));
			}
			else
			{
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
				P->SetNumberField(TEXT("edge_count"), Pin->Edges.Num());
				InputPins.Add(MakeShared<FJsonValueObject>(P));
			}
		}
		Obj->SetArrayField(TEXT("input_pins"), InputPins);

		TArray<TSharedPtr<FJsonValue>> OutputPins;
		for (UPCGPin* Pin : Node.GetOutputPins())
		{
			if (!Pin) { continue; }
			if (Graph)
			{
				OutputPins.Add(MakeShared<FJsonValueObject>(SerializePin(*Graph, *Pin)));
			}
			else
			{
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
				P->SetNumberField(TEXT("edge_count"), Pin->Edges.Num());
				OutputPins.Add(MakeShared<FJsonValueObject>(P));
			}
		}
		Obj->SetArrayField(TEXT("output_pins"), OutputPins);

		return Obj;
	}

	// ---- Level PCG component lookup (for generation commands) ----------------

	UWorld* GetEditorWorldForPCG()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	AActor* FindActorByLabelOrName(UWorld* World, const FString& ActorName, FString& OutError)
	{
		if (!World)
		{
			OutError = TEXT("No editor world available.");
			return nullptr;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (!A) { continue; }
			if (A->GetActorLabel() == ActorName || A->GetName() == ActorName)
			{
				return A;
			}
		}
		OutError = FString::Printf(TEXT("Actor '%s' not found in the editor world (matched against label and object name)."), *ActorName);
		return nullptr;
	}

	UPCGComponent* FindPCGComponentOnActor(AActor* Actor, const FString& ComponentName, FString& OutError)
	{
		TArray<UPCGComponent*> Components;
		Actor->GetComponents<UPCGComponent>(Components);
		if (Components.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Actor '%s' has no UPCGComponent."), *Actor->GetActorLabel());
			return nullptr;
		}
		if (ComponentName.IsEmpty())
		{
			return Components[0];
		}
		for (UPCGComponent* C : Components)
		{
			if (C && C->GetName() == ComponentName)
			{
				return C;
			}
		}
		OutError = FString::Printf(TEXT("Actor '%s' has no UPCGComponent named '%s'."), *Actor->GetActorLabel(), *ComponentName);
		return nullptr;
	}

	// Build a coverage report: which world regions actually have generated data,
	// from each registered local's APCGPartitionActor bounds. Headless/runtime
	// regen often covers only a sub-region, so this tells the caller WHERE sampling
	// will succeed instead of guessing coordinates.
	TSharedPtr<FJsonObject> BuildCoverageJson(UPCGSubsystem* Subsystem, UPCGComponent* Original)
	{
		TSharedPtr<FJsonObject> Cov = MakeShared<FJsonObject>();
		FBox Union(ForceInit);
		int32 Count = 0;
		TArray<TSharedPtr<FJsonValue>> Locals;

		if (Subsystem && Original)
		{
			Subsystem->ForAllRegisteredLocalComponents(Original,
				[&](UPCGComponent* Local)
				{
					if (!Local) { return; }
					APCGPartitionActor* PA = Cast<APCGPartitionActor>(Local->GetOwner());
					if (!PA) { return; }
					const FBox B = PA->GetFixedBounds();
					if (!B.IsValid) { return; }
					Union += B;
					++Count;
					const FVector C = B.GetCenter();
					const FVector E = B.GetExtent();
					TSharedPtr<FJsonObject> LB = MakeShared<FJsonObject>();
					LB->SetNumberField(TEXT("center_x"), C.X);
					LB->SetNumberField(TEXT("center_y"), C.Y);
					LB->SetNumberField(TEXT("center_z"), C.Z);
					LB->SetNumberField(TEXT("extent_x"), E.X);
					LB->SetNumberField(TEXT("extent_y"), E.Y);
					LB->SetNumberField(TEXT("extent_z"), E.Z);
					Locals.Add(MakeShared<FJsonValueObject>(LB));
				});
		}

		Cov->SetNumberField(TEXT("local_count"), Count);
		if (Count > 0)
		{
			TSharedPtr<FJsonObject> Min = MakeShared<FJsonObject>();
			Min->SetNumberField(TEXT("x"), Union.Min.X);
			Min->SetNumberField(TEXT("y"), Union.Min.Y);
			Min->SetNumberField(TEXT("z"), Union.Min.Z);
			TSharedPtr<FJsonObject> Max = MakeShared<FJsonObject>();
			Max->SetNumberField(TEXT("x"), Union.Max.X);
			Max->SetNumberField(TEXT("y"), Union.Max.Y);
			Max->SetNumberField(TEXT("z"), Union.Max.Z);
			Cov->SetField(TEXT("union_min"), MakeShared<FJsonValueObject>(Min));
			Cov->SetField(TEXT("union_max"), MakeShared<FJsonValueObject>(Max));
		}
		Cov->SetArrayField(TEXT("locals"), Locals);
		return Cov;
	}

	// ---- Generated point-data attribute reading -----------------------------

	// Stringify one supported PCG metadata value type.
	template <typename T>
	FString PCGAttrValueToString(const T& V)
	{
		if constexpr (std::is_same_v<T, FString>)            { return V; }
		else if constexpr (std::is_same_v<T, FName>)         { return V.ToString(); }
		else if constexpr (std::is_same_v<T, bool>)          { return V ? TEXT("true") : TEXT("false"); }
		else if constexpr (std::is_same_v<T, FVector> || std::is_same_v<T, FVector2D> || std::is_same_v<T, FVector4> ||
		                   std::is_same_v<T, FQuat> || std::is_same_v<T, FRotator> || std::is_same_v<T, FTransform> ||
		                   std::is_same_v<T, FSoftObjectPath> || std::is_same_v<T, FSoftClassPath>)
		{
			return V.ToString();
		}
		else { return LexToString(V); } // float, double, int32, int64
	}

	// Read a metadata attribute's value at a point's entry key as text, dispatching
	// on the attribute's runtime type id via the engine's CallbackWithRightType.
	FString StringifyAttributeValue(const FPCGMetadataAttributeBase* Base, int64 EntryKey)
	{
		if (!Base) { return FString(); }
		return PCGMetadataAttribute::CallbackWithRightType(
			static_cast<uint16>(Base->GetTypeId()),
			[Base, EntryKey](auto Dummy) -> FString
			{
				using T = decltype(Dummy);
				const FPCGMetadataAttribute<T>* Typed = static_cast<const FPCGMetadataAttribute<T>*>(Base);
				const T Value = Typed->GetValueFromItemKey(static_cast<PCGMetadataEntryKey>(EntryKey));
				return PCGAttrValueToString<T>(Value);
			});
	}

#endif // WITH_EDITOR
}

FEpicUnrealMCPPCGCommands::FEpicUnrealMCPPCGCommands() {}
FEpicUnrealMCPPCGCommands::~FEpicUnrealMCPPCGCommands() {}

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
#if !WITH_EDITOR
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PCG commands are editor-only."));
#else
	if (CommandType == TEXT("pcg_graph_list_nodes"))              return HandleListNodes(Params);
	if (CommandType == TEXT("pcg_graph_list_edges"))              return HandleListEdges(Params);
	if (CommandType == TEXT("pcg_graph_add_node"))                return HandleAddNode(Params);
	if (CommandType == TEXT("pcg_graph_remove_node"))             return HandleRemoveNode(Params);
	if (CommandType == TEXT("pcg_graph_add_edge"))                return HandleAddEdge(Params);
	if (CommandType == TEXT("pcg_graph_remove_edge"))             return HandleRemoveEdge(Params);
	if (CommandType == TEXT("pcg_graph_set_node_position"))       return HandleSetNodePosition(Params);
	if (CommandType == TEXT("pcg_graph_list_settings_classes"))   return HandleListSettingsClasses(Params);
	if (CommandType == TEXT("pcg_component_generate"))            return HandleComponentGenerate(Params);
	if (CommandType == TEXT("pcg_component_get_state"))           return HandleComponentGetState(Params);
	if (CommandType == TEXT("pcg_sample_point_attributes"))       return HandleSamplePointAttributes(Params);
	if (CommandType == TEXT("pcg_component_output_summary"))      return HandleComponentOutputSummary(Params);
	if (CommandType == TEXT("pcg_component_diagnose"))            return HandleComponentDiagnose(Params);
	if (CommandType == TEXT("pcg_set_filter_target"))            return HandleSetFilterTarget(Params);
	if (CommandType == TEXT("pcg_regen_start"))                  return HandleRegenStart(Params);
	if (CommandType == TEXT("pcg_regen_poll"))                   return HandleRegenPoll(Params);
	if (CommandType == TEXT("pcg_regen_finalize"))               return HandleRegenFinalize(Params);
	if (CommandType == TEXT("pcg_regen_recover"))                return HandleRegenRecover(Params);

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown PCG command: %s"), *CommandType));
#endif
}

#if WITH_EDITOR

// ============================================================================
// pcg_graph_list_nodes
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleListNodes(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	FString Error;
	UPCGGraph* Graph = LoadPCGGraph(AssetPath, Error);
	if (!Graph) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	TArray<TSharedPtr<FJsonValue>> Entries;
	// Include implicit input/output nodes — they're addressable as "input" and "output".
	if (UPCGNode* InputNode = Graph->GetInputNode())
	{
		TSharedPtr<FJsonObject> Obj = SerializeNode(*InputNode, Graph);
		Obj->SetBoolField(TEXT("is_input_node"), true);
		Entries.Add(MakeShared<FJsonValueObject>(Obj));
	}
	if (UPCGNode* OutputNode = Graph->GetOutputNode())
	{
		TSharedPtr<FJsonObject> Obj = SerializeNode(*OutputNode, Graph);
		Obj->SetBoolField(TEXT("is_output_node"), true);
		Entries.Add(MakeShared<FJsonValueObject>(Obj));
	}
	const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (Nodes[i]) { Entries.Add(MakeShared<FJsonValueObject>(SerializeNode(*Nodes[i], Graph, i))); }
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("count"), Entries.Num());
	Result->SetArrayField(TEXT("nodes"), Entries);
	return Result;
}

// ============================================================================
// pcg_graph_add_node — wraps UPCGGraph::AddNodeOfType.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleAddNode(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("settings_class")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required fields: asset_path, settings_class"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString SettingsClassPath = Params->GetStringField(TEXT("settings_class"));

	FString Error;
	UPCGGraph* Graph = LoadPCGGraph(AssetPath, Error);
	if (!Graph) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	UClass* SettingsClass = LoadObject<UClass>(nullptr, *SettingsClassPath);
	if (!SettingsClass || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("settings_class '%s' is not a UPCGSettings subclass. Use pcg_graph_list_settings_classes to discover available types."),
			*SettingsClassPath));
	}

	const bool bUseTransaction = Params->HasField(TEXT("transactional")) ? Params->GetBoolField(TEXT("transactional")) : true;
	TUniquePtr<FScopedTransaction> Transaction;
	if (bUseTransaction)
	{
		Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("PCGMCP", "AddNode", "MCP: Add PCG Node"));
		Graph->Modify();
	}

	UPCGSettings* DefaultSettings = nullptr;
	UPCGNode* NewNode = Graph->AddNodeOfType(SettingsClass, DefaultSettings);
	if (!NewNode)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("AddNodeOfType returned null for '%s'."), *SettingsClassPath));
	}

#if WITH_EDITOR
	if (Params->HasField(TEXT("pos_x")) || Params->HasField(TEXT("pos_y")))
	{
		const int32 X = Params->HasField(TEXT("pos_x")) ? static_cast<int32>(Params->GetNumberField(TEXT("pos_x"))) : 0;
		const int32 Y = Params->HasField(TEXT("pos_y")) ? static_cast<int32>(Params->GetNumberField(TEXT("pos_y"))) : 0;
		NewNode->SetNodePosition(X, Y);
	}
#endif

	if (Params->HasField(TEXT("title")))
	{
		NewNode->NodeTitle = FName(*Params->GetStringField(TEXT("title")));
	}

	Graph->MarkPackageDirty();

	if (Params->HasField(TEXT("save")) ? Params->GetBoolField(TEXT("save")) : true)
	{
		UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty*/ false);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetField(TEXT("node"), MakeShared<FJsonValueObject>(SerializeNode(*NewNode, Graph)));
	return Result;
}

// ============================================================================
// pcg_graph_remove_node — wraps UPCGGraph::RemoveNode.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleRemoveNode(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("node_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required fields: asset_path, node_name"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString NodeName = Params->GetStringField(TEXT("node_name"));

	FString Error;
	UPCGGraph* Graph = LoadPCGGraph(AssetPath, Error);
	if (!Graph) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	UPCGNode* Node = FindNodeByName(*Graph, NodeName, Error);
	if (!Node) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	if (Node == Graph->GetInputNode() || Node == Graph->GetOutputNode())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot remove the graph's implicit input/output node."));
	}

	const bool bUseTransaction = Params->HasField(TEXT("transactional")) ? Params->GetBoolField(TEXT("transactional")) : true;
	TUniquePtr<FScopedTransaction> Transaction;
	if (bUseTransaction)
	{
		Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("PCGMCP", "RemoveNode", "MCP: Remove PCG Node"));
		Graph->Modify();
	}

	Graph->RemoveNode(Node);
	Graph->MarkPackageDirty();

	if (Params->HasField(TEXT("save")) ? Params->GetBoolField(TEXT("save")) : true)
	{
		UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty*/ false);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// ============================================================================
// pcg_graph_add_edge — wraps UPCGGraph::AddEdge.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleAddEdge(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) ||
	    !Params->HasField(TEXT("from_node")) || !Params->HasField(TEXT("from_pin")) ||
	    !Params->HasField(TEXT("to_node")) || !Params->HasField(TEXT("to_pin")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required fields: asset_path, from_node, from_pin, to_node, to_pin"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	FString Error;
	UPCGGraph* Graph = LoadPCGGraph(AssetPath, Error);
	if (!Graph) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	FString FromResolvedBy, ToResolvedBy;
	UPCGNode* FromNode = FindNodeByName(*Graph, Params->GetStringField(TEXT("from_node")), Error, &FromResolvedBy);
	if (!FromNode) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGNode* ToNode = FindNodeByName(*Graph, Params->GetStringField(TEXT("to_node")), Error, &ToResolvedBy);
	if (!ToNode) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	const FName FromPin(*Params->GetStringField(TEXT("from_pin")));
	const FName ToPin(*Params->GetStringField(TEXT("to_pin")));

	const bool bUseTransaction = Params->HasField(TEXT("transactional")) ? Params->GetBoolField(TEXT("transactional")) : true;
	TUniquePtr<FScopedTransaction> Transaction;
	if (bUseTransaction)
	{
		Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("PCGMCP", "AddEdge", "MCP: Add PCG Edge"));
		Graph->Modify();
	}

	UPCGNode* Result = Graph->AddEdge(FromNode, FromPin, ToNode, ToPin);
	if (!Result)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("AddEdge returned null. Check pin labels exist on both nodes and types are compatible."));
	}

	Graph->MarkPackageDirty();

	if (Params->HasField(TEXT("save")) ? Params->GetBoolField(TEXT("save")) : true)
	{
		UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty*/ false);
	}

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetBoolField(TEXT("success"), true);
	// Echo how each endpoint resolved. "title" means the caller addressed by the
	// cosmetic NodeTitle, which PCG can auto-rename — re-address by token/#index.
	ResultJson->SetStringField(TEXT("from_resolved_by"), FromResolvedBy);
	ResultJson->SetStringField(TEXT("to_resolved_by"), ToResolvedBy);
	ResultJson->SetStringField(TEXT("from_token"), NodeToken(*Graph, FromNode));
	ResultJson->SetStringField(TEXT("to_token"), NodeToken(*Graph, ToNode));
	if (FromResolvedBy == TEXT("title") || ToResolvedBy == TEXT("title"))
	{
		ResultJson->SetStringField(TEXT("warning"),
			TEXT("A node was resolved by its cosmetic title, which PCG auto-renames. Use the returned *_token (or #index) for stable addressing."));
	}
	return ResultJson;
}

// ============================================================================
// pcg_graph_remove_edge — wraps UPCGGraph::RemoveEdge.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleRemoveEdge(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) ||
	    !Params->HasField(TEXT("from_node")) || !Params->HasField(TEXT("from_pin")) ||
	    !Params->HasField(TEXT("to_node")) || !Params->HasField(TEXT("to_pin")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required fields: asset_path, from_node, from_pin, to_node, to_pin"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	FString Error;
	UPCGGraph* Graph = LoadPCGGraph(AssetPath, Error);
	if (!Graph) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	UPCGNode* FromNode = FindNodeByName(*Graph, Params->GetStringField(TEXT("from_node")), Error);
	if (!FromNode) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGNode* ToNode = FindNodeByName(*Graph, Params->GetStringField(TEXT("to_node")), Error);
	if (!ToNode) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	const FName FromPin(*Params->GetStringField(TEXT("from_pin")));
	const FName ToPin(*Params->GetStringField(TEXT("to_pin")));

	const bool bUseTransaction = Params->HasField(TEXT("transactional")) ? Params->GetBoolField(TEXT("transactional")) : true;
	TUniquePtr<FScopedTransaction> Transaction;
	if (bUseTransaction)
	{
		Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("PCGMCP", "RemoveEdge", "MCP: Remove PCG Edge"));
		Graph->Modify();
	}

	const bool bRemoved = Graph->RemoveEdge(FromNode, FromPin, ToNode, ToPin);

	Graph->MarkPackageDirty();

	if (bRemoved && (Params->HasField(TEXT("save")) ? Params->GetBoolField(TEXT("save")) : true))
	{
		UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty*/ false);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bRemoved);
	if (!bRemoved)
	{
		Result->SetStringField(TEXT("error"), TEXT("No edge matched the from/to/pin specification."));
	}
	return Result;
}

// ============================================================================
// pcg_graph_set_node_position
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleSetNodePosition(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("node_name")) ||
	    !Params->HasField(TEXT("pos_x")) || !Params->HasField(TEXT("pos_y")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required fields: asset_path, node_name, pos_x, pos_y"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString NodeName = Params->GetStringField(TEXT("node_name"));
	const int32 X = static_cast<int32>(Params->GetNumberField(TEXT("pos_x")));
	const int32 Y = static_cast<int32>(Params->GetNumberField(TEXT("pos_y")));

	FString Error;
	UPCGGraph* Graph = LoadPCGGraph(AssetPath, Error);
	if (!Graph) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	UPCGNode* Node = FindNodeByName(*Graph, NodeName, Error);
	if (!Node) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	const bool bUseTransaction = Params->HasField(TEXT("transactional")) ? Params->GetBoolField(TEXT("transactional")) : true;
	TUniquePtr<FScopedTransaction> Transaction;
	if (bUseTransaction)
	{
		Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("PCGMCP", "SetNodePosition", "MCP: Set PCG Node Position"));
		Node->Modify();
	}

	Node->SetNodePosition(X, Y);
	Graph->MarkPackageDirty();

	if (Params->HasField(TEXT("save")) ? Params->GetBoolField(TEXT("save")) : true)
	{
		UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty*/ false);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// ============================================================================
// pcg_graph_list_settings_classes — enumerate available UPCGSettings subclasses
// so callers know what node types they can add via pcg_graph_add_node.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleListSettingsClasses(const TSharedPtr<FJsonObject>& Params)
{
	const FString Filter = Params->HasField(TEXT("filter"))
		? Params->GetStringField(TEXT("filter")).ToLower()
		: FString();

	const int32 Limit = Params->HasField(TEXT("limit"))
		? static_cast<int32>(Params->GetNumberField(TEXT("limit")))
		: 200;

	TArray<TSharedPtr<FJsonValue>> Entries;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Cls = *It;
		if (!Cls) { continue; }
		if (!Cls->IsChildOf(UPCGSettings::StaticClass())) { continue; }
		if (Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) { continue; }

		const FString Path = Cls->GetPathName();
		if (!Filter.IsEmpty() && !Path.ToLower().Contains(Filter)) { continue; }

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("path"), Path);
		Obj->SetStringField(TEXT("name"), Cls->GetName());
		Entries.Add(MakeShared<FJsonValueObject>(Obj));

		if (Entries.Num() >= Limit) { break; }
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), Entries.Num());
	Result->SetArrayField(TEXT("classes"), Entries);
	return Result;
}

// ============================================================================
// pcg_graph_list_edges — flat edge list for the whole graph. Closes the
// "MCP can't show graph wiring" gap: read the connections instead of inferring
// them by re-running scripts.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleListEdges(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	FString Error;
	UPCGGraph* Graph = LoadPCGGraph(AssetPath, Error);
	if (!Graph) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	// Enumerate each edge exactly once from the output side of every node,
	// including the implicit input node (whose output pins feed the graph).
	TArray<UPCGNode*> AllNodes;
	if (UPCGNode* In = Graph->GetInputNode())  { AllNodes.Add(In); }
	if (UPCGNode* Out = Graph->GetOutputNode()) { AllNodes.Add(Out); }
	for (UPCGNode* N : Graph->GetNodes()) { if (N) { AllNodes.Add(N); } }

	TArray<TSharedPtr<FJsonValue>> Edges;
	for (UPCGNode* Node : AllNodes)
	{
		for (UPCGPin* OutPin : Node->GetOutputPins())
		{
			if (!OutPin) { continue; }
			for (UPCGEdge* Edge : OutPin->Edges)
			{
				if (!Edge) { continue; }
				const UPCGPin* Other = Edge->GetOtherPin(OutPin);
				if (!Other) { continue; }
				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("from_node"), NodeToken(*Graph, Node));
				E->SetStringField(TEXT("from_pin"), OutPin->Properties.Label.ToString());
				E->SetStringField(TEXT("to_node"), NodeToken(*Graph, Other->Node));
				E->SetStringField(TEXT("to_pin"), Other->Properties.Label.ToString());
				Edges.Add(MakeShared<FJsonValueObject>(E));
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("count"), Edges.Num());
	Result->SetArrayField(TEXT("edges"), Edges);
	return Result;
}

// ============================================================================
// pcg_component_generate — trigger (re)generation on an actor's PCG component.
// Generation is ASYNC: this returns immediately after scheduling. Poll
// pcg_component_get_state until is_generating is false before measuring results.
// NEVER act on output read before generation completes.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleComponentGenerate(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("actor_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: actor_name"));
	}
	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	const FString ComponentName = Params->HasField(TEXT("component_name")) ? Params->GetStringField(TEXT("component_name")) : FString();
	const bool bForce = Params->HasField(TEXT("force")) ? Params->GetBoolField(TEXT("force")) : true;

	FString Error;
	AActor* Actor = FindActorByLabelOrName(GetEditorWorldForPCG(), ActorName, Error);
	if (!Actor) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGComponent* Component = FindPCGComponentOnActor(Actor, ComponentName, Error);
	if (!Component) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	Component->GenerateLocal(bForce);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("component"), Component->GetName());
	Result->SetBoolField(TEXT("is_generating"), Component->IsGenerating());
	Result->SetStringField(TEXT("note"), TEXT("Generation is async. Poll pcg_component_get_state until is_generating=false before reading/measuring results."));
	return Result;
}

// ============================================================================
// pcg_component_get_state — poll target for async generation completion.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleComponentGetState(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("actor_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: actor_name"));
	}
	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	const FString ComponentName = Params->HasField(TEXT("component_name")) ? Params->GetStringField(TEXT("component_name")) : FString();

	FString Error;
	AActor* Actor = FindActorByLabelOrName(GetEditorWorldForPCG(), ActorName, Error);
	if (!Actor) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGComponent* Component = FindPCGComponentOnActor(Actor, ComponentName, Error);
	if (!Component) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("component"), Component->GetName());
	Result->SetBoolField(TEXT("is_generating"), Component->IsGenerating());
	Result->SetNumberField(TEXT("output_data_count"), Component->GetGeneratedGraphOutput().TaggedData.Num());
	return Result;
}

// ============================================================================
// pcg_sample_point_attributes — read the actual attribute VALUES on the nearest
// generated point to a world location. Works for HiGen-partitioned components
// (whose original GetGeneratedGraphOutput() is empty) by querying the per-
// partition local components via the subsystem. This replaces guessing surface
// names with reading them.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleSamplePointAttributes(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("actor_name")) || !Params->HasField(TEXT("world_x")) || !Params->HasField(TEXT("world_y")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required fields: actor_name, world_x, world_y"));
	}
	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	const FString ComponentName = Params->HasField(TEXT("component_name")) ? Params->GetStringField(TEXT("component_name")) : FString();
	const double WX = Params->GetNumberField(TEXT("world_x"));
	const double WY = Params->GetNumberField(TEXT("world_y"));
	const double WZ = Params->HasField(TEXT("world_z")) ? Params->GetNumberField(TEXT("world_z")) : 0.0;
	const double Radius = Params->HasField(TEXT("radius")) ? Params->GetNumberField(TEXT("radius")) : 10000.0;
	const FVector Location(WX, WY, WZ);

	UWorld* World = GetEditorWorldForPCG();
	FString Error;
	AActor* Actor = FindActorByLabelOrName(World, ActorName, Error);
	if (!Actor) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGComponent* Original = FindPCGComponentOnActor(Actor, ComponentName, Error);
	if (!Original) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	UPCGSubsystem* Subsystem = UPCGSubsystem::GetInstance(World);
	if (!Subsystem) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("UPCGSubsystem unavailable for the editor world.")); }

	// Gather the local components whose bounds intersect the query region; fall
	// back to the original component for non-partitioned setups.
	TArray<UPCGComponent*> Locals;
	const FBoxCenterAndExtent QueryBounds(Location, FVector(Radius));
	Subsystem->ForAllRegisteredIntersectingLocalComponents(Original, QueryBounds,
		[&Locals](UPCGComponent* Local) { if (Local) { Locals.Add(Local); } });
	if (Locals.Num() == 0) { Locals.Add(Original); }

	const UPCGBasePointData* BestData = nullptr;
	int32 BestIndex = INDEX_NONE;
	double BestDistSq = TNumericLimits<double>::Max();
	FVector BestPos = FVector::ZeroVector;
	int64 BestEntryKey = -1;

	for (UPCGComponent* Local : Locals)
	{
		const FPCGDataCollection& Output = Local->GetGeneratedGraphOutput();
		for (const FPCGTaggedData& Tagged : Output.TaggedData)
		{
			const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(Tagged.Data.Get());
			if (!PointData) { continue; }
			const int32 NumPoints = PointData->GetNumPoints();
			if (NumPoints == 0) { continue; }

			FConstPCGPointValueRanges Ranges(PointData);
			for (int32 i = 0; i < NumPoints; ++i)
			{
				const FVector P = Ranges.TransformRange[i].GetLocation();
				const double DistSq = FVector::DistSquared(P, Location);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestData = PointData;
					BestIndex = i;
					BestPos = P;
					BestEntryKey = Ranges.MetadataEntryRange[i];
				}
			}
		}
	}

	if (!BestData || BestIndex == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("No generated points found near the location. The component may be ungenerated, the HiGen partitions covering this location may not be loaded, or no point data is in the output. For GENERATE_AT_RUNTIME components, ensure the region has been generated."));
	}

	TSharedPtr<FJsonObject> Attrs = MakeShared<FJsonObject>();
	int32 AttrCount = 0;
	if (const UPCGMetadata* Meta = BestData->ConstMetadata())
	{
		TArray<FName> Names;
		TArray<EPCGMetadataTypes> Types;
		Meta->GetAttributes(Names, Types);
		for (int32 a = 0; a < Names.Num(); ++a)
		{
			const FPCGMetadataAttributeBase* Base = Meta->GetConstAttribute(Names[a]);
			Attrs->SetStringField(Names[a].ToString(), StringifyAttributeValue(Base, BestEntryKey));
			++AttrCount;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("component"), Original->GetName());

	TSharedPtr<FJsonObject> Queried = MakeShared<FJsonObject>();
	Queried->SetNumberField(TEXT("x"), WX);
	Queried->SetNumberField(TEXT("y"), WY);
	Queried->SetNumberField(TEXT("z"), WZ);
	Result->SetField(TEXT("queried_location"), MakeShared<FJsonValueObject>(Queried));

	TSharedPtr<FJsonObject> Nearest = MakeShared<FJsonObject>();
	Nearest->SetNumberField(TEXT("x"), BestPos.X);
	Nearest->SetNumberField(TEXT("y"), BestPos.Y);
	Nearest->SetNumberField(TEXT("z"), BestPos.Z);
	Nearest->SetNumberField(TEXT("distance"), FMath::Sqrt(BestDistSq));
	Result->SetField(TEXT("nearest_point"), MakeShared<FJsonValueObject>(Nearest));

	Result->SetNumberField(TEXT("attribute_count"), AttrCount);
	Result->SetObjectField(TEXT("attributes"), Attrs);
	return Result;
}

// ============================================================================
// pcg_component_output_summary — instance count per static mesh across a
// component's generated ISM/HISM managed resources (partition-aware).
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleComponentOutputSummary(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("actor_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: actor_name"));
	}
	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	const FString ComponentName = Params->HasField(TEXT("component_name")) ? Params->GetStringField(TEXT("component_name")) : FString();

	UWorld* World = GetEditorWorldForPCG();
	FString Error;
	AActor* Actor = FindActorByLabelOrName(World, ActorName, Error);
	if (!Actor) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGComponent* Original = FindPCGComponentOnActor(Actor, ComponentName, Error);
	if (!Original) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	UPCGSubsystem* Subsystem = UPCGSubsystem::GetInstance(World);
	if (!Subsystem) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("UPCGSubsystem unavailable for the editor world.")); }

	// All registered locals (whole component, not just an intersection); fall back
	// to the original for non-partitioned setups.
	TArray<UPCGComponent*> Comps;
	Subsystem->ForAllRegisteredLocalComponents(Original,
		[&Comps](UPCGComponent* Local) { if (Local) { Comps.Add(Local); } });
	if (Comps.Num() == 0) { Comps.Add(Original); }

	TMap<FString, int64> CountsByMesh;
	int32 InaccessibleComps = 0;
	for (UPCGComponent* Comp : Comps)
	{
		if (!Comp) { continue; }
		if (!Comp->AreManagedResourcesAccessible())
		{
			++InaccessibleComps; // mid-generation; skip rather than report a partial lie
			continue;
		}
		Comp->ForEachConstManagedResource(
			[&CountsByMesh](const UPCGManagedResource* Res)
			{
				const UPCGManagedISMComponent* IsmRes = Cast<UPCGManagedISMComponent>(Res);
				if (!IsmRes) { return; }
				const UInstancedStaticMeshComponent* ISM = IsmRes->GetComponent();
				if (!ISM) { return; }
				const UStaticMesh* Mesh = ISM->GetStaticMesh();
				const FString Key = Mesh ? Mesh->GetPathName() : TEXT("None");
				CountsByMesh.FindOrAdd(Key) += ISM->GetInstanceCount();
			});
	}

	TArray<TSharedPtr<FJsonValue>> Meshes;
	int64 TotalInstances = 0;
	for (const TPair<FString, int64>& Pair : CountsByMesh)
	{
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("static_mesh"), Pair.Key);
		M->SetNumberField(TEXT("instance_count"), Pair.Value);
		Meshes.Add(MakeShared<FJsonValueObject>(M));
		TotalInstances += Pair.Value;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("component"), Original->GetName());
	Result->SetNumberField(TEXT("local_component_count"), Comps.Num());
	Result->SetNumberField(TEXT("mesh_count"), Meshes.Num());
	Result->SetNumberField(TEXT("total_instances"), TotalInstances);
	Result->SetArrayField(TEXT("meshes"), Meshes);
	// Coverage: the world regions these counts actually cover. For a headless/runtime
	// regen this is a sub-region, NOT the whole world — counts are a representative
	// sample, not a world total. Sample (pcg_sample_point_attributes) within union_min..union_max.
	Result->SetField(TEXT("coverage"), MakeShared<FJsonValueObject>(BuildCoverageJson(Subsystem, Original)));
	if (InaccessibleComps > 0)
	{
		// Surface partial coverage rather than silently undercounting.
		Result->SetNumberField(TEXT("inaccessible_components"), InaccessibleComps);
		Result->SetStringField(TEXT("warning"),
			FString::Printf(TEXT("%d local component(s) were mid-generation and skipped; counts are incomplete. Re-run after generation settles (pcg_component_get_state.is_generating=false)."), InaccessibleComps));
	}
	return Result;
}

// ============================================================================
// pcg_component_diagnose — explain WHY a component is/ isn't producing, in one
// call. Built to turn "generate() is a no-op" from a multi-step hunt into a
// single structured answer.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleComponentDiagnose(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("actor_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: actor_name"));
	}
	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	const FString ComponentName = Params->HasField(TEXT("component_name")) ? Params->GetStringField(TEXT("component_name")) : FString();

	UWorld* World = GetEditorWorldForPCG();
	FString Error;
	AActor* Actor = FindActorByLabelOrName(World, ActorName, Error);
	if (!Actor) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGComponent* Comp = FindPCGComponentOnActor(Actor, ComponentName, Error);
	if (!Comp) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	UPCGSubsystem* Subsystem = UPCGSubsystem::GetInstance(World);

	// Generation trigger (public field; no getter).
	const EPCGComponentGenerationTrigger Trigger = Comp->GenerationTrigger;
	const TCHAR* TriggerStr =
		Trigger == EPCGComponentGenerationTrigger::GenerateOnLoad   ? TEXT("GenerateOnLoad")   :
		Trigger == EPCGComponentGenerationTrigger::GenerateOnDemand ? TEXT("GenerateOnDemand") :
		Trigger == EPCGComponentGenerationTrigger::GenerateAtRuntime? TEXT("GenerateAtRuntime"): TEXT("Unknown");
	const bool bRuntime = Comp->IsManagedByRuntimeGenSystem();
	const bool bPartitioned = Comp->IsPartitioned();
	const bool bGraphNull = (Comp->GetGraph() == nullptr);
	const bool bActivated = Comp->bActivated;
	const bool bGenerating = Comp->IsGenerating();

	// Partition / produced state (the reliable "has it produced" signal for HiGen).
	int32 MappingCount = 0;
	int32 LocalCount = 0;
	int32 ProducedLocals = 0;
	if (Subsystem && bPartitioned)
	{
		MappingCount = Subsystem->GetPCGComponentPartitionActorMappings(Comp).Num();
		Subsystem->ForAllRegisteredLocalComponents(Comp,
			[&LocalCount, &ProducedLocals](UPCGComponent* Local)
			{
				if (!Local) { return; }
				++LocalCount;
				if (Local->GetGeneratedGraphOutput().TaggedData.Num() > 0) { ++ProducedLocals; }
			});
	}

	// Active runtime generation sources (the "is anything driving it" signal).
	int32 GenSourceCount = -1;
	if (Subsystem)
	{
		if (FPCGGenSourceManager* Mgr = Subsystem->GetGenSourceManager())
		{
			if (APCGWorldActor* WorldActor = Subsystem->GetPCGWorldActor())
			{
				GenSourceCount = Mgr->GetAllGenSources(WorldActor).Num();
			}
		}
	}

	// Synthesize a human-readable reason for the most common "nothing happens" cases.
	FString Reason;
	if (bGraphNull)
	{
		Reason = TEXT("No graph assigned — assign a UPCGGraph before generating.");
	}
	else if (!bActivated)
	{
		Reason = TEXT("Component is deactivated (bActivated=false).");
	}
	else if (Trigger == EPCGComponentGenerationTrigger::GenerateAtRuntime)
	{
		if (GenSourceCount == 0)
		{
			Reason = TEXT("GenerateAtRuntime with zero active generation sources: generate()/regenerate are no-ops and nothing drives it headlessly. Move a generation source (or the viewport camera) into range, or use a transient gen source.");
		}
		else
		{
			Reason = FString::Printf(TEXT("GenerateAtRuntime; %d active generation source(s); %d/%d partition locals have produced output."), GenSourceCount, ProducedLocals, LocalCount);
		}
	}
	else if (bPartitioned && ProducedLocals == 0)
	{
		Reason = TEXT("Partitioned and no local has produced output yet — call pcg_component_generate (or load the partitions covering the region).");
	}
	else if (bGenerating)
	{
		Reason = TEXT("Currently generating — poll pcg_component_get_state until is_generating=false before reading output.");
	}
	else
	{
		Reason = FString::Printf(TEXT("Appears healthy: trigger=%s, partitioned=%s, produced_locals=%d/%d."),
			TriggerStr, bPartitioned ? TEXT("true") : TEXT("false"), ProducedLocals, LocalCount);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("component"), Comp->GetName());
	Result->SetStringField(TEXT("generation_trigger"), TriggerStr);
	Result->SetBoolField(TEXT("is_runtime_managed"), bRuntime);
	Result->SetBoolField(TEXT("is_partitioned"), bPartitioned);
	Result->SetBoolField(TEXT("is_local_component"), Comp->IsLocalComponent());
	Result->SetBoolField(TEXT("has_graph"), !bGraphNull);
	Result->SetBoolField(TEXT("activated"), bActivated);
	Result->SetBoolField(TEXT("is_generating"), bGenerating);
	// bGenerated is unreliable on partitioned originals; report it but lead with produced_locals.
	Result->SetBoolField(TEXT("b_generated_flag"), Comp->bGenerated);
	Result->SetBoolField(TEXT("managed_resources_accessible"), Comp->AreManagedResourcesAccessible());
	Result->SetNumberField(TEXT("partition_actor_mappings"), MappingCount);
	Result->SetNumberField(TEXT("registered_local_components"), LocalCount);
	Result->SetNumberField(TEXT("produced_local_components"), ProducedLocals);
	Result->SetNumberField(TEXT("active_generation_sources"), GenSourceCount);
	Result->SetNumberField(TEXT("generation_grid_size"), static_cast<double>(Comp->GetGenerationGridSize()));
	Result->SetStringField(TEXT("diagnosis"), Reason);
	return Result;
}

// ============================================================================
// pcg_set_filter_target — set a node's attribute-selector to target a named
// attribute via the real FPCGAttributePropertySelector::SetAttributeName API,
// removing the import_text("PCGBegin(X)PCGEnd") magic string. The selector's
// fields are protected (hence Python needed the string round-trip); C++ calls
// the setter directly. Works on any node whose settings carry a selector.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleSetFilterTarget(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("node")) || !Params->HasField(TEXT("attribute_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required fields: asset_path, node, attribute_name"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString NodeName = Params->GetStringField(TEXT("node"));
	const FString AttributeName = Params->GetStringField(TEXT("attribute_name"));
	const FString PropertyName = Params->HasField(TEXT("property_name")) ? Params->GetStringField(TEXT("property_name")) : FString();

	FString Error;
	UPCGGraph* Graph = LoadPCGGraph(AssetPath, Error);
	if (!Graph) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	FString ResolvedBy;
	UPCGNode* Node = FindNodeByName(*Graph, NodeName, Error, &ResolvedBy);
	if (!Node) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }

	UPCGSettings* Settings = Node->GetSettings();
	if (!Settings)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Node has no settings object."));
	}
	UClass* SettingsClass = Settings->GetClass();

	// "Filter target" is an FPCGAttributePropertySelector on most filter nodes
	// (e.g. UPCGAttributeFilteringSettings::TargetAttribute) but a plain FName on
	// UPCGFilterByAttributeSettings::Attribute. Resolve either form. Preference:
	// explicit property_name, then the conventional names, then first selector.
	FStructProperty* SelectorProp = nullptr;
	FNameProperty* NameProp = nullptr;

	auto ClassifyProp = [](FProperty* P, FStructProperty*& OutSel, FNameProperty*& OutName)
	{
		if (FStructProperty* SP = CastField<FStructProperty>(P))
		{
			if (SP->Struct && SP->Struct->IsChildOf(FPCGAttributePropertySelector::StaticStruct())) { OutSel = SP; }
		}
		else if (FNameProperty* NP = CastField<FNameProperty>(P))
		{
			OutName = NP;
		}
	};

	if (!PropertyName.IsEmpty())
	{
		ClassifyProp(SettingsClass->FindPropertyByName(*PropertyName), SelectorProp, NameProp);
		if (!SelectorProp && !NameProp)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Property '%s' on %s is neither an FPCGAttributePropertySelector nor an FName."), *PropertyName, *SettingsClass->GetName()));
		}
	}
	else
	{
		// Conventional target property names first.
		static const TCHAR* Preferred[] = { TEXT("TargetAttribute"), TEXT("Attribute") };
		for (const TCHAR* Name : Preferred)
		{
			ClassifyProp(SettingsClass->FindPropertyByName(Name), SelectorProp, NameProp);
			if (SelectorProp || NameProp) { break; }
		}
		// Fall back to the first selector-typed property on the settings.
		if (!SelectorProp && !NameProp)
		{
			TArray<FString> SelectorProps;
			for (TFieldIterator<FStructProperty> It(SettingsClass); It; ++It)
			{
				if (It->Struct && It->Struct->IsChildOf(FPCGAttributePropertySelector::StaticStruct()))
				{
					if (!SelectorProp) { SelectorProp = *It; }
					SelectorProps.Add(It->GetName());
				}
			}
			if (SelectorProps.Num() > 1)
			{
				Error = FString::Printf(TEXT("Multiple selector properties on %s: [%s]. Used '%s'. Pass property_name to choose."),
					*SettingsClass->GetName(), *FString::Join(SelectorProps, TEXT(", ")), *SelectorProp->GetName());
			}
		}
		if (!SelectorProp && !NameProp)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Node settings %s have no attribute selector or FName target. Pass property_name explicitly."), *SettingsClass->GetName()));
		}
	}

	const bool bUseTransaction = Params->HasField(TEXT("transactional")) ? Params->GetBoolField(TEXT("transactional")) : true;
	TUniquePtr<FScopedTransaction> Transaction;
	if (bUseTransaction)
	{
		Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("PCGMCP", "SetFilterTarget", "MCP: Set PCG Filter Target"));
		Settings->Modify();
	}

	FString AppliedProperty;
	FString ResultingValue;
	bool bChanged = false;
	if (SelectorProp)
	{
		// The selector's fields are protected — call the real setter, not ImportText.
		FPCGAttributePropertySelector* Selector = SelectorProp->ContainerPtrToValuePtr<FPCGAttributePropertySelector>(Settings);
		bChanged = Selector->SetAttributeName(FName(*AttributeName));
		AppliedProperty = SelectorProp->GetName();
		ResultingValue = Selector->ToString();
	}
	else // NameProp — plain FName target (e.g. UPCGFilterByAttributeSettings::Attribute)
	{
		FName* NamePtr = NameProp->ContainerPtrToValuePtr<FName>(Settings);
		const FName NewName(*AttributeName);
		bChanged = (*NamePtr != NewName);
		*NamePtr = NewName;
		AppliedProperty = NameProp->GetName();
		ResultingValue = NewName.ToString();
	}

	// Notify via the public settings-changed delegate so the node invalidates its
	// executor CRC. UPCGSettings::PostEditChangeProperty is protected; broadcasting
	// OnSettingsChangedDelegate is the public path the node listens on. MarkPackageDirty
	// alone would persist the value but not force re-evaluation.
	Settings->OnSettingsChangedDelegate.Broadcast(Settings, EPCGChangeType::Settings);
	Graph->MarkPackageDirty();

	if (Params->HasField(TEXT("save")) ? Params->GetBoolField(TEXT("save")) : true)
	{
		UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty*/ false);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node"), NodeToken(*Graph, Node));
	Result->SetStringField(TEXT("property"), AppliedProperty);
	Result->SetStringField(TEXT("attribute"), AttributeName);
	Result->SetBoolField(TEXT("changed"), bChanged);
	Result->SetStringField(TEXT("resulting_selector"), ResultingValue);
	if (!Error.IsEmpty())
	{
		Result->SetStringField(TEXT("warning"), Error);
	}
	return Result;
}

// ============================================================================
// Headless editor regeneration of a partitioned GENERATE_AT_RUNTIME component.
//
// The runtime scheduler is unexported and only the viewport camera drives it, so
// generate() is a no-op. The working mechanism (verified against PCG source):
// temporarily flip the original's trigger to GenerateOnDemand (keeping it
// partitioned) — which removes it from the scheduler's view and routes it through
// the editor partitioned-generate path producing the SAME HiGen result — then
// read, then restore.
//
// Generation is async over editor ticks (no public synchronous flush exists; the
// executor is protected + time-sliced), so this is a STATEFUL 3-call flow:
//   pcg_regen_start  -> flip + kick generation, return
//   pcg_regen_poll   -> until is_generating=false (editor idle ticks drive it)
//   <read via pcg_sample_point_attributes / pcg_component_output_summary>
//   pcg_regen_finalize -> cleanup the on-demand locals + restore the trigger
// pcg_regen_recover restores any component left flipped by a skipped finalize.
//
// ActiveRegens (member) maps component path -> saved trigger so a forgotten
// finalize is recoverable within the session; the live GenerationTrigger field is
// the ground-truth signal (a component reading GenerateOnDemand that should be
// runtime is a leaked regen).
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleRegenStart(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("actor_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: actor_name"));
	}
	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	const FString ComponentName = Params->HasField(TEXT("component_name")) ? Params->GetStringField(TEXT("component_name")) : FString();

	UWorld* World = GetEditorWorldForPCG();
	FString Error;
	AActor* Actor = FindActorByLabelOrName(World, ActorName, Error);
	if (!Actor) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGComponent* Original = FindPCGComponentOnActor(Actor, ComponentName, Error);
	if (!Original) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGSubsystem* Subsystem = UPCGSubsystem::GetInstance(World);
	if (!Subsystem) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("UPCGSubsystem unavailable.")); }

	const FString Key = Original->GetPathName();
	if (ActiveRegens.Contains(Key))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("A regen is already in flight for this component. Poll/finalize it first, or call pcg_regen_recover."));
	}

	// Optional: pin the World Partition cells around a target location BEFORE generating.
	// The editor on-demand path only creates partition actors for LOADED WP cells, so
	// without this the regen produces 0 locals when the target region isn't streamed in
	// (nondeterministic headlessly). Loading the region makes coverage deterministic.
	bool bRegionPinned = false;
	if (Params->HasField(TEXT("region_x")) && Params->HasField(TEXT("region_y")))
	{
		if (UWorldPartition* WP = World->GetWorldPartition())
		{
			const double RX = Params->GetNumberField(TEXT("region_x"));
			const double RY = Params->GetNumberField(TEXT("region_y"));
			const double RZ = Params->HasField(TEXT("region_z")) ? Params->GetNumberField(TEXT("region_z")) : 0.0;
			const double Ext = Params->HasField(TEXT("region_extent")) ? Params->GetNumberField(TEXT("region_extent")) : 25600.0;
			const FBox RegionBox(FVector(RX - Ext, RY - Ext, RZ - Ext), FVector(RX + Ext, RY + Ext, RZ + Ext));
			UWorldPartitionEditorLoaderAdapter* EditorAdapter =
				WP->CreateEditorLoaderAdapter<FLoaderAdapterShape>(World, RegionBox, TEXT("PCGRegenRegion"));
			if (EditorAdapter)
			{
				if (IWorldPartitionActorLoaderInterface::ILoaderAdapter* Adapter = EditorAdapter->GetLoaderAdapter())
				{
					Adapter->Load(); // synchronous pin of intersecting actors
				}
				FWorldPartitionHelpers::FakeEngineTick(World); // drain deferred loading before generate
				ActiveRegenAdapters.Add(Key, EditorAdapter);
				bRegionPinned = true;
			}
		}
	}

	const EPCGComponentGenerationTrigger Saved = Original->GenerationTrigger;
	const bool bWasRuntime = (Saved == EPCGComponentGenerationTrigger::GenerateAtRuntime);
	if (bWasRuntime)
	{
		// Hand the component out of the runtime scheduler, then flip to on-demand.
		Subsystem->RefreshRuntimeGenComponent(Original, EPCGChangeType::GenerationGrid);
		Original->Modify();
		Original->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
		Subsystem->RegisterOrUpdatePCGComponent(Original, /*bDoActorMapping=*/true);
		ActiveRegens.Add(Key, static_cast<uint8>(Saved));
	}

	// Kick generation (editor partitioned path for partitioned components). Region
	// load (if any) has already completed above, so the now-loaded cells produce locals.
	Original->GenerateLocalGetTaskId(/*bForce=*/true);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("component"), Original->GetName());
	Result->SetBoolField(TEXT("flipped_from_runtime"), bWasRuntime);
	Result->SetBoolField(TEXT("region_pinned"), bRegionPinned);
	Result->SetBoolField(TEXT("is_generating"), Original->IsGenerating());
	Result->SetStringField(TEXT("note"), bRegionPinned
		? TEXT("Region pinned + generation kicked (async). Poll pcg_regen_poll until is_generating=false, READ output, THEN pcg_regen_finalize (restores trigger, releases the pinned region, destroys temp output).")
		: TEXT("Generation kicked (async). NOTE: no region_x/region_y given (or non-WP level) — coverage depends on which WP cells happen to be loaded and may be 0 locals. Pass region_x/region_y(/region_extent) for deterministic coverage. Poll, read, then finalize."));
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleRegenPoll(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("actor_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: actor_name"));
	}
	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	const FString ComponentName = Params->HasField(TEXT("component_name")) ? Params->GetStringField(TEXT("component_name")) : FString();

	UWorld* World = GetEditorWorldForPCG();
	FString Error;
	AActor* Actor = FindActorByLabelOrName(World, ActorName, Error);
	if (!Actor) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGComponent* Original = FindPCGComponentOnActor(Actor, ComponentName, Error);
	if (!Original) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGSubsystem* Subsystem = UPCGSubsystem::GetInstance(World);

	// For a partitioned component the original's own task flag isn't enough — also
	// check the graph executor. Done when neither reports activity.
	UPCGGraph* Graph = Original->GetGraph();
	const bool bGraphExecuting = (Subsystem && Graph) ? Subsystem->IsGraphCurrentlyExecuting(Graph) : false;
	const bool bGenerating = Original->IsGenerating() || bGraphExecuting;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("component"), Original->GetName());
	Result->SetBoolField(TEXT("is_generating"), bGenerating);
	Result->SetBoolField(TEXT("graph_executing"), bGraphExecuting);
	// When done, report which regions were produced so the caller knows where to
	// sample (regen often covers only a sub-region around the loaded cells).
	if (!bGenerating)
	{
		Result->SetField(TEXT("coverage"), MakeShared<FJsonValueObject>(BuildCoverageJson(Subsystem, Original)));
	}
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleRegenFinalize(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("actor_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: actor_name"));
	}
	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	const FString ComponentName = Params->HasField(TEXT("component_name")) ? Params->GetStringField(TEXT("component_name")) : FString();
	const bool bCleanup = Params->HasField(TEXT("cleanup")) ? Params->GetBoolField(TEXT("cleanup")) : true;

	UWorld* World = GetEditorWorldForPCG();
	FString Error;
	AActor* Actor = FindActorByLabelOrName(World, ActorName, Error);
	if (!Actor) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGComponent* Original = FindPCGComponentOnActor(Actor, ComponentName, Error);
	if (!Original) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error); }
	UPCGSubsystem* Subsystem = UPCGSubsystem::GetInstance(World);
	if (!Subsystem) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("UPCGSubsystem unavailable.")); }

	const FString Key = Original->GetPathName();
	const bool bWasFlipped = ActiveRegens.Contains(Key);

	if (bWasFlipped)
	{
		const EPCGComponentGenerationTrigger Saved = static_cast<EPCGComponentGenerationTrigger>(ActiveRegens[Key]);
		if (bCleanup)
		{
			// Tear down the editor-serialized on-demand locals we created.
			Subsystem->CleanupLocalComponentsImmediate(Original, /*bRemoveComponents=*/true);
		}
		Original->Modify();
		Original->GenerationTrigger = Saved;
		Subsystem->RegisterOrUpdatePCGComponent(Original, /*bDoActorMapping=*/true);
		Subsystem->RefreshRuntimeGenComponent(Original, EPCGChangeType::GenerationGrid);
		ActiveRegens.Remove(Key);
	}

	// Release the pinned WP region (independent of the flip — a non-runtime component
	// may still have pinned a region).
	bool bRegionReleased = false;
	if (TWeakObjectPtr<UWorldPartitionEditorLoaderAdapter>* AdapterPtr = ActiveRegenAdapters.Find(Key))
	{
		if (UWorldPartitionEditorLoaderAdapter* Adapter = AdapterPtr->Get())
		{
			if (UWorldPartition* WP = World->GetWorldPartition())
			{
				WP->ReleaseEditorLoaderAdapter(Adapter);
				bRegionReleased = true;
			}
		}
		ActiveRegenAdapters.Remove(Key);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("component"), Original->GetName());
	Result->SetBoolField(TEXT("restored"), bWasFlipped);
	Result->SetBoolField(TEXT("region_released"), bRegionReleased);
	Result->SetBoolField(TEXT("cleaned_up_locals"), bWasFlipped && bCleanup);
	if (!bWasFlipped)
	{
		Result->SetStringField(TEXT("note"), TEXT("Component was not flipped by pcg_regen_start (it wasn't a runtime component); nothing to restore."));
	}
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPPCGCommands::HandleRegenRecover(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = GetEditorWorldForPCG();
	UPCGSubsystem* Subsystem = World ? UPCGSubsystem::GetInstance(World) : nullptr;
	if (!Subsystem) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("UPCGSubsystem unavailable.")); }

	TArray<TSharedPtr<FJsonValue>> Restored;
	for (const TPair<FString, uint8>& Pair : ActiveRegens)
	{
		UPCGComponent* Comp = FindObject<UPCGComponent>(nullptr, *Pair.Key);
		if (!Comp) { continue; }
		Subsystem->CleanupLocalComponentsImmediate(Comp, /*bRemoveComponents=*/true);
		Comp->Modify();
		Comp->GenerationTrigger = static_cast<EPCGComponentGenerationTrigger>(Pair.Value);
		Subsystem->RegisterOrUpdatePCGComponent(Comp, /*bDoActorMapping=*/true);
		Subsystem->RefreshRuntimeGenComponent(Comp, EPCGChangeType::GenerationGrid);
		Restored.Add(MakeShared<FJsonValueString>(Pair.Key));
	}
	ActiveRegens.Empty();

	// Release any WP regions left pinned by interrupted regens.
	int32 RegionsReleased = 0;
	if (UWorldPartition* WP = World ? World->GetWorldPartition() : nullptr)
	{
		for (const TPair<FString, TWeakObjectPtr<UWorldPartitionEditorLoaderAdapter>>& Pair : ActiveRegenAdapters)
		{
			if (UWorldPartitionEditorLoaderAdapter* Adapter = Pair.Value.Get())
			{
				WP->ReleaseEditorLoaderAdapter(Adapter);
				++RegionsReleased;
			}
		}
	}
	ActiveRegenAdapters.Empty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("restored_count"), Restored.Num());
	Result->SetNumberField(TEXT("regions_released"), RegionsReleased);
	Result->SetArrayField(TEXT("restored_components"), Restored);
	return Result;
}

#endif // WITH_EDITOR
