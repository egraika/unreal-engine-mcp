// EpicUnrealMCPRVTCommands.h — Runtime Virtual Texture management
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNREALMCP_API FEpicUnrealMCPRVTCommands
{
public:
	FEpicUnrealMCPRVTCommands() = default;
	~FEpicUnrealMCPRVTCommands() = default;
	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);
private:
	TSharedPtr<FJsonObject> HandleGetRVTInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetRVTProperties(const TSharedPtr<FJsonObject>& Params);
};
