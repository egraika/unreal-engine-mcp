// EpicUnrealMCPThumbnailCommands.h — Thumbnail generation and scanning
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNREALMCP_API FEpicUnrealMCPThumbnailCommands
{
public:
	FEpicUnrealMCPThumbnailCommands() = default;
	~FEpicUnrealMCPThumbnailCommands() = default;
	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);
private:
	TSharedPtr<FJsonObject> HandleGenerateThumbnail(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGenerateThumbnails(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleScanAssetsForThumbnails(const TSharedPtr<FJsonObject>& Params);
};
