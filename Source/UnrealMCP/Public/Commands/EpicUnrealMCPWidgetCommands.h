#pragma once

#include "CoreMinimal.h"
#include "Json.h"

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
	TSharedPtr<FJsonObject> SerializeWidget(class UWidget* Widget, int32 Depth);
};
