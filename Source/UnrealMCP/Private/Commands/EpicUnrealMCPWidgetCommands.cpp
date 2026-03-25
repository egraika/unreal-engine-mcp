#include "Commands/EpicUnrealMCPWidgetCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPPropertyUtils.h"

#include "EditorAssetLibrary.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/OverlaySlot.h"

FEpicUnrealMCPWidgetCommands::FEpicUnrealMCPWidgetCommands()
{
}

FEpicUnrealMCPWidgetCommands::~FEpicUnrealMCPWidgetCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("analyze_widget_blueprint"))
	{
		return HandleAnalyzeWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("get_widget_details"))
	{
		return HandleGetWidgetDetails(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown Widget command: %s"), *CommandType));
}

// ============================================================================
// Helper: Count all widgets in a tree recursively
// ============================================================================

static int32 CountWidgets(UWidget* Widget)
{
	if (!Widget)
	{
		return 0;
	}

	int32 Count = 1;

	UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
	if (PanelWidget)
	{
		for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
		{
			Count += CountWidgets(PanelWidget->GetChildAt(i));
		}
	}

	return Count;
}

// ============================================================================
// Helper: Convert ESlateVisibility to string
// ============================================================================

static FString VisibilityToString(ESlateVisibility Visibility)
{
	switch (Visibility)
	{
	case ESlateVisibility::Visible:			return TEXT("Visible");
	case ESlateVisibility::Collapsed:		return TEXT("Collapsed");
	case ESlateVisibility::Hidden:			return TEXT("Hidden");
	case ESlateVisibility::HitTestInvisible:	return TEXT("HitTestInvisible");
	case ESlateVisibility::SelfHitTestInvisible:	return TEXT("SelfHitTestInvisible");
	default:					return TEXT("Unknown");
	}
}

// ============================================================================
// Helper: Convert EHorizontalAlignment to string
// ============================================================================

static FString HAlignToString(EHorizontalAlignment Align)
{
	switch (Align)
	{
	case HAlign_Fill:	return TEXT("Fill");
	case HAlign_Left:	return TEXT("Left");
	case HAlign_Center:	return TEXT("Center");
	case HAlign_Right:	return TEXT("Right");
	default:		return TEXT("Unknown");
	}
}

// ============================================================================
// Helper: Convert EVerticalAlignment to string
// ============================================================================

static FString VAlignToString(EVerticalAlignment Align)
{
	switch (Align)
	{
	case VAlign_Fill:	return TEXT("Fill");
	case VAlign_Top:	return TEXT("Top");
	case VAlign_Center:	return TEXT("Center");
	case VAlign_Bottom:	return TEXT("Bottom");
	default:		return TEXT("Unknown");
	}
}

// ============================================================================
// Helper: Convert ESizeRule to string
// ============================================================================

static FString SizeRuleToString(ESlateSizeRule::Type Rule)
{
	switch (Rule)
	{
	case ESlateSizeRule::Automatic:	return TEXT("Automatic");
	case ESlateSizeRule::Fill:	return TEXT("Fill");
	default:			return TEXT("Unknown");
	}
}

// ============================================================================
// Helper: Serialize FMargin (padding) to JSON
// ============================================================================

static TSharedPtr<FJsonObject> MarginToJson(const FMargin& Margin)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetNumberField(TEXT("left"), Margin.Left);
	Obj->SetNumberField(TEXT("top"), Margin.Top);
	Obj->SetNumberField(TEXT("right"), Margin.Right);
	Obj->SetNumberField(TEXT("bottom"), Margin.Bottom);
	return Obj;
}

// ============================================================================
// analyze_widget_blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleAnalyzeWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBlueprint)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UWidgetBlueprint: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), WidgetBlueprint->GetName());
	Result->SetStringField(TEXT("generated_class"), WidgetBlueprint->GeneratedClass ? WidgetBlueprint->GeneratedClass->GetName() : TEXT("None"));

	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;
	if (WidgetTree && WidgetTree->RootWidget)
	{
		TSharedPtr<FJsonObject> TreeJson = SerializeWidget(WidgetTree->RootWidget, 0);
		Result->SetObjectField(TEXT("widget_tree"), TreeJson);
		Result->SetNumberField(TEXT("total_widget_count"), CountWidgets(WidgetTree->RootWidget));
	}
	else
	{
		Result->SetField(TEXT("widget_tree"), MakeShareable(new FJsonValueNull()));
		Result->SetNumberField(TEXT("total_widget_count"), 0);
	}

	return Result;
}

// ============================================================================
// get_widget_details
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleGetWidgetDetails(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("widget_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString WidgetName = Params->GetStringField(TEXT("widget_name"));

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBlueprint)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UWidgetBlueprint: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;
	if (!WidgetTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget blueprint has no WidgetTree"));
	}

	UWidget* FoundWidget = WidgetTree->FindWidget(FName(*WidgetName));
	if (!FoundWidget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("widget_name"), FoundWidget->GetName());
	Result->SetStringField(TEXT("widget_class"), FoundWidget->GetClass()->GetName());

	// Full property dump via reflection
	TSharedPtr<FJsonObject> Properties = FEpicUnrealMCPPropertyUtils::SerializeObjectProperties(FoundWidget);
	if (Properties.IsValid())
	{
		Result->SetObjectField(TEXT("properties"), Properties);
	}

	// Slot info
	if (FoundWidget->Slot)
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("slot_type"), FoundWidget->Slot->GetClass()->GetName());

		// Serialize slot properties via reflection as well
		TSharedPtr<FJsonObject> SlotProperties = FEpicUnrealMCPPropertyUtils::SerializeObjectProperties(FoundWidget->Slot);
		if (SlotProperties.IsValid())
		{
			SlotObj->SetObjectField(TEXT("slot_properties"), SlotProperties);
		}

		Result->SetObjectField(TEXT("slot"), SlotObj);
	}

	return Result;
}

// ============================================================================
// SerializeWidget (recursive)
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::SerializeWidget(UWidget* Widget, int32 Depth)
{
	if (!Widget || Depth > 20)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> WidgetObj = MakeShareable(new FJsonObject);
	WidgetObj->SetStringField(TEXT("name"), Widget->GetName());
	WidgetObj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
	WidgetObj->SetStringField(TEXT("visibility"), VisibilityToString(Widget->GetVisibility()));
	WidgetObj->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);

	// Tool tip (only if non-empty)
	const FString ToolTipStr = Widget->ToolTipText.ToString();
	if (!ToolTipStr.IsEmpty())
	{
		WidgetObj->SetStringField(TEXT("tool_tip"), ToolTipStr);
	}

	// Slot info
	if (Widget->Slot)
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("slot_type"), Widget->Slot->GetClass()->GetName());

		// Canvas panel slot
		UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
		if (CanvasSlot)
		{
			FAnchors Anchors = CanvasSlot->GetAnchors();
			TSharedPtr<FJsonObject> AnchorObj = MakeShareable(new FJsonObject);
			TSharedPtr<FJsonObject> AnchorMin = MakeShareable(new FJsonObject);
			AnchorMin->SetNumberField(TEXT("x"), Anchors.Minimum.X);
			AnchorMin->SetNumberField(TEXT("y"), Anchors.Minimum.Y);
			AnchorObj->SetObjectField(TEXT("minimum"), AnchorMin);
			TSharedPtr<FJsonObject> AnchorMax = MakeShareable(new FJsonObject);
			AnchorMax->SetNumberField(TEXT("x"), Anchors.Maximum.X);
			AnchorMax->SetNumberField(TEXT("y"), Anchors.Maximum.Y);
			AnchorObj->SetObjectField(TEXT("maximum"), AnchorMax);
			SlotObj->SetObjectField(TEXT("anchors"), AnchorObj);

			FMargin Offsets = CanvasSlot->GetOffsets();
			SlotObj->SetObjectField(TEXT("offsets"), MarginToJson(Offsets));

			FVector2D Alignment = CanvasSlot->GetAlignment();
			TSharedPtr<FJsonObject> AlignObj = MakeShareable(new FJsonObject);
			AlignObj->SetNumberField(TEXT("x"), Alignment.X);
			AlignObj->SetNumberField(TEXT("y"), Alignment.Y);
			SlotObj->SetObjectField(TEXT("alignment"), AlignObj);

			SlotObj->SetNumberField(TEXT("z_order"), CanvasSlot->GetZOrder());
		}

		// Horizontal box slot
		UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Widget->Slot);
		if (HBoxSlot)
		{
			SlotObj->SetStringField(TEXT("size_rule"), SizeRuleToString(HBoxSlot->GetSize().SizeRule));
			SlotObj->SetNumberField(TEXT("size_value"), HBoxSlot->GetSize().Value);
			SlotObj->SetStringField(TEXT("h_align"), HAlignToString(HBoxSlot->GetHorizontalAlignment()));
			SlotObj->SetStringField(TEXT("v_align"), VAlignToString(HBoxSlot->GetVerticalAlignment()));
			SlotObj->SetObjectField(TEXT("padding"), MarginToJson(HBoxSlot->GetPadding()));
		}

		// Vertical box slot
		UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Widget->Slot);
		if (VBoxSlot)
		{
			SlotObj->SetStringField(TEXT("size_rule"), SizeRuleToString(VBoxSlot->GetSize().SizeRule));
			SlotObj->SetNumberField(TEXT("size_value"), VBoxSlot->GetSize().Value);
			SlotObj->SetStringField(TEXT("h_align"), HAlignToString(VBoxSlot->GetHorizontalAlignment()));
			SlotObj->SetStringField(TEXT("v_align"), VAlignToString(VBoxSlot->GetVerticalAlignment()));
			SlotObj->SetObjectField(TEXT("padding"), MarginToJson(VBoxSlot->GetPadding()));
		}

		// Overlay slot
		UOverlaySlot* OvlSlot = Cast<UOverlaySlot>(Widget->Slot);
		if (OvlSlot)
		{
			SlotObj->SetStringField(TEXT("h_align"), HAlignToString(OvlSlot->GetHorizontalAlignment()));
			SlotObj->SetStringField(TEXT("v_align"), VAlignToString(OvlSlot->GetVerticalAlignment()));
			SlotObj->SetObjectField(TEXT("padding"), MarginToJson(OvlSlot->GetPadding()));
		}

		WidgetObj->SetObjectField(TEXT("slot"), SlotObj);
	}

	// Type-specific info
	UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
	if (TextBlock)
	{
		WidgetObj->SetStringField(TEXT("text"), TextBlock->GetText().ToString());
	}

	UImage* ImageWidget = Cast<UImage>(Widget);
	if (ImageWidget)
	{
		TSharedPtr<FJsonObject> BrushObj = MakeShareable(new FJsonObject);
		const FSlateBrush& Brush = ImageWidget->GetBrush();
		BrushObj->SetStringField(TEXT("resource_name"), Brush.GetResourceName().ToString());

		TSharedPtr<FJsonObject> SizeObj = MakeShareable(new FJsonObject);
		SizeObj->SetNumberField(TEXT("x"), Brush.ImageSize.X);
		SizeObj->SetNumberField(TEXT("y"), Brush.ImageSize.Y);
		BrushObj->SetObjectField(TEXT("image_size"), SizeObj);

		WidgetObj->SetObjectField(TEXT("brush"), BrushObj);
	}

	UButton* ButtonWidget = Cast<UButton>(Widget);
	if (ButtonWidget)
	{
		WidgetObj->SetStringField(TEXT("widget_type_info"), TEXT("Button"));
	}

	UProgressBar* ProgressBarWidget = Cast<UProgressBar>(Widget);
	if (ProgressBarWidget)
	{
		WidgetObj->SetNumberField(TEXT("percent"), ProgressBarWidget->GetPercent());
	}

	// Children (if panel widget)
	UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
	if (PanelWidget && PanelWidget->GetChildrenCount() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ChildrenArray;
		for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
		{
			UWidget* Child = PanelWidget->GetChildAt(i);
			if (Child)
			{
				TSharedPtr<FJsonObject> ChildObj = SerializeWidget(Child, Depth + 1);
				if (ChildObj.IsValid())
				{
					ChildrenArray.Add(MakeShareable(new FJsonValueObject(ChildObj)));
				}
			}
		}
		WidgetObj->SetArrayField(TEXT("children"), ChildrenArray);
	}

	return WidgetObj;
}
