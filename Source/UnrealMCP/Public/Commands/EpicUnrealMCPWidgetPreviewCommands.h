#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for offscreen Widget Blueprint preview captures.
 *
 * Uses FWidgetRenderer to rasterize a UUserWidget into a UTextureRenderTarget2D
 * without spawning into the world. Substantially cheaper than USceneCaptureComponent2D
 * for UMG content. PNGs are written to Saved/MCP_Previews/ AND returned base64-inline
 * in the JSON response so the model can consume the image without a second Read.
 *
 * Two modes:
 *   capture_widget_preview  — defaults only; pure layout/style/hierarchy snapshot.
 *   capture_widget_state    — applies a JSON mock_data property bag via reflection
 *                             to the constructed widget instance before render.
 */
class UNREALMCP_API FEpicUnrealMCPWidgetPreviewCommands
{
public:
	FEpicUnrealMCPWidgetPreviewCommands();
	~FEpicUnrealMCPWidgetPreviewCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleCaptureWidgetPreview(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCaptureWidgetState(const TSharedPtr<FJsonObject>& Params);

	// Shared rasterization helper. MockData may be null.
	TSharedPtr<FJsonObject> CaptureWidget(
		const FString& WidgetPath,
		int32 RequestedResolution,
		const TSharedPtr<FJsonObject>& MockData,
		const FString& OutFilenameStem);
};
