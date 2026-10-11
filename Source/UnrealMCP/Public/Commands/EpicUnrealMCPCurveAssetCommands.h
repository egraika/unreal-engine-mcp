#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for curve-asset MCP commands (UCurveFloat).
 * Counterpart to FEpicUnrealMCPCurveTableCommands — that handler operates on
 * UCurveTable rows; this one operates on standalone UCurveFloat assets.
 */
class UNREALMCP_API FEpicUnrealMCPCurveAssetCommands
{
public:
	FEpicUnrealMCPCurveAssetCommands();
	~FEpicUnrealMCPCurveAssetCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleListCurveFloats(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadCurveFloat(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateCurveFloat(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleUpdateCurveFloat(const TSharedPtr<FJsonObject>& Params);
};
