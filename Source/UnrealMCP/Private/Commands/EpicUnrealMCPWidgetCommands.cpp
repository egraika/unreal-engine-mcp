#include "Commands/EpicUnrealMCPWidgetCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPPropertyUtils.h"

#include "EditorAssetLibrary.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
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
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "Components/SizeBox.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/ScrollBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/ListView.h"
#include "Components/RichTextBlock.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

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
	else if (CommandType == TEXT("create_widget_blueprint"))
	{
		return HandleCreateWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("add_widget_child"))
	{
		return HandleAddWidgetChild(Params);
	}
	else if (CommandType == TEXT("set_widget_property"))
	{
		return HandleSetWidgetProperty(Params);
	}
	else if (CommandType == TEXT("remove_widget"))
	{
		return HandleRemoveWidget(Params);
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

// ============================================================================
// create_widget_blueprint — Create a new Widget Blueprint (UMG)
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleCreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("name"), WidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
	}

	FString PackagePath = TEXT("/Game/Blueprints/Widgets/");
	if (Params->HasField(TEXT("path")))
	{
		PackagePath = Params->GetStringField(TEXT("path"));
		if (!PackagePath.EndsWith(TEXT("/")))
		{
			PackagePath += TEXT("/");
		}
	}

	// Check if already exists
	FString FullPath = PackagePath + WidgetName;
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint already exists: %s"), *FullPath));
	}

	// Resolve parent class (default: UUserWidget)
	UClass* ParentClass = UUserWidget::StaticClass();
	FString ParentClassName;
	if (Params->TryGetStringField(TEXT("parent_class"), ParentClassName) && !ParentClassName.IsEmpty())
	{
		// Try to find the class by name
		UClass* FoundClass = FindObject<UClass>(ANY_PACKAGE, *ParentClassName);
		if (!FoundClass)
		{
			// Try with U prefix
			FoundClass = FindObject<UClass>(ANY_PACKAGE, *(TEXT("U") + ParentClassName));
		}
		if (!FoundClass)
		{
			// Try loading by path
			FoundClass = LoadClass<UUserWidget>(nullptr, *ParentClassName);
		}
		if (FoundClass && FoundClass->IsChildOf(UUserWidget::StaticClass()))
		{
			ParentClass = FoundClass;
		}
		else if (FoundClass)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent class '%s' is not a UUserWidget subclass"), *ParentClassName));
		}
		else
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent class not found: %s"), *ParentClassName));
		}
	}

	// Create widget blueprint via factory
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* CreatedAsset = AssetTools.CreateAsset(WidgetName, PackagePath, UWidgetBlueprint::StaticClass(), Factory);

	if (!CreatedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Widget Blueprint"));
	}

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(CreatedAsset);
	if (!WidgetBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Created asset is not a Widget Blueprint"));
	}

	// Save the asset
	UEditorAssetLibrary::SaveAsset(CreatedAsset->GetPathName());

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), WidgetName);
	Result->SetStringField(TEXT("path"), CreatedAsset->GetPathName());
	Result->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	return Result;
}

// ============================================================================
// add_widget_child — Add a child widget to a Widget Blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleAddWidgetChild(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString WidgetType;
	if (!Params->TryGetStringField(TEXT("widget_type"), WidgetType))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_type' parameter"));
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	// Load the widget blueprint
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}

	UWidgetTree* Tree = WidgetBP->WidgetTree;

	// Check if widget name already exists
	if (Tree->FindWidget(FName(*WidgetName)))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' already exists"), *WidgetName));
	}

	// Map widget type string to UClass
	static const TMap<FString, UClass*> WidgetClassMap = {
		{TEXT("TextBlock"), UTextBlock::StaticClass()},
		{TEXT("Image"), UImage::StaticClass()},
		{TEXT("Button"), UButton::StaticClass()},
		{TEXT("CanvasPanel"), UCanvasPanel::StaticClass()},
		{TEXT("VerticalBox"), UVerticalBox::StaticClass()},
		{TEXT("HorizontalBox"), UHorizontalBox::StaticClass()},
		{TEXT("Overlay"), UOverlay::StaticClass()},
		{TEXT("Border"), UBorder::StaticClass()},
		{TEXT("ProgressBar"), UProgressBar::StaticClass()},
		{TEXT("Spacer"), USpacer::StaticClass()},
		{TEXT("SizeBox"), USizeBox::StaticClass()},
		{TEXT("CheckBox"), UCheckBox::StaticClass()},
		{TEXT("Slider"), USlider::StaticClass()},
		{TEXT("ScrollBox"), UScrollBox::StaticClass()},
		{TEXT("WidgetSwitcher"), UWidgetSwitcher::StaticClass()},
		{TEXT("RichTextBlock"), URichTextBlock::StaticClass()},
	};

	UClass* const* FoundClass = WidgetClassMap.Find(WidgetType);
	if (!FoundClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown widget type: %s. Supported: TextBlock, Image, Button, CanvasPanel, VerticalBox, HorizontalBox, Overlay, Border, ProgressBar, Spacer, SizeBox, CheckBox, Slider, ScrollBox, WidgetSwitcher, RichTextBlock"), *WidgetType));
	}

	// Create the widget via WidgetTree (correct way — sets Outer and RF_Transactional)
	UWidget* NewWidget = Tree->ConstructWidget<UWidget>(*FoundClass, FName(*WidgetName));
	if (!NewWidget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to construct widget"));
	}

	// Find parent widget (optional — defaults to root)
	FString ParentName;
	UPanelWidget* ParentPanel = nullptr;

	if (Params->TryGetStringField(TEXT("parent_name"), ParentName) && !ParentName.IsEmpty())
	{
		UWidget* FoundParent = Tree->FindWidget(FName(*ParentName));
		ParentPanel = Cast<UPanelWidget>(FoundParent);
		if (!ParentPanel)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent '%s' not found or is not a panel widget"), *ParentName));
		}
	}
	else
	{
		// Add to root panel
		ParentPanel = Cast<UPanelWidget>(Tree->RootWidget);
		if (!ParentPanel)
		{
			// No root panel — set this widget as root if it's a panel, otherwise create a canvas
			if (UPanelWidget* AsPanel = Cast<UPanelWidget>(NewWidget))
			{
				Tree->RootWidget = AsPanel;
			}
			else
			{
				UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
				Tree->RootWidget = Canvas;
				ParentPanel = Canvas;
			}
		}
	}

	// Add as child
	UPanelSlot* Slot = nullptr;
	if (ParentPanel)
	{
		Slot = ParentPanel->AddChild(NewWidget);
	}

	// Apply slot properties if provided and it's a canvas slot
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		if (Params->HasField(TEXT("position")))
		{
			FVector2D Pos = FVector2D(
				Params->GetObjectField(TEXT("position"))->GetNumberField(TEXT("x")),
				Params->GetObjectField(TEXT("position"))->GetNumberField(TEXT("y"))
			);
			CanvasSlot->SetPosition(Pos);
		}
		if (Params->HasField(TEXT("size")))
		{
			FVector2D Size = FVector2D(
				Params->GetObjectField(TEXT("size"))->GetNumberField(TEXT("x")),
				Params->GetObjectField(TEXT("size"))->GetNumberField(TEXT("y"))
			);
			CanvasSlot->SetSize(Size);
		}
		if (Params->HasField(TEXT("anchors")))
		{
			const TSharedPtr<FJsonObject>& AnchorsObj = Params->GetObjectField(TEXT("anchors"));
			FAnchors Anchors(
				AnchorsObj->GetNumberField(TEXT("min_x")),
				AnchorsObj->GetNumberField(TEXT("min_y")),
				AnchorsObj->HasField(TEXT("max_x")) ? AnchorsObj->GetNumberField(TEXT("max_x")) : AnchorsObj->GetNumberField(TEXT("min_x")),
				AnchorsObj->HasField(TEXT("max_y")) ? AnchorsObj->GetNumberField(TEXT("max_y")) : AnchorsObj->GetNumberField(TEXT("min_y"))
			);
			CanvasSlot->SetAnchors(Anchors);
		}
		if (Params->HasField(TEXT("alignment")))
		{
			FVector2D Align = FVector2D(
				Params->GetObjectField(TEXT("alignment"))->GetNumberField(TEXT("x")),
				Params->GetObjectField(TEXT("alignment"))->GetNumberField(TEXT("y"))
			);
			CanvasSlot->SetAlignment(Align);
		}
		if (Params->HasField(TEXT("auto_size")))
		{
			CanvasSlot->SetAutoSize(Params->GetBoolField(TEXT("auto_size")));
		}
		if (Params->HasField(TEXT("z_order")))
		{
			CanvasSlot->SetZOrder(static_cast<int32>(Params->GetNumberField(TEXT("z_order"))));
		}
	}

	// Set initial text for TextBlock
	if (UTextBlock* TextWidget = Cast<UTextBlock>(NewWidget))
	{
		FString Text;
		if (Params->TryGetStringField(TEXT("text"), Text))
		{
			TextWidget->SetText(FText::FromString(Text));
		}
	}

	// Set initial visibility
	FString VisibilityStr;
	if (Params->TryGetStringField(TEXT("visibility"), VisibilityStr))
	{
		if (VisibilityStr == TEXT("Collapsed")) NewWidget->SetVisibility(ESlateVisibility::Collapsed);
		else if (VisibilityStr == TEXT("Hidden")) NewWidget->SetVisibility(ESlateVisibility::Hidden);
		else if (VisibilityStr == TEXT("HitTestInvisible")) NewWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		else if (VisibilityStr == TEXT("SelfHitTestInvisible")) NewWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		else NewWidget->SetVisibility(ESlateVisibility::Visible);
	}

	// Compile
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("widget_type"), WidgetType);
	Result->SetStringField(TEXT("parent"), ParentPanel ? ParentPanel->GetName() : TEXT("(root)"));
	Result->SetStringField(TEXT("slot_type"), Slot ? Slot->GetClass()->GetName() : TEXT("none"));
	return Result;
}

// ============================================================================
// set_widget_property — Modify properties on an existing widget
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleSetWidgetProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}

	UWidget* Widget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));
	}

	TArray<FString> SetFields;

	// Visibility
	FString VisibilityStr;
	if (Params->TryGetStringField(TEXT("visibility"), VisibilityStr))
	{
		if (VisibilityStr == TEXT("Visible")) Widget->SetVisibility(ESlateVisibility::Visible);
		else if (VisibilityStr == TEXT("Collapsed")) Widget->SetVisibility(ESlateVisibility::Collapsed);
		else if (VisibilityStr == TEXT("Hidden")) Widget->SetVisibility(ESlateVisibility::Hidden);
		else if (VisibilityStr == TEXT("HitTestInvisible")) Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
		else if (VisibilityStr == TEXT("SelfHitTestInvisible")) Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		SetFields.Add(TEXT("visibility"));
	}

	// Render opacity
	if (Params->HasField(TEXT("render_opacity")))
	{
		Widget->SetRenderOpacity(static_cast<float>(Params->GetNumberField(TEXT("render_opacity"))));
		SetFields.Add(TEXT("render_opacity"));
	}

	// Is enabled
	if (Params->HasField(TEXT("is_enabled")))
	{
		Widget->SetIsEnabled(Params->GetBoolField(TEXT("is_enabled")));
		SetFields.Add(TEXT("is_enabled"));
	}

	// TextBlock-specific
	if (UTextBlock* TextWidget = Cast<UTextBlock>(Widget))
	{
		FString Text;
		if (Params->TryGetStringField(TEXT("text"), Text))
		{
			TextWidget->SetText(FText::FromString(Text));
			SetFields.Add(TEXT("text"));
		}

		if (Params->HasField(TEXT("color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			TextWidget->SetColorAndOpacity(FSlateColor(Color));
			SetFields.Add(TEXT("color"));
		}

		if (Params->HasField(TEXT("auto_wrap")))
		{
			TextWidget->SetAutoWrapText(Params->GetBoolField(TEXT("auto_wrap")));
			SetFields.Add(TEXT("auto_wrap"));
		}
	}

	// Image-specific
	if (UImage* ImageWidget = Cast<UImage>(Widget))
	{
		if (Params->HasField(TEXT("color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			ImageWidget->SetColorAndOpacity(Color);
			SetFields.Add(TEXT("color"));
		}

		FString TexturePath;
		if (Params->TryGetStringField(TEXT("texture"), TexturePath))
		{
			UTexture2D* Texture = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(TexturePath));
			if (Texture)
			{
				ImageWidget->SetBrushFromTexture(Texture);
				SetFields.Add(TEXT("texture"));
			}
		}
	}

	// ProgressBar-specific
	if (UProgressBar* BarWidget = Cast<UProgressBar>(Widget))
	{
		if (Params->HasField(TEXT("percent")))
		{
			BarWidget->SetPercent(static_cast<float>(Params->GetNumberField(TEXT("percent"))));
			SetFields.Add(TEXT("percent"));
		}
	}

	// Compile
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetNumberField(TEXT("properties_set"), SetFields.Num());

	TArray<TSharedPtr<FJsonValue>> SetArray;
	for (const FString& F : SetFields)
	{
		SetArray.Add(MakeShareable(new FJsonValueString(F)));
	}
	Result->SetArrayField(TEXT("fields_set"), SetArray);
	return Result;
}

// ============================================================================
// remove_widget — Remove a widget from a Widget Blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleRemoveWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}

	UWidget* Widget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));
	}

	bool bRemoved = WidgetBP->WidgetTree->RemoveWidget(Widget);

	if (bRemoved)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
		FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bRemoved);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetBoolField(TEXT("removed"), bRemoved);
	if (!bRemoved)
	{
		Result->SetStringField(TEXT("message"), TEXT("Widget found but could not be removed"));
	}
	return Result;
}
