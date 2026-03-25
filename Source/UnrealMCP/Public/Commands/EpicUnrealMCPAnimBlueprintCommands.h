#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UAnimBlueprint;
class UAnimGraphNode_StateMachine;
class UAnimStateNode;
class UAnimationStateMachineGraph;
class UEdGraph;

/**
 * Handler class for Animation Blueprint MCP commands.
 * Provides tools to create, inspect, and edit UAnimBlueprint assets
 * including state machines, states, transitions, and anim graph nodes.
 */
class UNREALMCP_API FEpicUnrealMCPAnimBlueprintCommands
{
public:
	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Phase 1: Lifecycle
	TSharedPtr<FJsonObject> HandleCreateAnimBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadAnimBlueprint(const TSharedPtr<FJsonObject>& Params);

	// Phase 2: State Machines
	TSharedPtr<FJsonObject> HandleAddStateMachine(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddState(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddTransition(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetDefaultState(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetStateAnimation(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetTransitionRule(const TSharedPtr<FJsonObject>& Params);

	// Phase 3: Advanced
	TSharedPtr<FJsonObject> HandleAddBlendSpacePlayer(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleConnectAnimNodes(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetStateMachineInfo(const TSharedPtr<FJsonObject>& Params);

	// Phase 4: AnimGraph Nodes (CopyPoseFromMesh, AnimDynamics, etc.)
	TSharedPtr<FJsonObject> HandleAddAnimNode(const TSharedPtr<FJsonObject>& Params);

	// Helpers
	UAnimBlueprint* LoadAnimBlueprint(const FString& NameOrPath, FString& OutError);
	UEdGraph* FindAnimGraph(UAnimBlueprint* ABP);
	UAnimGraphNode_StateMachine* FindStateMachineByName(UAnimBlueprint* ABP, const FString& Name);
	UAnimStateNode* FindStateByName(UAnimationStateMachineGraph* SMGraph, const FString& Name);
};
