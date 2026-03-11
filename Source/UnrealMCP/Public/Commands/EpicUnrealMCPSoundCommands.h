#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Sound asset MCP commands.
 * Provides tools to inspect USoundWave and USoundCue assets,
 * including node graph serialization for SoundCues.
 */
class UNREALMCP_API FEpicUnrealMCPSoundCommands
{
public:
	FEpicUnrealMCPSoundCommands();
	~FEpicUnrealMCPSoundCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleGetSoundInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> SerializeSoundCueNode(class USoundNode* Node, int32 Depth);
};
