#include "Commands/BlueprintGraph/NodeDeleter.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"

TSharedPtr<FJsonObject> FNodeDeleter::DeleteNode(const TSharedPtr<FJsonObject>& Params)
{
	// Validate parameters
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid parameters"));
	}

	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString NodeID;
	if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
	{
		return CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
	}

	// Get optional function name
	FString FunctionName;
	Params->TryGetStringField(TEXT("function_name"), FunctionName);

	// Load the Blueprint
	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	// Get the appropriate graph
	UEdGraph* Graph = GetGraph(Blueprint, FunctionName);
	if (!Graph)
	{
		if (FunctionName.IsEmpty())
		{
			return CreateErrorResponse(TEXT("Blueprint has no event graph"));
		}
		else
		{
			return CreateErrorResponse(FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));
		}
	}

	// Find the node
	UEdGraphNode* Node = FindNodeByID(Graph, NodeID);
	if (!Node)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeID));
	}

	// Store node ID before deletion
	FString DeletedID = Node->NodeGuid.ToString();

	// Remove the node
	if (!RemoveNode(Graph, Node))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Failed to remove node: %s"), *NodeID));
	}

	// Notify changes
	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogTemp, Display, TEXT("Successfully deleted node '%s' from %s"), *DeletedID, *BlueprintName);

	return CreateSuccessResponse(DeletedID);
}

UEdGraph* FNodeDeleter::GetGraph(UBlueprint* Blueprint, const FString& FunctionName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// If no function name, return EventGraph
	if (FunctionName.IsEmpty())
	{
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			return Blueprint->UbergraphPages[0];
		}
		return nullptr;
	}

	// Search in function graphs
	for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
	{
		if (FuncGraph && FuncGraph->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
		{
			return FuncGraph;
		}
	}

	return nullptr;
}

UEdGraphNode* FNodeDeleter::FindNodeByID(UEdGraph* Graph, const FString& NodeID)
{
	if (!Graph)
	{
		return nullptr;
	}

	// Distinguish int UniqueID (the form returned by read_anim_blueprint /
	// analyze_blueprint_graph) from a NodeGuid hex string. A guid that happens
	// to start with digits would otherwise pass Atoi and falsely match a
	// UniqueID, so require the whole string to be numeric.
	bool bIdIsNumeric = !NodeID.IsEmpty();
	for (const TCHAR Ch : NodeID)
	{
		if (!FChar::IsDigit(Ch)) { bIdIsNumeric = false; break; }
	}
	const int32 NodeUniqueId = bIdIsNumeric ? FCString::Atoi(*NodeID) : 0;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		// Try matching by int UniqueID first — this is what read_anim_blueprint
		// and analyze_blueprint_graph emit, so it's the most common form.
		if (bIdIsNumeric && (int32)Node->GetUniqueID() == NodeUniqueId)
		{
			return Node;
		}

		// Try matching by NodeGuid
		if (Node->NodeGuid.ToString().Equals(NodeID, ESearchCase::IgnoreCase))
		{
			return Node;
		}

		// Try matching by GetName()
		if (Node->GetName().Equals(NodeID, ESearchCase::IgnoreCase))
		{
			return Node;
		}
	}

	return nullptr;
}

bool FNodeDeleter::RemoveNode(UEdGraph* Graph, UEdGraphNode* Node)
{
	if (!Graph || !Node)
	{
		return false;
	}

	// Break all connections first
	Node->BreakAllNodeLinks();

	// Remove from graph
	Graph->RemoveNode(Node);

	return true;
}

UBlueprint* FNodeDeleter::LoadBlueprint(const FString& BlueprintName)
{
	// Helper: load a (possibly bare) path through the editor asset library and
	// cast to UBlueprint. Accepts both "/Game/Foo/Bar" and "/Game/Foo/Bar.Bar".
	auto TryLoadAt = [](const FString& InPath) -> UBlueprint*
	{
		FString Path = InPath;
		if (!Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPaths::GetBaseFilename(Path);
		}
		if (UBlueprint* Direct = LoadObject<UBlueprint>(nullptr, *Path))
		{
			return Direct;
		}
		if (UEditorAssetLibrary::DoesAssetExist(Path))
		{
			return Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
		}
		return nullptr;
	};

	// Full path passed in directly.
	if (BlueprintName.StartsWith(TEXT("/")))
	{
		return TryLoadAt(BlueprintName);
	}

	// Bare name — probe a few common roots before giving up. AnimBPs live
	// outside /Game/Blueprints/ in this project, so an unqualified name has
	// to fan out instead of hard-failing on the legacy path.
	const TArray<FString> Roots = {
		TEXT("/Game/Blueprints/"),
		TEXT("/Game/AnimBlueprints/"),
		TEXT("/Game/Characters/"),
		TEXT("/Game/")
	};
	for (const FString& Root : Roots)
	{
		if (UBlueprint* Found = TryLoadAt(Root + BlueprintName))
		{
			return Found;
		}
	}

	return nullptr;
}

TSharedPtr<FJsonObject> FNodeDeleter::CreateSuccessResponse(const FString& DeletedNodeID)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("deleted_node_id"), DeletedNodeID);
	return Response;
}

TSharedPtr<FJsonObject> FNodeDeleter::CreateErrorResponse(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	return Response;
}
