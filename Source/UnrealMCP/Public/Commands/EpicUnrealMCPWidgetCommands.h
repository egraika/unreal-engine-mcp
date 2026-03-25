#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UWidgetBlueprint;

/**
 * Handler class for Widget Blueprint MCP commands.
 * Provides tools to inspect, create, and modify UWidgetBlueprint assets
 * and their widget trees via the MCP bridge.
 */
class UNREALMCP_API FEpicUnrealMCPWidgetCommands
{
public:
	FEpicUnrealMCPWidgetCommands();
	~FEpicUnrealMCPWidgetCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Read commands
	TSharedPtr<FJsonObject> HandleAnalyzeWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetWidgetDetails(const TSharedPtr<FJsonObject>& Params);

	// Write commands
	TSharedPtr<FJsonObject> HandleCreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddWidgetChild(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetWidgetProperties(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveWidget(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReplaceWidgetRoot(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCompileWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);

	// Helpers
	TSharedPtr<FJsonObject> SerializeWidget(class UWidget* Widget, int32 Depth);
	UWidgetBlueprint* LoadWidgetBlueprint(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutError);
	UClass* ResolveWidgetClass(const FString& ClassName);
};
