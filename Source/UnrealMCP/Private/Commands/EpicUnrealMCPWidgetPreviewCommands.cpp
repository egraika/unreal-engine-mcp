#include "Commands/EpicUnrealMCPWidgetPreviewCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/SWidget.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "JsonObjectConverter.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "PixelFormat.h"

FEpicUnrealMCPWidgetPreviewCommands::FEpicUnrealMCPWidgetPreviewCommands()
{
}

FEpicUnrealMCPWidgetPreviewCommands::~FEpicUnrealMCPWidgetPreviewCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetPreviewCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("capture_widget_preview"))
	{
		return HandleCaptureWidgetPreview(Params);
	}
	if (CommandType == TEXT("capture_widget_state"))
	{
		return HandleCaptureWidgetState(Params);
	}
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown WidgetPreview command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetPreviewCommands::HandleCaptureWidgetPreview(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetPath;
	if (!Params->TryGetStringField(TEXT("widget_path"), WidgetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_path' parameter"));
	}
	const int32 Resolution = Params->HasField(TEXT("resolution"))
		? static_cast<int32>(Params->GetNumberField(TEXT("resolution")))
		: 1024;

	return CaptureWidget(WidgetPath, Resolution, /*MockData=*/nullptr, TEXT("widget_preview"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetPreviewCommands::HandleCaptureWidgetState(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetPath;
	if (!Params->TryGetStringField(TEXT("widget_path"), WidgetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_path' parameter"));
	}
	const int32 Resolution = Params->HasField(TEXT("resolution"))
		? static_cast<int32>(Params->GetNumberField(TEXT("resolution")))
		: 1024;

	TSharedPtr<FJsonObject> MockData;
	const TSharedPtr<FJsonObject>* MockDataPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("mock_data"), MockDataPtr) && MockDataPtr && MockDataPtr->IsValid())
	{
		MockData = *MockDataPtr;
	}

	return CaptureWidget(WidgetPath, Resolution, MockData, TEXT("widget_state"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetPreviewCommands::CaptureWidget(
	const FString& WidgetPath,
	int32 RequestedResolution,
	const TSharedPtr<FJsonObject>& MockData,
	const FString& OutFilenameStem)
{
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(WidgetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *WidgetPath));
	}

	UClass* GeneratedClass = WidgetBP->GeneratedClass;
	if (!GeneratedClass || !GeneratedClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget Blueprint has no valid UUserWidget GeneratedClass: %s"), *WidgetPath));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world available"));
	}

	UUserWidget* UserWidget = CreateWidget<UUserWidget>(World, GeneratedClass);
	if (!UserWidget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("CreateWidget<UUserWidget> failed"));
	}

	// Apply mock_data via reflection. Walks JSON top-level keys and writes matching
	// FProperty fields directly on the runtime instance (not the CDO). Untyped or
	// unknown keys are silently skipped — this is a best-effort property bag.
	int32 MockFieldsApplied = 0;
	if (MockData.IsValid())
	{
		const int64 CheckFlags = 0;
		const int64 SkipFlags = 0;
		const bool bOk = FJsonObjectConverter::JsonObjectToUStruct(
			MockData.ToSharedRef(),
			GeneratedClass,
			UserWidget,
			CheckFlags,
			SkipFlags);
		// Even when bOk is false individual fields may have been written, so we
		// surface a coarse counter of top-level JSON keys for diagnostics.
		MockFieldsApplied = bOk ? MockData->Values.Num() : 0;
	}

	TSharedRef<SWidget> SlateWidget = UserWidget->TakeWidget();

	// Resolve render size. If the widget has a meaningful desired size, fit the
	// longest edge to RequestedResolution. Otherwise fall back to a square.
	SlateWidget->SlatePrepass(1.0f);
	FVector2D Desired = SlateWidget->GetDesiredSize();
	if (Desired.X < 1.0 || Desired.Y < 1.0)
	{
		Desired = FVector2D(static_cast<double>(RequestedResolution), static_cast<double>(RequestedResolution));
	}
	const double LongestEdge = FMath::Max(Desired.X, Desired.Y);
	const double Scale = (LongestEdge > 0.0) ? (static_cast<double>(RequestedResolution) / LongestEdge) : 1.0;
	const int32 Width = FMath::Max(1, FMath::RoundToInt(Desired.X * Scale));
	const int32 Height = FMath::Max(1, FMath::RoundToInt(Desired.Y * Scale));
	const FVector2D DrawSize(static_cast<double>(Width), static_cast<double>(Height));

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RenderTarget->ClearColor = FLinearColor::Transparent;
	RenderTarget->TargetGamma = 0.0f; // Use platform default
	RenderTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
	RenderTarget->UpdateResourceImmediate(true);

	const bool bUseGammaSpace = true;
	FWidgetRenderer* WidgetRenderer = new FWidgetRenderer(bUseGammaSpace);
	WidgetRenderer->SetIsPrepassNeeded(true);
	WidgetRenderer->DrawWidget(RenderTarget, SlateWidget, 1.0f, DrawSize, 0.0f);

	// DrawWidget is render-thread deferred; flush before reading pixels.
	FlushRenderingCommands();
	BeginCleanup(WidgetRenderer);

	TArray<FColor> Pixels;
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (RTResource)
	{
		FReadSurfaceDataFlags ReadFlags;
		ReadFlags.SetLinearToGamma(false);
		RTResource->ReadPixels(Pixels, ReadFlags);
	}

	if (Pixels.Num() == 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ReadPixels returned no data"));
	}

	TArray64<uint8> PNGBytes;
	FImageUtils::PNGCompressImageArray(Width, Height, Pixels, PNGBytes);

	const FString OutputDir = FPaths::ProjectSavedDir() / TEXT("MCP_Previews");
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	const FString FileName = FString::Printf(TEXT("%s_%s.png"),
		*OutFilenameStem,
		*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	const FString FullPath = OutputDir / FileName;

	FFileHelper::SaveArrayToFile(PNGBytes, *FullPath);

	const FString Base64Png = FBase64::Encode(PNGBytes.GetData(), PNGBytes.Num());

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_path"), WidgetPath);
	Result->SetStringField(TEXT("saved_path"), FullPath);
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	Result->SetNumberField(TEXT("png_bytes"), PNGBytes.Num());
	Result->SetStringField(TEXT("format"), TEXT("png"));
	Result->SetStringField(TEXT("image_base64"), Base64Png);
	if (MockData.IsValid())
	{
		Result->SetNumberField(TEXT("mock_fields_applied"), MockFieldsApplied);
	}
	return Result;
}
