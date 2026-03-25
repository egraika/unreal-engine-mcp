// EpicUnrealMCPThumbnailCommands.cpp — Thumbnail generation stub implementations
#include "Commands/EpicUnrealMCPThumbnailCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

TSharedPtr<FJsonObject> FEpicUnrealMCPThumbnailCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("generate_thumbnail"))           return HandleGenerateThumbnail(Params);
	else if (CommandType == TEXT("generate_thumbnails"))     return HandleGenerateThumbnails(Params);
	else if (CommandType == TEXT("scan_assets_for_thumbnails")) return HandleScanAssetsForThumbnails(Params);
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown Thumbnail command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPThumbnailCommands::HandleGenerateThumbnail(const TSharedPtr<FJsonObject>& Params) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("generate_thumbnail: Pending restoration")); }
TSharedPtr<FJsonObject> FEpicUnrealMCPThumbnailCommands::HandleGenerateThumbnails(const TSharedPtr<FJsonObject>& Params) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("generate_thumbnails: Pending restoration")); }
TSharedPtr<FJsonObject> FEpicUnrealMCPThumbnailCommands::HandleScanAssetsForThumbnails(const TSharedPtr<FJsonObject>& Params) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("scan_assets_for_thumbnails: Pending restoration")); }
