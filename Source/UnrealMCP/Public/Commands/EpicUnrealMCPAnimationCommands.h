#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Animation analysis MCP commands.
 * Provides tools to inspect UAnimMontage, UAnimSequence, and list animation
 * assets via the MCP bridge.
 */
class UNREALMCP_API FEpicUnrealMCPAnimationCommands
{
public:
	FEpicUnrealMCPAnimationCommands();
	~FEpicUnrealMCPAnimationCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleAnalyzeAnimMontage(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAnalyzeAnimSequence(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListAnimationAssets(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetSkeletalMeshInfo(const TSharedPtr<FJsonObject>& Params);
};
