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
	TSharedPtr<FJsonObject> HandleAddSocketToSkeleton(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleModifySocket(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveSocket(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandlePreviewMeshOnSocket(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleClearSocketPreview(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCaptureSocketPreview(const TSharedPtr<FJsonObject>& Params);

	// Montage creation & mutation
	TSharedPtr<FJsonObject> HandleCreateAnimMontage(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddMontageSection(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveMontageSection(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMontageSectionLink(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddMontageNotify(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveMontageNotify(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddMontageSegment(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMontageBlendTimes(const TSharedPtr<FJsonObject>& Params);

	// Curve editing & root motion
	TSharedPtr<FJsonObject> HandleGetAnimCurveKeys(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddAnimCurve(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetAnimCurveKeys(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveAnimCurve(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetRootMotionData(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleBatchAddSpeedCurves(const TSharedPtr<FJsonObject>& Params);

	// Mirror Data Table
	TSharedPtr<FJsonObject> HandleCreateMirrorDataTable(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAnalyzeMirrorDataTable(const TSharedPtr<FJsonObject>& Params);

	// Preview mesh tracking for cleanup
	TMap<FString, TArray<TWeakObjectPtr<AActor>>> PreviewActors;
};
