// EpicUnrealMCPEditorCommands.h — Actor spawning, level queries, transforms
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNREALMCP_API FEpicUnrealMCPEditorCommands
{
public:
	FEpicUnrealMCPEditorCommands() = default;
	~FEpicUnrealMCPEditorCommands() = default;
	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);
private:
	TSharedPtr<FJsonObject> HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSpawnActor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteActor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetRVTVolumes(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetLandscapeInfo(const TSharedPtr<FJsonObject>& Params);
};
