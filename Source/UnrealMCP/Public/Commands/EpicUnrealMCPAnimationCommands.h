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
	TSharedPtr<FJsonObject> HandleUpdateMontageSection(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddMontageNotify(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveMontageNotify(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleUpdateMontageNotify(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddMontageNotifyTrack(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRenameMontageNotifyTrack(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveMontageNotifyTrack(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddMontageSegment(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveMontageSegment(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleUpdateMontageSegment(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMontageBlendTimes(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMontageRootMotion(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMontageSyncGroup(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMontagePlayback(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMontageTimeStretch(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMontagePreviewBasePose(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddMontageSlot(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRenameMontageSlot(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveMontageSlot(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMontageNotifyTrackColor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddSectionMetadata(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveSectionMetadata(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReorderMontageSection(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReorderMontageSegment(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReorderMontageNotifyTrack(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReorderMontageSlot(const TSharedPtr<FJsonObject>& Params);

	// Curve editing & root motion
	TSharedPtr<FJsonObject> HandleGetAnimCurveKeys(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddAnimCurve(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetAnimCurveKeys(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveAnimCurve(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetRootMotionData(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleBatchAddSpeedCurves(const TSharedPtr<FJsonObject>& Params);

	// AnimSequence-level root motion flags (distinct from montage-level flags above).
	TSharedPtr<FJsonObject> HandleSetAnimSequenceRootMotion(const TSharedPtr<FJsonObject>& Params);

	// AnimSequence interpolation / additive / compression properties (non-root-motion metadata
	// that still materially affects extracted poses — e.g. a wrongly-additive sequence drifts).
	TSharedPtr<FJsonObject> HandleSetAnimSequenceProperties(const TSharedPtr<FJsonObject>& Params);

	// Mirror Data Table
	TSharedPtr<FJsonObject> HandleCreateMirrorDataTable(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAnalyzeMirrorDataTable(const TSharedPtr<FJsonObject>& Params);

	// Preview mesh tracking for cleanup
	TMap<FString, TArray<TWeakObjectPtr<AActor>>> PreviewActors;
};
