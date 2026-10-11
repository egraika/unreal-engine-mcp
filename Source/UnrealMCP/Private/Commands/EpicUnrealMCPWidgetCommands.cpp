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
#include "Components/TileView.h"
#include "Components/TreeView.h"
#include "Components/RichTextBlock.h"
#include "Components/NamedSlot.h"
#include "Components/NamedSlotInterface.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Throbber.h"
#include "Components/CircularThrobber.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SpinBox.h"
#include "Components/ExpandableArea.h"
#include "Components/InvalidationBox.h"
#include "Components/InputKeySelector.h"
#include "Components/RetainerBox.h"
#include "Components/BackgroundBlur.h"
#include "Components/SafeZone.h"
#include "Components/StackBox.h"
#include "Components/StackBoxSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Components/BorderSlot.h"
#include "CommonActivatableWidget.h"
#include "CommonActivatableWidgetSwitcher.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "CommonAnimatedSwitcher.h"
#include "CommonBorder.h"
#include "CommonButtonBase.h"
#include "CommonHierarchicalScrollBox.h"
#include "CommonLazyImage.h"
#include "CommonRichTextBlock.h"
#include "CommonTextBlock.h"
#include "CommonUserWidget.h"
#include "CommonVisibilitySwitcher.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Animation/WidgetAnimation.h"
#include "UObject/UObjectHash.h"

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
	else if (CommandType == TEXT("save_widget_blueprint"))
	{
		return HandleSaveWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("delete_widget_blueprint"))
	{
		return HandleDeleteWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("rename_widget_blueprint"))
	{
		return HandleRenameWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("duplicate_widget_blueprint"))
	{
		return HandleDuplicateWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("list_widget_blueprints"))
	{
		return HandleListWidgetBlueprints(Params);
	}
	else if (CommandType == TEXT("move_widget"))
	{
		return HandleMoveWidget(Params);
	}
	else if (CommandType == TEXT("reorder_widget_children"))
	{
		return HandleReorderWidgetChildren(Params);
	}
	else if (CommandType == TEXT("set_widget_slot_property"))
	{
		return HandleSetWidgetSlotProperty(Params);
	}
	else if (CommandType == TEXT("set_widget_root"))
	{
		return HandleSetWidgetRoot(Params);
	}
	else if (CommandType == TEXT("compile_widget_blueprint"))
	{
		return HandleCompileWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("list_required_bindwidgets"))
	{
		return HandleListRequiredBindWidgets(Params);
	}
	else if (CommandType == TEXT("set_widget_class_property"))
	{
		return HandleSetWidgetClassProperty(Params);
	}
	else if (CommandType == TEXT("set_widget_object_property"))
	{
		return HandleSetWidgetObjectProperty(Params);
	}
	else if (CommandType == TEXT("create_common_style_asset"))
	{
		return HandleCreateCommonStyleAsset(Params);
	}
	else if (CommandType == TEXT("set_named_slot_content"))
	{
		return HandleSetNamedSlotContent(Params);
	}
	else if (CommandType == TEXT("wrap_widget"))
	{
		return HandleWrapWidget(Params);
	}
	else if (CommandType == TEXT("set_widget_is_variable"))
	{
		return HandleSetWidgetIsVariable(Params);
	}
	else if (CommandType == TEXT("replace_widget_class"))
	{
		return HandleReplaceWidgetClass(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown Widget command: %s"), *CommandType));
}

// ============================================================================
// Helpers: shared slot-property application
// ============================================================================

static EHorizontalAlignment ParseHAlign(const FString& S)
{
	if (S == TEXT("Fill")) return HAlign_Fill;
	if (S == TEXT("Left")) return HAlign_Left;
	if (S == TEXT("Center")) return HAlign_Center;
	if (S == TEXT("Right")) return HAlign_Right;
	return HAlign_Fill;
}

static EVerticalAlignment ParseVAlign(const FString& S)
{
	if (S == TEXT("Fill")) return VAlign_Fill;
	if (S == TEXT("Top")) return VAlign_Top;
	if (S == TEXT("Center")) return VAlign_Center;
	if (S == TEXT("Bottom")) return VAlign_Bottom;
	return VAlign_Fill;
}

static FMargin ParseMarginObj(const TSharedPtr<FJsonObject>& Obj)
{
	FMargin M;
	if (!Obj.IsValid()) return M;
	if (Obj->HasField(TEXT("left"))) M.Left = static_cast<float>(Obj->GetNumberField(TEXT("left")));
	if (Obj->HasField(TEXT("top"))) M.Top = static_cast<float>(Obj->GetNumberField(TEXT("top")));
	if (Obj->HasField(TEXT("right"))) M.Right = static_cast<float>(Obj->GetNumberField(TEXT("right")));
	if (Obj->HasField(TEXT("bottom"))) M.Bottom = static_cast<float>(Obj->GetNumberField(TEXT("bottom")));
	return M;
}

// Applies any slot-relevant fields from Params to the given slot. Returns the
// number of fields actually applied. Routes by slot type (Canvas, HBox, VBox,
// Overlay). Unknown slot types are no-ops.
static int32 ApplySlotProperties(UPanelSlot* Slot, const TSharedPtr<FJsonObject>& Params)
{
	if (!Slot || !Params.IsValid()) return 0;
	int32 Applied = 0;

	if (UCanvasPanelSlot* CSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		if (Params->HasField(TEXT("position")))
		{
			const TSharedPtr<FJsonObject>& O = Params->GetObjectField(TEXT("position"));
			CSlot->SetPosition(FVector2D(O->GetNumberField(TEXT("x")), O->GetNumberField(TEXT("y"))));
			++Applied;
		}
		if (Params->HasField(TEXT("size")))
		{
			const TSharedPtr<FJsonObject>& O = Params->GetObjectField(TEXT("size"));
			CSlot->SetSize(FVector2D(O->GetNumberField(TEXT("x")), O->GetNumberField(TEXT("y"))));
			++Applied;
		}
		if (Params->HasField(TEXT("anchors")))
		{
			const TSharedPtr<FJsonObject>& O = Params->GetObjectField(TEXT("anchors"));
			const float MinX = static_cast<float>(O->GetNumberField(TEXT("min_x")));
			const float MinY = static_cast<float>(O->GetNumberField(TEXT("min_y")));
			const float MaxX = O->HasField(TEXT("max_x")) ? static_cast<float>(O->GetNumberField(TEXT("max_x"))) : MinX;
			const float MaxY = O->HasField(TEXT("max_y")) ? static_cast<float>(O->GetNumberField(TEXT("max_y"))) : MinY;
			CSlot->SetAnchors(FAnchors(MinX, MinY, MaxX, MaxY));
			++Applied;
		}
		if (Params->HasField(TEXT("alignment")))
		{
			const TSharedPtr<FJsonObject>& O = Params->GetObjectField(TEXT("alignment"));
			CSlot->SetAlignment(FVector2D(O->GetNumberField(TEXT("x")), O->GetNumberField(TEXT("y"))));
			++Applied;
		}
		if (Params->HasField(TEXT("auto_size")))
		{
			CSlot->SetAutoSize(Params->GetBoolField(TEXT("auto_size")));
			++Applied;
		}
		if (Params->HasField(TEXT("z_order")))
		{
			CSlot->SetZOrder(static_cast<int32>(Params->GetNumberField(TEXT("z_order"))));
			++Applied;
		}
	}
	else if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Slot))
	{
		if (Params->HasField(TEXT("padding")))
		{
			HSlot->SetPadding(ParseMarginObj(Params->GetObjectField(TEXT("padding"))));
			++Applied;
		}
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { HSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { HSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
		if (Params->HasField(TEXT("size_fill")))
		{
			FSlateChildSize Size; Size.SizeRule = ESlateSizeRule::Fill; Size.Value = static_cast<float>(Params->GetNumberField(TEXT("size_fill")));
			HSlot->SetSize(Size); ++Applied;
		}
		else if (Params->HasField(TEXT("size_auto")) && Params->GetBoolField(TEXT("size_auto")))
		{
			FSlateChildSize Size; Size.SizeRule = ESlateSizeRule::Automatic;
			HSlot->SetSize(Size); ++Applied;
		}
	}
	else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		if (Params->HasField(TEXT("padding")))
		{
			VSlot->SetPadding(ParseMarginObj(Params->GetObjectField(TEXT("padding"))));
			++Applied;
		}
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { VSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { VSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
		if (Params->HasField(TEXT("size_fill")))
		{
			FSlateChildSize Size; Size.SizeRule = ESlateSizeRule::Fill; Size.Value = static_cast<float>(Params->GetNumberField(TEXT("size_fill")));
			VSlot->SetSize(Size); ++Applied;
		}
		else if (Params->HasField(TEXT("size_auto")) && Params->GetBoolField(TEXT("size_auto")))
		{
			FSlateChildSize Size; Size.SizeRule = ESlateSizeRule::Automatic;
			VSlot->SetSize(Size); ++Applied;
		}
	}
	else if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(Slot))
	{
		if (Params->HasField(TEXT("padding")))
		{
			OSlot->SetPadding(ParseMarginObj(Params->GetObjectField(TEXT("padding"))));
			++Applied;
		}
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { OSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { OSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
	}
	else if (UGridSlot* GSlot = Cast<UGridSlot>(Slot))
	{
		if (Params->HasField(TEXT("row")))         { GSlot->SetRow(static_cast<int32>(Params->GetIntegerField(TEXT("row")))); ++Applied; }
		if (Params->HasField(TEXT("column")))      { GSlot->SetColumn(static_cast<int32>(Params->GetIntegerField(TEXT("column")))); ++Applied; }
		if (Params->HasField(TEXT("row_span")))    { GSlot->SetRowSpan(static_cast<int32>(Params->GetIntegerField(TEXT("row_span")))); ++Applied; }
		if (Params->HasField(TEXT("column_span"))) { GSlot->SetColumnSpan(static_cast<int32>(Params->GetIntegerField(TEXT("column_span")))); ++Applied; }
		if (Params->HasField(TEXT("layer")))       { GSlot->SetLayer(static_cast<int32>(Params->GetIntegerField(TEXT("layer")))); ++Applied; }
		if (Params->HasField(TEXT("padding")))     { GSlot->SetPadding(ParseMarginObj(Params->GetObjectField(TEXT("padding")))); ++Applied; }
		if (Params->HasField(TEXT("nudge")))
		{
			const TSharedPtr<FJsonObject>& O = Params->GetObjectField(TEXT("nudge"));
			GSlot->SetNudge(FVector2D(O->GetNumberField(TEXT("x")), O->GetNumberField(TEXT("y"))));
			++Applied;
		}
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { GSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { GSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
	}
	else if (UUniformGridSlot* UGSlot = Cast<UUniformGridSlot>(Slot))
	{
		if (Params->HasField(TEXT("row")))    { UGSlot->SetRow(static_cast<int32>(Params->GetIntegerField(TEXT("row")))); ++Applied; }
		if (Params->HasField(TEXT("column"))) { UGSlot->SetColumn(static_cast<int32>(Params->GetIntegerField(TEXT("column")))); ++Applied; }
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { UGSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { UGSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
	}
	else if (UWrapBoxSlot* WSlot = Cast<UWrapBoxSlot>(Slot))
	{
		if (Params->HasField(TEXT("padding")))                  { WSlot->SetPadding(ParseMarginObj(Params->GetObjectField(TEXT("padding")))); ++Applied; }
		if (Params->HasField(TEXT("fill_empty_space")))         { WSlot->SetFillEmptySpace(Params->GetBoolField(TEXT("fill_empty_space"))); ++Applied; }
		if (Params->HasField(TEXT("fill_span_when_less_than"))) { WSlot->SetFillSpanWhenLessThan(static_cast<float>(Params->GetNumberField(TEXT("fill_span_when_less_than")))); ++Applied; }
		if (Params->HasField(TEXT("force_new_line")))           { WSlot->SetNewLine(Params->GetBoolField(TEXT("force_new_line"))); ++Applied; }
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { WSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { WSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
	}
	else if (UScrollBoxSlot* SBSlot = Cast<UScrollBoxSlot>(Slot))
	{
		if (Params->HasField(TEXT("padding"))) { SBSlot->SetPadding(ParseMarginObj(Params->GetObjectField(TEXT("padding")))); ++Applied; }
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { SBSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { SBSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
		// SetSize takes FSlateChildSize (Fill / Automatic) — same convention as HBox/VBox above.
		if (Params->HasField(TEXT("size_fill")))
		{
			FSlateChildSize Size; Size.SizeRule = ESlateSizeRule::Fill; Size.Value = static_cast<float>(Params->GetNumberField(TEXT("size_fill")));
			SBSlot->SetSize(Size); ++Applied;
		}
		else if (Params->HasField(TEXT("size_auto")) && Params->GetBoolField(TEXT("size_auto")))
		{
			FSlateChildSize Size; Size.SizeRule = ESlateSizeRule::Automatic;
			SBSlot->SetSize(Size); ++Applied;
		}
	}
	else if (UStackBoxSlot* StSlot = Cast<UStackBoxSlot>(Slot))
	{
		if (Params->HasField(TEXT("padding"))) { StSlot->SetPadding(ParseMarginObj(Params->GetObjectField(TEXT("padding")))); ++Applied; }
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { StSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { StSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
		if (Params->HasField(TEXT("size_fill")))
		{
			FSlateChildSize Size; Size.SizeRule = ESlateSizeRule::Fill; Size.Value = static_cast<float>(Params->GetNumberField(TEXT("size_fill")));
			StSlot->SetSize(Size); ++Applied;
		}
		else if (Params->HasField(TEXT("size_auto")) && Params->GetBoolField(TEXT("size_auto")))
		{
			FSlateChildSize Size; Size.SizeRule = ESlateSizeRule::Automatic;
			StSlot->SetSize(Size); ++Applied;
		}
	}
	else if (UBorderSlot* BSlot = Cast<UBorderSlot>(Slot))
	{
		if (Params->HasField(TEXT("padding"))) { BSlot->SetPadding(ParseMarginObj(Params->GetObjectField(TEXT("padding")))); ++Applied; }
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { BSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { BSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
	}
	else if (USizeBoxSlot* SzSlot = Cast<USizeBoxSlot>(Slot))
	{
		if (Params->HasField(TEXT("padding"))) { SzSlot->SetPadding(ParseMarginObj(Params->GetObjectField(TEXT("padding")))); ++Applied; }
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { SzSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { SzSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
	}
	else if (UScaleBoxSlot* ScSlot = Cast<UScaleBoxSlot>(Slot))
	{
		if (Params->HasField(TEXT("padding"))) { ScSlot->SetPadding(ParseMarginObj(Params->GetObjectField(TEXT("padding")))); ++Applied; }
		FString S;
		if (Params->TryGetStringField(TEXT("h_align"), S)) { ScSlot->SetHorizontalAlignment(ParseHAlign(S)); ++Applied; }
		if (Params->TryGetStringField(TEXT("v_align"), S)) { ScSlot->SetVerticalAlignment(ParseVAlign(S)); ++Applied; }
	}

	return Applied;
}

// ============================================================================
// Helper: collect a widget and all descendants
// ============================================================================

static void CollectWidgetAndDescendants(UWidget* Widget, TArray<UWidget*>& OutAll)
{
	if (!Widget) return;
	OutAll.Add(Widget);
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			CollectWidgetAndDescendants(Panel->GetChildAt(i), OutAll);
		}
	}
}

// ============================================================================
// Helper: reconcile WidgetVariableNameToGuidMap with the current tree state.
// Mirrors what FWidgetBlueprintCompilerContext::ValidateAndFixUpVariableGuids
// does, but without the ensureAlwaysMsgf at WidgetBlueprintCompiler.cpp:794
// (missing GUID) and :828 (orphan GUID).
// Call BEFORE FKismetEditorUtilities::CompileBlueprint when we've mutated
// the widget tree, so the compiler's own pass has nothing to ensure on.
// ============================================================================

static void PruneOrphanedWidgetVariableGuids(UWidgetBlueprint* WBP)
{
	if (!WBP)
	{
		return;
	}

	// IMPORTANT: walk the same set the compiler walks
	// (UBaseWidgetBlueprint::ForEachSourceWidgetImpl in BaseWidgetBlueprint.cpp).
	// That is: every UWidget directly outered to WidgetTree, regardless of
	// whether it's currently reachable via RootWidget/parent-child slots.
	// UWidgetTree::ForEachWidget would miss widgets that are outered but
	// unparented (e.g. during a move/reparent sequence), and the compiler's
	// ensures would still fire on them.
	// IMPORTANT: do NOT filter on bIsVariable. The compiler's
	// ValidateAndFixUpVariableGuids walks every UWidget directly outered to
	// the tree (via ForEachSourceWidget — see BaseWidgetBlueprint.cpp:86) and
	// ensures each one has a GUID, regardless of bIsVariable. Newly constructed
	// widgets default to bIsVariable=false, so filtering on it here would skip
	// adding GUIDs the compiler then ensures on (WidgetBlueprintCompiler.cpp:794).
	TSet<FName> SeenVariableNames;
	if (UWidgetTree* Tree = WBP->WidgetTree)
	{
		const bool bIncludeNestedObjects = false;
		ForEachObjectWithOuter(
			Tree,
			[&SeenVariableNames](UObject* Inner)
			{
				if (UWidget* AsWidget = Cast<UWidget>(Inner))
				{
					SeenVariableNames.Add(AsWidget->GetFName());
				}
			},
			bIncludeNestedObjects,
			RF_NoFlags,
			EInternalObjectFlags::Garbage // Exclude pending kills, matching compiler
		);
	}
	for (UWidgetAnimation* Animation : WBP->Animations)
	{
		if (Animation)
		{
			SeenVariableNames.Add(Animation->GetFName());
		}
	}

	bool bModified = false;

	// Drop entries whose names are no longer present (silences :828 ensure).
	for (auto It = WBP->WidgetVariableNameToGuidMap.CreateIterator(); It; ++It)
	{
		if (!SeenVariableNames.Contains(It.Key()))
		{
			It.RemoveCurrent();
			bModified = true;
		}
	}

	// Add entries for variable widgets/animations that don't have one yet
	// (silences :794 ensure when the bridge created widgets without going
	// through UWidgetBlueprint::OnVariableAdded).
	for (const FName& Name : SeenVariableNames)
	{
		if (!WBP->WidgetVariableNameToGuidMap.Contains(Name))
		{
			WBP->WidgetVariableNameToGuidMap.Emplace(Name, FGuid::NewGuid());
			bModified = true;
		}
	}

	if (bModified)
	{
		WBP->Modify();
	}
}

// Public re-export so EpicUnrealMCPBlueprintCommands.cpp (the generic
// compile_blueprint path) and any future caller can run the same
// reconciliation without duplicating the logic.
void EpicUnrealMCP_ReconcileWidgetBlueprintGuids(UWidgetBlueprint* WBP)
{
	PruneOrphanedWidgetVariableGuids(WBP);
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
	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
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

	// Animations: surface variable-eligible names to make GUID-map debugging visible.
	TArray<TSharedPtr<FJsonValue>> AnimsArray;
	for (UWidgetAnimation* Animation : WidgetBlueprint->Animations)
	{
		if (!Animation) continue;
		TSharedPtr<FJsonObject> AnimObj = MakeShareable(new FJsonObject);
		AnimObj->SetStringField(TEXT("name"), Animation->GetName());
		AnimObj->SetStringField(TEXT("display_label"), Animation->GetDisplayLabel());
		AnimsArray.Add(MakeShareable(new FJsonValueObject(AnimObj)));
	}
	Result->SetArrayField(TEXT("animations"), AnimsArray);
	Result->SetNumberField(TEXT("animation_count"), AnimsArray.Num());

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

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
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
	const FString ToolTipStr = Widget->GetToolTipText().ToString();
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
	if (FEpicUnrealMCPCommonUtils::AssetExists(FullPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint already exists: %s"), *FullPath));
	}

	// Resolve parent class (default: UUserWidget)
	UClass* ParentClass = UUserWidget::StaticClass();
	FString ParentClassName;
	if (Params->TryGetStringField(TEXT("parent_class"), ParentClassName) && !ParentClassName.IsEmpty())
	{
		// Try to find the class by name
		UClass* FoundClass = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
		if (!FoundClass)
		{
			// Try with U prefix
			FoundClass = FindFirstObject<UClass>(*(TEXT("U") + ParentClassName), EFindFirstObjectOptions::EnsureIfAmbiguous);
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
	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
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
		// List/tile/tree views — items populated at runtime by gameplay code.
		{TEXT("ListView"), UListView::StaticClass()},
		{TEXT("TileView"), UTileView::StaticClass()},
		{TEXT("TreeView"), UTreeView::StaticClass()},
		// Layout panels.
		{TEXT("WrapBox"), UWrapBox::StaticClass()},
		{TEXT("ScaleBox"), UScaleBox::StaticClass()},
		{TEXT("GridPanel"), UGridPanel::StaticClass()},
		{TEXT("UniformGridPanel"), UUniformGridPanel::StaticClass()},
		{TEXT("StackBox"), UStackBox::StaticClass()},
		{TEXT("ExpandableArea"), UExpandableArea::StaticClass()},
		{TEXT("InvalidationBox"), UInvalidationBox::StaticClass()},
		{TEXT("RetainerBox"), URetainerBox::StaticClass()},
		{TEXT("BackgroundBlur"), UBackgroundBlur::StaticClass()},
		{TEXT("SafeZone"), USafeZone::StaticClass()},
		{TEXT("NamedSlot"), UNamedSlot::StaticClass()},
		// Editable text family.
		{TEXT("EditableText"), UEditableText::StaticClass()},
		{TEXT("EditableTextBox"), UEditableTextBox::StaticClass()},
		{TEXT("MultiLineEditableText"), UMultiLineEditableText::StaticClass()},
		{TEXT("MultiLineEditableTextBox"), UMultiLineEditableTextBox::StaticClass()},
		// Numeric / dropdown / input.
		{TEXT("SpinBox"), USpinBox::StaticClass()},
		{TEXT("ComboBoxString"), UComboBoxString::StaticClass()},
		{TEXT("InputKeySelector"), UInputKeySelector::StaticClass()},
		// Throbbers.
		{TEXT("Throbber"), UThrobber::StaticClass()},
		{TEXT("CircularThrobber"), UCircularThrobber::StaticClass()},
		// CommonUI containers / widgets — added directly so callers don't depend on
		// FindFirstObject ambiguity rules. Containers extend UWidget (NOT UPanelWidget):
		// the WidgetTree treats them as leaves; their children are pushed at runtime.
		{TEXT("CommonActivatableWidgetStack"), UCommonActivatableWidgetStack::StaticClass()},
		{TEXT("CommonActivatableWidgetQueue"), UCommonActivatableWidgetQueue::StaticClass()},
		{TEXT("CommonActivatableWidgetSwitcher"), UCommonActivatableWidgetSwitcher::StaticClass()},
		{TEXT("CommonAnimatedSwitcher"), UCommonAnimatedSwitcher::StaticClass()},
		{TEXT("CommonVisibilitySwitcher"), UCommonVisibilitySwitcher::StaticClass()},
		{TEXT("CommonHierarchicalScrollBox"), UCommonHierarchicalScrollBox::StaticClass()},
		{TEXT("CommonBorder"), UCommonBorder::StaticClass()},
		{TEXT("CommonButtonBase"), UCommonButtonBase::StaticClass()},
		{TEXT("CommonTextBlock"), UCommonTextBlock::StaticClass()},
		{TEXT("CommonRichTextBlock"), UCommonRichTextBlock::StaticClass()},
		{TEXT("CommonLazyImage"), UCommonLazyImage::StaticClass()},
	};

	UClass* WidgetClass = nullptr;
	UClass* const* FoundBuiltinClass = WidgetClassMap.Find(WidgetType);
	if (FoundBuiltinClass)
	{
		WidgetClass = *FoundBuiltinClass;
	}
	else
	{
		// Only attempt asset load when the string is shaped like a content path
		// ("/Game/...", "/Engine/...", "/<plugin>/..."). Calling
		// UEditorAssetLibrary::LoadAsset on a bare class name like "ListView"
		// or "CommonActivatableWidgetContainer" logs a misleading
		// "does not map to a root" error from LogEditorAssetSubsystem.
		const bool bLooksLikeContentPath = WidgetType.StartsWith(TEXT("/"));
		if (bLooksLikeContentPath && !WidgetType.EndsWith(TEXT("_C")))
		{
			UObject* LoadedBP = UEditorAssetLibrary::LoadAsset(WidgetType);
			UWidgetBlueprint* CustomWBP = Cast<UWidgetBlueprint>(LoadedBP);
			if (CustomWBP && CustomWBP->GeneratedClass)
			{
				WidgetClass = CustomWBP->GeneratedClass;
			}
		}

		if (!WidgetClass)
		{
			// Try as a C++ class name
			WidgetClass = FindFirstObject<UClass>(*WidgetType, EFindFirstObjectOptions::EnsureIfAmbiguous);
			if (!WidgetClass)
			{
				WidgetClass = FindFirstObject<UClass>(*(TEXT("U") + WidgetType), EFindFirstObjectOptions::EnsureIfAmbiguous);
			}
		}

		if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass()))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Unknown widget type: %s. Use a built-in type (TextBlock, Image, Button, etc.), a C++ class name, or a Widget Blueprint asset path (/Game/...)"),
				*WidgetType));
		}
	}

	// Reject abstract classes before construction. StaticConstructObject_Internal
	// ensures (UObjectGlobals.cpp:3449) when given an abstract class — the resulting
	// widget is nulled out on save and corrupts the WBP. C++ widget bases (e.g.
	// UPRKSquadRosterStripWidget) are typically marked Abstract precisely because
	// they expect a derived WBP. Caller should pass the WBP asset path instead.
	if (WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget class '%s' is abstract and cannot be instantiated directly. Pass the Widget Blueprint asset path that derives from it (e.g. /Game/PRK/UI/Widgets/WBP_%s) instead of the C++ class name."),
			*WidgetClass->GetName(),
			*WidgetType));
	}

	// Create the widget via WidgetTree (correct way — sets Outer and RF_Transactional)
	UWidget* NewWidget = Tree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
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
			if (Tree->RootWidget != nullptr)
			{
				// Existing root is a non-panel widget. Refuse to silently orphan it
				// or auto-wrap it. Caller should use set_widget_root or move_widget.
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
					TEXT("Existing root '%s' is a non-panel widget (%s); cannot add a child without an explicit parent_name. Use set_widget_root or move_widget to restructure first."),
					*Tree->RootWidget->GetName(),
					*Tree->RootWidget->GetClass()->GetName()));
			}
			// Empty tree: install a sensible root.
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

	// Add as child. Single-child panels (UContentWidget subclasses: Border,
	// SizeBox, ScaleBox, RetainerBox, BackgroundBlur, NamedSlot, etc.) reject
	// the second child silently — UPanelWidget::AddChild returns nullptr
	// (PanelWidget.cpp:139) and the widget is left outered to the WidgetTree
	// but never parented, so compile discards it. Guard up-front so the
	// caller gets an actionable error and the orphan is cleaned up.
	UPanelSlot* Slot = nullptr;
	if (ParentPanel)
	{
		if (!ParentPanel->CanHaveMultipleChildren() && ParentPanel->GetChildrenCount() > 0)
		{
			const FString ExistingChildName = ParentPanel->GetChildAt(0)
				? ParentPanel->GetChildAt(0)->GetName()
				: TEXT("(unknown)");
			Tree->RemoveWidget(NewWidget);
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Parent '%s' is a single-child %s and already contains '%s'. "
					 "Pass a different parent_name, or wrap the existing child in a panel "
					 "(Overlay/VerticalBox/etc.) so the parent has room for another widget."),
				*ParentPanel->GetName(),
				*ParentPanel->GetClass()->GetName(),
				*ExistingChildName));
		}

		Slot = ParentPanel->AddChild(NewWidget);
		if (!Slot)
		{
			Tree->RemoveWidget(NewWidget);
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("AddChild returned null for parent '%s' (%s); widget was discarded. "
					 "Most common cause: the parent rejects this child class or has reached its slot limit."),
				*ParentPanel->GetName(),
				*ParentPanel->GetClass()->GetName()));
		}
	}

	// Apply slot properties (now routed for all supported slot types — Canvas/HBox/VBox/Overlay).
	ApplySlotProperties(Slot, Params);

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
	PruneOrphanedWidgetVariableGuids(WidgetBP);
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

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
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

		FString JustifyStr;
		if (Params->TryGetStringField(TEXT("justification"), JustifyStr))
		{
			ETextJustify::Type Justify = ETextJustify::Left;
			bool bJustifyOk = true;
			if      (JustifyStr == TEXT("Left"))   Justify = ETextJustify::Left;
			else if (JustifyStr == TEXT("Center")) Justify = ETextJustify::Center;
			else if (JustifyStr == TEXT("Right"))  Justify = ETextJustify::Right;
			else { bJustifyOk = false; }
			if (bJustifyOk)
			{
				TextWidget->SetJustification(Justify);
				SetFields.Add(TEXT("justification"));
			}
		}

		// Font size mutates the FSlateFontInfo; copy via Font getter, then SetFont.
		if (Params->HasField(TEXT("font_size")))
		{
			FSlateFontInfo FontInfo = TextWidget->GetFont();
			FontInfo.Size = static_cast<float>(Params->GetNumberField(TEXT("font_size")));
			TextWidget->SetFont(FontInfo);
			SetFields.Add(TEXT("font_size"));
		}

		if (Params->HasField(TEXT("shadow_color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("shadow_color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			TextWidget->SetShadowColorAndOpacity(Color);
			SetFields.Add(TEXT("shadow_color"));
		}

		if (Params->HasField(TEXT("shadow_offset")))
		{
			const TSharedPtr<FJsonObject>& Off = Params->GetObjectField(TEXT("shadow_offset"));
			FVector2D Offset(
				static_cast<float>(Off->GetNumberField(TEXT("x"))),
				static_cast<float>(Off->GetNumberField(TEXT("y")))
			);
			TextWidget->SetShadowOffset(Offset);
			SetFields.Add(TEXT("shadow_offset"));
		}

		if (Params->HasField(TEXT("min_desired_width")))
		{
			TextWidget->SetMinDesiredWidth(static_cast<float>(Params->GetNumberField(TEXT("min_desired_width"))));
			SetFields.Add(TEXT("min_desired_width"));
		}
	}

	// RichTextBlock-specific (sibling of TextBlock under UTextLayoutWidget)
	if (URichTextBlock* RichTextWidget = Cast<URichTextBlock>(Widget))
	{
		FString RichText;
		if (Params->TryGetStringField(TEXT("text"), RichText))
		{
			RichTextWidget->SetText(FText::FromString(RichText));
			SetFields.Add(TEXT("text"));
		}

		FString RichJustifyStr;
		if (Params->TryGetStringField(TEXT("justification"), RichJustifyStr))
		{
			ETextJustify::Type Justify = ETextJustify::Left;
			bool bJustifyOk = true;
			if      (RichJustifyStr == TEXT("Left"))   Justify = ETextJustify::Left;
			else if (RichJustifyStr == TEXT("Center")) Justify = ETextJustify::Center;
			else if (RichJustifyStr == TEXT("Right"))  Justify = ETextJustify::Right;
			else { bJustifyOk = false; }
			if (bJustifyOk)
			{
				RichTextWidget->SetJustification(Justify);
				SetFields.Add(TEXT("justification"));
			}
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

		FString ImageDrawAs;
		if (Params->TryGetStringField(TEXT("draw_as"), ImageDrawAs))
		{
			ESlateBrushDrawType::Type DrawType = ESlateBrushDrawType::Image;
			bool bDrawTypeOk = true;
			if      (ImageDrawAs == TEXT("NoDrawType")) DrawType = ESlateBrushDrawType::NoDrawType;
			else if (ImageDrawAs == TEXT("Box"))        DrawType = ESlateBrushDrawType::Box;
			else if (ImageDrawAs == TEXT("Border"))     DrawType = ESlateBrushDrawType::Border;
			else if (ImageDrawAs == TEXT("Image"))      DrawType = ESlateBrushDrawType::Image;
			else if (ImageDrawAs == TEXT("RoundedBox")) DrawType = ESlateBrushDrawType::RoundedBox;
			else { bDrawTypeOk = false; }
			if (bDrawTypeOk)
			{
				FSlateBrush Brush = ImageWidget->GetBrush();
				Brush.DrawAs = DrawType;
				ImageWidget->SetBrush(Brush);
				SetFields.Add(TEXT("draw_as"));
			}
		}

		if (Params->HasField(TEXT("brush_size")))
		{
			const TSharedPtr<FJsonObject>& Size = Params->GetObjectField(TEXT("brush_size"));
			FVector2D NewSize(
				static_cast<float>(Size->GetNumberField(TEXT("x"))),
				static_cast<float>(Size->GetNumberField(TEXT("y")))
			);
			ImageWidget->SetDesiredSizeOverride(NewSize);
			SetFields.Add(TEXT("brush_size"));
		}

		// 9-slice margin on the Image brush.
		if (Params->HasField(TEXT("margin")))
		{
			const TSharedPtr<FJsonObject>& M = Params->GetObjectField(TEXT("margin"));
			FSlateBrush Brush = ImageWidget->GetBrush();
			Brush.Margin = FMargin(
				static_cast<float>(M->GetNumberField(TEXT("left"))),
				static_cast<float>(M->GetNumberField(TEXT("top"))),
				static_cast<float>(M->GetNumberField(TEXT("right"))),
				static_cast<float>(M->GetNumberField(TEXT("bottom")))
			);
			ImageWidget->SetBrush(Brush);
			SetFields.Add(TEXT("margin"));
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

		// Fill (bar) tint — direct setter on UProgressBar.
		// "color" alias kept for parity with TextBlock/Image branches above.
		const TCHAR* FillColorKey = nullptr;
		if (Params->HasField(TEXT("fill_color"))) { FillColorKey = TEXT("fill_color"); }
		else if (Params->HasField(TEXT("color")))      { FillColorKey = TEXT("color"); }
		if (FillColorKey)
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(FillColorKey);
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			BarWidget->SetFillColorAndOpacity(Color);
			SetFields.Add(FString(FillColorKey));
		}

		// Background tint — there is no direct setter; the background brush
		// lives inside FProgressBarStyle. Copy-mutate-set is the only path.
		if (Params->HasField(TEXT("background_tint")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("background_tint"));
			FLinearColor Tint(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			FProgressBarStyle Style = BarWidget->GetWidgetStyle();
			Style.BackgroundImage.TintColor = FSlateColor(Tint);
			BarWidget->SetWidgetStyle(Style);
			SetFields.Add(TEXT("background_tint"));
		}

		// 9-slice margins on the ProgressBar style brushes (BackgroundImage,
		// FillImage). Same copy-mutate-SetWidgetStyle pattern as background_tint.
		if (Params->HasField(TEXT("background_margin")))
		{
			const TSharedPtr<FJsonObject>& M = Params->GetObjectField(TEXT("background_margin"));
			FProgressBarStyle Style = BarWidget->GetWidgetStyle();
			Style.BackgroundImage.Margin = FMargin(
				static_cast<float>(M->GetNumberField(TEXT("left"))),
				static_cast<float>(M->GetNumberField(TEXT("top"))),
				static_cast<float>(M->GetNumberField(TEXT("right"))),
				static_cast<float>(M->GetNumberField(TEXT("bottom")))
			);
			BarWidget->SetWidgetStyle(Style);
			SetFields.Add(TEXT("background_margin"));
		}

		if (Params->HasField(TEXT("fill_margin")))
		{
			const TSharedPtr<FJsonObject>& M = Params->GetObjectField(TEXT("fill_margin"));
			FProgressBarStyle Style = BarWidget->GetWidgetStyle();
			Style.FillImage.Margin = FMargin(
				static_cast<float>(M->GetNumberField(TEXT("left"))),
				static_cast<float>(M->GetNumberField(TEXT("top"))),
				static_cast<float>(M->GetNumberField(TEXT("right"))),
				static_cast<float>(M->GetNumberField(TEXT("bottom")))
			);
			BarWidget->SetWidgetStyle(Style);
			SetFields.Add(TEXT("fill_margin"));
		}

		FString FillTypeStr;
		if (Params->TryGetStringField(TEXT("bar_fill_type"), FillTypeStr))
		{
			EProgressBarFillType::Type FillType = EProgressBarFillType::LeftToRight;
			bool bFillTypeOk = true;
			if      (FillTypeStr == TEXT("LeftToRight"))             FillType = EProgressBarFillType::LeftToRight;
			else if (FillTypeStr == TEXT("RightToLeft"))             FillType = EProgressBarFillType::RightToLeft;
			else if (FillTypeStr == TEXT("TopToBottom"))             FillType = EProgressBarFillType::TopToBottom;
			else if (FillTypeStr == TEXT("BottomToTop"))             FillType = EProgressBarFillType::BottomToTop;
			else if (FillTypeStr == TEXT("FillFromCenter"))          FillType = EProgressBarFillType::FillFromCenter;
			else if (FillTypeStr == TEXT("FillFromCenterHorizontal")) FillType = EProgressBarFillType::FillFromCenterHorizontal;
			else if (FillTypeStr == TEXT("FillFromCenterVertical"))   FillType = EProgressBarFillType::FillFromCenterVertical;
			else { bFillTypeOk = false; }
			if (bFillTypeOk)
			{
				BarWidget->SetBarFillType(FillType);
				SetFields.Add(TEXT("bar_fill_type"));
			}
		}

		FString FillStyleStr;
		if (Params->TryGetStringField(TEXT("bar_fill_style"), FillStyleStr))
		{
			if (FillStyleStr == TEXT("Mask"))
			{
				BarWidget->SetBarFillStyle(EProgressBarFillStyle::Mask);
				SetFields.Add(TEXT("bar_fill_style"));
			}
			else if (FillStyleStr == TEXT("Scale"))
			{
				BarWidget->SetBarFillStyle(EProgressBarFillStyle::Scale);
				SetFields.Add(TEXT("bar_fill_style"));
			}
		}

		if (Params->HasField(TEXT("is_marquee")))
		{
			BarWidget->SetIsMarquee(Params->GetBoolField(TEXT("is_marquee")));
			SetFields.Add(TEXT("is_marquee"));
		}

		if (Params->HasField(TEXT("border_padding")))
		{
			const TSharedPtr<FJsonObject>& Pad = Params->GetObjectField(TEXT("border_padding"));
			FVector2D Padding(
				static_cast<float>(Pad->GetNumberField(TEXT("x"))),
				static_cast<float>(Pad->GetNumberField(TEXT("y")))
			);
			BarWidget->SetBorderPadding(Padding);
			SetFields.Add(TEXT("border_padding"));
		}
	}

	// Border-specific. UBorder::Background is an FSlateBrush struct member; DrawAs
	// has no direct setter — copy via GetBackground(), mutate, push via SetBrush().
	if (UBorder* BorderWidget = Cast<UBorder>(Widget))
	{
		if (Params->HasField(TEXT("background_color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("background_color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			BorderWidget->SetBrushColor(Color);
			SetFields.Add(TEXT("background_color"));
		}

		FString BorderTexturePath;
		if (Params->TryGetStringField(TEXT("background_texture"), BorderTexturePath))
		{
			UTexture2D* Texture = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(BorderTexturePath));
			if (Texture)
			{
				BorderWidget->SetBrushFromTexture(Texture);
				SetFields.Add(TEXT("background_texture"));
			}
		}

		FString BorderDrawAs;
		if (Params->TryGetStringField(TEXT("background_draw_as"), BorderDrawAs))
		{
			ESlateBrushDrawType::Type DrawType = ESlateBrushDrawType::Image;
			bool bDrawTypeOk = true;
			if      (BorderDrawAs == TEXT("NoDrawType")) DrawType = ESlateBrushDrawType::NoDrawType;
			else if (BorderDrawAs == TEXT("Box"))        DrawType = ESlateBrushDrawType::Box;
			else if (BorderDrawAs == TEXT("Border"))     DrawType = ESlateBrushDrawType::Border;
			else if (BorderDrawAs == TEXT("Image"))      DrawType = ESlateBrushDrawType::Image;
			else if (BorderDrawAs == TEXT("RoundedBox")) DrawType = ESlateBrushDrawType::RoundedBox;
			else { bDrawTypeOk = false; }
			if (bDrawTypeOk)
			{
				FSlateBrush Brush = BorderWidget->Background;
				Brush.DrawAs = DrawType;
				BorderWidget->SetBrush(Brush);
				SetFields.Add(TEXT("background_draw_as"));
			}
		}

		// 9-slice margin on the Background brush. Required when DrawAs is Box or
		// Border — without it the texture stretches as a single image and
		// corners distort on non-square widgets.
		if (Params->HasField(TEXT("background_margin")))
		{
			const TSharedPtr<FJsonObject>& M = Params->GetObjectField(TEXT("background_margin"));
			FSlateBrush Brush = BorderWidget->Background;
			Brush.Margin = FMargin(
				static_cast<float>(M->GetNumberField(TEXT("left"))),
				static_cast<float>(M->GetNumberField(TEXT("top"))),
				static_cast<float>(M->GetNumberField(TEXT("right"))),
				static_cast<float>(M->GetNumberField(TEXT("bottom")))
			);
			BorderWidget->SetBrush(Brush);
			SetFields.Add(TEXT("background_margin"));
		}

		if (Params->HasField(TEXT("padding")))
		{
			const TSharedPtr<FJsonObject>& Pad = Params->GetObjectField(TEXT("padding"));
			FMargin Margin(
				static_cast<float>(Pad->GetNumberField(TEXT("left"))),
				static_cast<float>(Pad->GetNumberField(TEXT("top"))),
				static_cast<float>(Pad->GetNumberField(TEXT("right"))),
				static_cast<float>(Pad->GetNumberField(TEXT("bottom")))
			);
			BorderWidget->SetPadding(Margin);
			SetFields.Add(TEXT("padding"));
		}

		FString HAlignStr;
		if (Params->TryGetStringField(TEXT("h_align"), HAlignStr))
		{
			EHorizontalAlignment Align = HAlign_Fill;
			bool bAlignOk = true;
			if      (HAlignStr == TEXT("Fill"))   Align = HAlign_Fill;
			else if (HAlignStr == TEXT("Left"))   Align = HAlign_Left;
			else if (HAlignStr == TEXT("Center")) Align = HAlign_Center;
			else if (HAlignStr == TEXT("Right"))  Align = HAlign_Right;
			else { bAlignOk = false; }
			if (bAlignOk)
			{
				BorderWidget->SetHorizontalAlignment(Align);
				SetFields.Add(TEXT("h_align"));
			}
		}

		FString VAlignStr;
		if (Params->TryGetStringField(TEXT("v_align"), VAlignStr))
		{
			EVerticalAlignment Align = VAlign_Fill;
			bool bAlignOk = true;
			if      (VAlignStr == TEXT("Fill"))   Align = VAlign_Fill;
			else if (VAlignStr == TEXT("Top"))    Align = VAlign_Top;
			else if (VAlignStr == TEXT("Center")) Align = VAlign_Center;
			else if (VAlignStr == TEXT("Bottom")) Align = VAlign_Bottom;
			else { bAlignOk = false; }
			if (bAlignOk)
			{
				BorderWidget->SetVerticalAlignment(Align);
				SetFields.Add(TEXT("v_align"));
			}
		}

		if (Params->HasField(TEXT("content_color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("content_color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			BorderWidget->SetContentColorAndOpacity(Color);
			SetFields.Add(TEXT("content_color"));
		}
	}

	// SizeBox-specific: per-axis size overrides.
	if (USizeBox* SizeBoxWidget = Cast<USizeBox>(Widget))
	{
		if (Params->HasField(TEXT("width_override")))
		{
			SizeBoxWidget->SetWidthOverride(static_cast<float>(Params->GetNumberField(TEXT("width_override"))));
			SetFields.Add(TEXT("width_override"));
		}
		if (Params->HasField(TEXT("height_override")))
		{
			SizeBoxWidget->SetHeightOverride(static_cast<float>(Params->GetNumberField(TEXT("height_override"))));
			SetFields.Add(TEXT("height_override"));
		}
		if (Params->HasField(TEXT("min_desired_width")))
		{
			SizeBoxWidget->SetMinDesiredWidth(static_cast<float>(Params->GetNumberField(TEXT("min_desired_width"))));
			SetFields.Add(TEXT("min_desired_width"));
		}
		if (Params->HasField(TEXT("min_desired_height")))
		{
			SizeBoxWidget->SetMinDesiredHeight(static_cast<float>(Params->GetNumberField(TEXT("min_desired_height"))));
			SetFields.Add(TEXT("min_desired_height"));
		}
		if (Params->HasField(TEXT("max_desired_width")))
		{
			SizeBoxWidget->SetMaxDesiredWidth(static_cast<float>(Params->GetNumberField(TEXT("max_desired_width"))));
			SetFields.Add(TEXT("max_desired_width"));
		}
		if (Params->HasField(TEXT("max_desired_height")))
		{
			SizeBoxWidget->SetMaxDesiredHeight(static_cast<float>(Params->GetNumberField(TEXT("max_desired_height"))));
			SetFields.Add(TEXT("max_desired_height"));
		}
	}

	// Spacer-specific: explicit slot size.
	if (USpacer* SpacerWidget = Cast<USpacer>(Widget))
	{
		if (Params->HasField(TEXT("spacer_size")))
		{
			const TSharedPtr<FJsonObject>& Size = Params->GetObjectField(TEXT("spacer_size"));
			FVector2D NewSize(
				static_cast<float>(Size->GetNumberField(TEXT("x"))),
				static_cast<float>(Size->GetNumberField(TEXT("y")))
			);
			SpacerWidget->SetSize(NewSize);
			SetFields.Add(TEXT("spacer_size"));
		}
	}

	// CheckBox-specific.
	if (UCheckBox* CheckBoxWidget = Cast<UCheckBox>(Widget))
	{
		if (Params->HasField(TEXT("is_checked")))
		{
			CheckBoxWidget->SetIsChecked(Params->GetBoolField(TEXT("is_checked")));
			SetFields.Add(TEXT("is_checked"));
		}
		FString CheckStateStr;
		if (Params->TryGetStringField(TEXT("check_state"), CheckStateStr))
		{
			ECheckBoxState State = ECheckBoxState::Unchecked;
			bool bStateOk = true;
			if      (CheckStateStr == TEXT("Unchecked"))    State = ECheckBoxState::Unchecked;
			else if (CheckStateStr == TEXT("Checked"))      State = ECheckBoxState::Checked;
			else if (CheckStateStr == TEXT("Undetermined")) State = ECheckBoxState::Undetermined;
			else { bStateOk = false; }
			if (bStateOk)
			{
				CheckBoxWidget->SetCheckedState(State);
				SetFields.Add(TEXT("check_state"));
			}
		}
	}

	// WidgetSwitcher-specific.
	if (UWidgetSwitcher* SwitcherWidget = Cast<UWidgetSwitcher>(Widget))
	{
		if (Params->HasField(TEXT("active_widget_index")))
		{
			SwitcherWidget->SetActiveWidgetIndex(static_cast<int32>(Params->GetIntegerField(TEXT("active_widget_index"))));
			SetFields.Add(TEXT("active_widget_index"));
		}
	}

	// Button-specific. Per-state brush colors require copy-mutate-set on the
	// FButtonStyle (no direct setter exists). The Style asset path
	// (set_widget_object_property) is preferred for full restyles; this branch
	// is for one-off tint tweaks.
	if (UButton* ButtonWidget = Cast<UButton>(Widget))
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
			ButtonWidget->SetColorAndOpacity(Color);
			SetFields.Add(TEXT("color"));
		}

		if (Params->HasField(TEXT("background_color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("background_color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			ButtonWidget->SetBackgroundColor(Color);
			SetFields.Add(TEXT("background_color"));
		}

		auto ApplyStateBrushColor = [&](const TCHAR* JsonKey, FSlateBrush& StateBrush, const TCHAR* FieldTag)
		{
			if (Params->HasField(JsonKey))
			{
				const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(JsonKey);
				FLinearColor Color(
					static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
					static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
					static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
					ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
				);
				StateBrush.TintColor = FSlateColor(Color);
				SetFields.Add(FieldTag);
			}
		};

		const bool bWantsStateColor =
			Params->HasField(TEXT("normal_color")) ||
			Params->HasField(TEXT("hovered_color")) ||
			Params->HasField(TEXT("pressed_color")) ||
			Params->HasField(TEXT("disabled_color"));
		if (bWantsStateColor)
		{
			FButtonStyle Style = ButtonWidget->GetStyle();
			ApplyStateBrushColor(TEXT("normal_color"),   Style.Normal,   TEXT("normal_color"));
			ApplyStateBrushColor(TEXT("hovered_color"),  Style.Hovered,  TEXT("hovered_color"));
			ApplyStateBrushColor(TEXT("pressed_color"),  Style.Pressed,  TEXT("pressed_color"));
			ApplyStateBrushColor(TEXT("disabled_color"), Style.Disabled, TEXT("disabled_color"));
			ButtonWidget->SetStyle(Style);
		}

		FString ClickMethodStr;
		if (Params->TryGetStringField(TEXT("click_method"), ClickMethodStr))
		{
			EButtonClickMethod::Type Method = EButtonClickMethod::DownAndUp;
			bool bOk = true;
			if      (ClickMethodStr == TEXT("DownAndUp"))    Method = EButtonClickMethod::DownAndUp;
			else if (ClickMethodStr == TEXT("MouseDown"))    Method = EButtonClickMethod::MouseDown;
			else if (ClickMethodStr == TEXT("MouseUp"))      Method = EButtonClickMethod::MouseUp;
			else if (ClickMethodStr == TEXT("PreciseClick")) Method = EButtonClickMethod::PreciseClick;
			else { bOk = false; }
			if (bOk)
			{
				ButtonWidget->SetClickMethod(Method);
				SetFields.Add(TEXT("click_method"));
			}
		}

		FString TouchMethodStr;
		if (Params->TryGetStringField(TEXT("touch_method"), TouchMethodStr))
		{
			EButtonTouchMethod::Type Method = EButtonTouchMethod::DownAndUp;
			bool bOk = true;
			if      (TouchMethodStr == TEXT("DownAndUp"))    Method = EButtonTouchMethod::DownAndUp;
			else if (TouchMethodStr == TEXT("Down"))         Method = EButtonTouchMethod::Down;
			else if (TouchMethodStr == TEXT("PreciseTap"))   Method = EButtonTouchMethod::PreciseTap;
			else { bOk = false; }
			if (bOk)
			{
				ButtonWidget->SetTouchMethod(Method);
				SetFields.Add(TEXT("touch_method"));
			}
		}

		FString PressMethodStr;
		if (Params->TryGetStringField(TEXT("press_method"), PressMethodStr))
		{
			EButtonPressMethod::Type Method = EButtonPressMethod::DownAndUp;
			bool bOk = true;
			if      (PressMethodStr == TEXT("DownAndUp"))    Method = EButtonPressMethod::DownAndUp;
			else if (PressMethodStr == TEXT("ButtonPress"))  Method = EButtonPressMethod::ButtonPress;
			else if (PressMethodStr == TEXT("ButtonRelease")) Method = EButtonPressMethod::ButtonRelease;
			else { bOk = false; }
			if (bOk)
			{
				ButtonWidget->SetPressMethod(Method);
				SetFields.Add(TEXT("press_method"));
			}
		}
	}

	// Slider-specific.
	if (USlider* SliderWidget = Cast<USlider>(Widget))
	{
		if (Params->HasField(TEXT("value")))
		{
			SliderWidget->SetValue(static_cast<float>(Params->GetNumberField(TEXT("value"))));
			SetFields.Add(TEXT("value"));
		}
		if (Params->HasField(TEXT("min_value")))
		{
			SliderWidget->SetMinValue(static_cast<float>(Params->GetNumberField(TEXT("min_value"))));
			SetFields.Add(TEXT("min_value"));
		}
		if (Params->HasField(TEXT("max_value")))
		{
			SliderWidget->SetMaxValue(static_cast<float>(Params->GetNumberField(TEXT("max_value"))));
			SetFields.Add(TEXT("max_value"));
		}
		if (Params->HasField(TEXT("step_size")))
		{
			SliderWidget->SetStepSize(static_cast<float>(Params->GetNumberField(TEXT("step_size"))));
			SetFields.Add(TEXT("step_size"));
		}
		if (Params->HasField(TEXT("locked")))
		{
			SliderWidget->SetLocked(Params->GetBoolField(TEXT("locked")));
			SetFields.Add(TEXT("locked"));
		}
		if (Params->HasField(TEXT("indent_handle")))
		{
			SliderWidget->SetIndentHandle(Params->GetBoolField(TEXT("indent_handle")));
			SetFields.Add(TEXT("indent_handle"));
		}
		FString OrientationStr;
		if (Params->TryGetStringField(TEXT("orientation"), OrientationStr))
		{
			EOrientation Orient = Orient_Horizontal;
			bool bOk = true;
			if      (OrientationStr == TEXT("Horizontal")) Orient = Orient_Horizontal;
			else if (OrientationStr == TEXT("Vertical"))   Orient = Orient_Vertical;
			else { bOk = false; }
			if (bOk)
			{
				SliderWidget->SetOrientation(Orient);
				SetFields.Add(TEXT("orientation"));
			}
		}
		if (Params->HasField(TEXT("slider_handle_color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("slider_handle_color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			SliderWidget->SetSliderHandleColor(Color);
			SetFields.Add(TEXT("slider_handle_color"));
		}
		if (Params->HasField(TEXT("slider_bar_color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("slider_bar_color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			SliderWidget->SetSliderBarColor(Color);
			SetFields.Add(TEXT("slider_bar_color"));
		}
	}

	// ScrollBox-specific.
	if (UScrollBox* ScrollBoxWidget = Cast<UScrollBox>(Widget))
	{
		FString OrientationStr;
		if (Params->TryGetStringField(TEXT("orientation"), OrientationStr))
		{
			EOrientation Orient = Orient_Vertical;
			bool bOk = true;
			if      (OrientationStr == TEXT("Horizontal")) Orient = Orient_Horizontal;
			else if (OrientationStr == TEXT("Vertical"))   Orient = Orient_Vertical;
			else { bOk = false; }
			if (bOk)
			{
				ScrollBoxWidget->SetOrientation(Orient);
				SetFields.Add(TEXT("orientation"));
			}
		}
		FString ScrollBarVisStr;
		if (Params->TryGetStringField(TEXT("scroll_bar_visibility"), ScrollBarVisStr))
		{
			ESlateVisibility Vis = ESlateVisibility::Visible;
			bool bOk = true;
			if      (ScrollBarVisStr == TEXT("Visible"))               Vis = ESlateVisibility::Visible;
			else if (ScrollBarVisStr == TEXT("Collapsed"))             Vis = ESlateVisibility::Collapsed;
			else if (ScrollBarVisStr == TEXT("Hidden"))                Vis = ESlateVisibility::Hidden;
			else if (ScrollBarVisStr == TEXT("HitTestInvisible"))      Vis = ESlateVisibility::HitTestInvisible;
			else if (ScrollBarVisStr == TEXT("SelfHitTestInvisible"))  Vis = ESlateVisibility::SelfHitTestInvisible;
			else { bOk = false; }
			if (bOk)
			{
				ScrollBoxWidget->SetScrollBarVisibility(Vis);
				SetFields.Add(TEXT("scroll_bar_visibility"));
			}
		}
		if (Params->HasField(TEXT("scrollbar_thickness")))
		{
			const TSharedPtr<FJsonObject>& Obj = Params->GetObjectField(TEXT("scrollbar_thickness"));
			FVector2D V(
				static_cast<float>(Obj->GetNumberField(TEXT("x"))),
				static_cast<float>(Obj->GetNumberField(TEXT("y")))
			);
			ScrollBoxWidget->SetScrollbarThickness(V);
			SetFields.Add(TEXT("scrollbar_thickness"));
		}
		if (Params->HasField(TEXT("scrollbar_padding")))
		{
			const TSharedPtr<FJsonObject>& Pad = Params->GetObjectField(TEXT("scrollbar_padding"));
			FMargin Margin(
				static_cast<float>(Pad->GetNumberField(TEXT("left"))),
				static_cast<float>(Pad->GetNumberField(TEXT("top"))),
				static_cast<float>(Pad->GetNumberField(TEXT("right"))),
				static_cast<float>(Pad->GetNumberField(TEXT("bottom")))
			);
			ScrollBoxWidget->SetScrollbarPadding(Margin);
			SetFields.Add(TEXT("scrollbar_padding"));
		}
		if (Params->HasField(TEXT("always_show_scrollbar")))
		{
			ScrollBoxWidget->SetAlwaysShowScrollbar(Params->GetBoolField(TEXT("always_show_scrollbar")));
			SetFields.Add(TEXT("always_show_scrollbar"));
		}
		if (Params->HasField(TEXT("always_show_scrollbar_track")))
		{
			ScrollBoxWidget->SetAlwaysShowScrollbarTrack(Params->GetBoolField(TEXT("always_show_scrollbar_track")));
			SetFields.Add(TEXT("always_show_scrollbar_track"));
		}
		if (Params->HasField(TEXT("allow_overscroll")))
		{
			ScrollBoxWidget->SetAllowOverscroll(Params->GetBoolField(TEXT("allow_overscroll")));
			SetFields.Add(TEXT("allow_overscroll"));
		}
		if (Params->HasField(TEXT("animate_wheel_scrolling")))
		{
			ScrollBoxWidget->SetAnimateWheelScrolling(Params->GetBoolField(TEXT("animate_wheel_scrolling")));
			SetFields.Add(TEXT("animate_wheel_scrolling"));
		}
		if (Params->HasField(TEXT("wheel_scroll_multiplier")))
		{
			ScrollBoxWidget->SetWheelScrollMultiplier(static_cast<float>(Params->GetNumberField(TEXT("wheel_scroll_multiplier"))));
			SetFields.Add(TEXT("wheel_scroll_multiplier"));
		}
		if (Params->HasField(TEXT("scroll_animation_interpolation_speed")))
		{
			ScrollBoxWidget->SetScrollAnimationInterpolationSpeed(static_cast<float>(Params->GetNumberField(TEXT("scroll_animation_interpolation_speed"))));
			SetFields.Add(TEXT("scroll_animation_interpolation_speed"));
		}
		if (Params->HasField(TEXT("scroll_offset")))
		{
			ScrollBoxWidget->SetScrollOffset(static_cast<float>(Params->GetNumberField(TEXT("scroll_offset"))));
			SetFields.Add(TEXT("scroll_offset"));
		}
		if (Params->HasField(TEXT("allow_right_click_drag_scrolling")))
		{
			ScrollBoxWidget->SetAllowRightClickDragScrolling(Params->GetBoolField(TEXT("allow_right_click_drag_scrolling")));
			SetFields.Add(TEXT("allow_right_click_drag_scrolling"));
		}
		FString ConsumeWheelStr;
		if (Params->TryGetStringField(TEXT("consume_mouse_wheel"), ConsumeWheelStr))
		{
			EConsumeMouseWheel Mode = EConsumeMouseWheel::WhenScrollingPossible;
			bool bOk = true;
			if      (ConsumeWheelStr == TEXT("WhenScrollingPossible")) Mode = EConsumeMouseWheel::WhenScrollingPossible;
			else if (ConsumeWheelStr == TEXT("Always"))                Mode = EConsumeMouseWheel::Always;
			else if (ConsumeWheelStr == TEXT("Never"))                 Mode = EConsumeMouseWheel::Never;
			else { bOk = false; }
			if (bOk)
			{
				ScrollBoxWidget->SetConsumeMouseWheel(Mode);
				SetFields.Add(TEXT("consume_mouse_wheel"));
			}
		}
		FString ScrollWhenFocusStr;
		if (Params->TryGetStringField(TEXT("scroll_when_focus_changes"), ScrollWhenFocusStr))
		{
			EScrollWhenFocusChanges Mode = EScrollWhenFocusChanges::NoScroll;
			bool bOk = true;
			if      (ScrollWhenFocusStr == TEXT("NoScroll"))         Mode = EScrollWhenFocusChanges::NoScroll;
			else if (ScrollWhenFocusStr == TEXT("InstantScroll"))    Mode = EScrollWhenFocusChanges::InstantScroll;
			else if (ScrollWhenFocusStr == TEXT("AnimatedScroll"))   Mode = EScrollWhenFocusChanges::AnimatedScroll;
			else { bOk = false; }
			if (bOk)
			{
				ScrollBoxWidget->SetScrollWhenFocusChanges(Mode);
				SetFields.Add(TEXT("scroll_when_focus_changes"));
			}
		}
		FString NavDestStr;
		if (Params->TryGetStringField(TEXT("navigation_destination"), NavDestStr))
		{
			EDescendantScrollDestination Dest = EDescendantScrollDestination::IntoView;
			bool bOk = true;
			if      (NavDestStr == TEXT("IntoView"))      Dest = EDescendantScrollDestination::IntoView;
			else if (NavDestStr == TEXT("TopOrLeft"))     Dest = EDescendantScrollDestination::TopOrLeft;
			else if (NavDestStr == TEXT("Center"))        Dest = EDescendantScrollDestination::Center;
			else if (NavDestStr == TEXT("BottomOrRight")) Dest = EDescendantScrollDestination::BottomOrRight;
			else { bOk = false; }
			if (bOk)
			{
				ScrollBoxWidget->SetNavigationDestination(Dest);
				SetFields.Add(TEXT("navigation_destination"));
			}
		}
		if (Params->HasField(TEXT("is_focusable")))
		{
			ScrollBoxWidget->SetIsFocusable(Params->GetBoolField(TEXT("is_focusable")));
			SetFields.Add(TEXT("is_focusable"));
		}
		if (Params->HasField(TEXT("consume_pointer_input")))
		{
			ScrollBoxWidget->SetConsumePointerInput(Params->GetBoolField(TEXT("consume_pointer_input")));
			SetFields.Add(TEXT("consume_pointer_input"));
		}
		if (Params->HasField(TEXT("is_touch_scrolling_enabled")))
		{
			ScrollBoxWidget->SetIsTouchScrollingEnabled(Params->GetBoolField(TEXT("is_touch_scrolling_enabled")));
			SetFields.Add(TEXT("is_touch_scrolling_enabled"));
		}
	}

	// EditableText-specific (single-line).
	if (UEditableText* EditTextWidget = Cast<UEditableText>(Widget))
	{
		FString TextStr;
		if (Params->TryGetStringField(TEXT("text"), TextStr))
		{
			EditTextWidget->SetText(FText::FromString(TextStr));
			SetFields.Add(TEXT("text"));
		}
		FString HintTextStr;
		if (Params->TryGetStringField(TEXT("hint_text"), HintTextStr))
		{
			EditTextWidget->SetHintText(FText::FromString(HintTextStr));
			SetFields.Add(TEXT("hint_text"));
		}
		if (Params->HasField(TEXT("is_password")))
		{
			EditTextWidget->SetIsPassword(Params->GetBoolField(TEXT("is_password")));
			SetFields.Add(TEXT("is_password"));
		}
		if (Params->HasField(TEXT("is_read_only")))
		{
			EditTextWidget->SetIsReadOnly(Params->GetBoolField(TEXT("is_read_only")));
			SetFields.Add(TEXT("is_read_only"));
		}
		if (Params->HasField(TEXT("is_caret_moved_when_gain_focus")))
		{
			EditTextWidget->SetIsCaretMovedWhenGainFocus(Params->GetBoolField(TEXT("is_caret_moved_when_gain_focus")));
			SetFields.Add(TEXT("is_caret_moved_when_gain_focus"));
		}
		if (Params->HasField(TEXT("select_all_text_when_focused")))
		{
			EditTextWidget->SetSelectAllTextWhenFocused(Params->GetBoolField(TEXT("select_all_text_when_focused")));
			SetFields.Add(TEXT("select_all_text_when_focused"));
		}
		if (Params->HasField(TEXT("revert_text_on_escape")))
		{
			EditTextWidget->SetRevertTextOnEscape(Params->GetBoolField(TEXT("revert_text_on_escape")));
			SetFields.Add(TEXT("revert_text_on_escape"));
		}
		if (Params->HasField(TEXT("clear_keyboard_focus_on_commit")))
		{
			EditTextWidget->SetClearKeyboardFocusOnCommit(Params->GetBoolField(TEXT("clear_keyboard_focus_on_commit")));
			SetFields.Add(TEXT("clear_keyboard_focus_on_commit"));
		}
		if (Params->HasField(TEXT("select_all_text_on_commit")))
		{
			EditTextWidget->SetSelectAllTextOnCommit(Params->GetBoolField(TEXT("select_all_text_on_commit")));
			SetFields.Add(TEXT("select_all_text_on_commit"));
		}
		if (Params->HasField(TEXT("min_desired_width")))
		{
			EditTextWidget->SetMinimumDesiredWidth(static_cast<float>(Params->GetNumberField(TEXT("min_desired_width"))));
			SetFields.Add(TEXT("min_desired_width"));
		}
		FString JustifyStr;
		if (Params->TryGetStringField(TEXT("justification"), JustifyStr))
		{
			ETextJustify::Type Justify = ETextJustify::Left;
			bool bJustifyOk = true;
			if      (JustifyStr == TEXT("Left"))   Justify = ETextJustify::Left;
			else if (JustifyStr == TEXT("Center")) Justify = ETextJustify::Center;
			else if (JustifyStr == TEXT("Right"))  Justify = ETextJustify::Right;
			else { bJustifyOk = false; }
			if (bJustifyOk)
			{
				EditTextWidget->SetJustification(Justify);
				SetFields.Add(TEXT("justification"));
			}
		}
		if (Params->HasField(TEXT("font_size")))
		{
			FSlateFontInfo FontInfo = EditTextWidget->GetFont();
			FontInfo.Size = static_cast<float>(Params->GetNumberField(TEXT("font_size")));
			EditTextWidget->SetFont(FontInfo);
			SetFields.Add(TEXT("font_size"));
		}
	}

	// EditableTextBox-specific (single-line).
	if (UEditableTextBox* EditTextBoxWidget = Cast<UEditableTextBox>(Widget))
	{
		FString TextStr;
		if (Params->TryGetStringField(TEXT("text"), TextStr))
		{
			EditTextBoxWidget->SetText(FText::FromString(TextStr));
			SetFields.Add(TEXT("text"));
		}
		FString HintTextStr;
		if (Params->TryGetStringField(TEXT("hint_text"), HintTextStr))
		{
			EditTextBoxWidget->SetHintText(FText::FromString(HintTextStr));
			SetFields.Add(TEXT("hint_text"));
		}
		FString ErrorStr;
		if (Params->TryGetStringField(TEXT("error_text"), ErrorStr))
		{
			EditTextBoxWidget->SetError(FText::FromString(ErrorStr));
			SetFields.Add(TEXT("error_text"));
		}
		if (Params->HasField(TEXT("is_password")))
		{
			EditTextBoxWidget->SetIsPassword(Params->GetBoolField(TEXT("is_password")));
			SetFields.Add(TEXT("is_password"));
		}
		if (Params->HasField(TEXT("is_read_only")))
		{
			EditTextBoxWidget->SetIsReadOnly(Params->GetBoolField(TEXT("is_read_only")));
			SetFields.Add(TEXT("is_read_only"));
		}
		if (Params->HasField(TEXT("min_desired_width")))
		{
			EditTextBoxWidget->SetMinDesiredWidth(static_cast<float>(Params->GetNumberField(TEXT("min_desired_width"))));
			SetFields.Add(TEXT("min_desired_width"));
		}
		if (Params->HasField(TEXT("foreground_color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("foreground_color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			EditTextBoxWidget->SetForegroundColor(Color);
			SetFields.Add(TEXT("foreground_color"));
		}
		FString JustifyStr;
		if (Params->TryGetStringField(TEXT("justification"), JustifyStr))
		{
			ETextJustify::Type Justify = ETextJustify::Left;
			bool bJustifyOk = true;
			if      (JustifyStr == TEXT("Left"))   Justify = ETextJustify::Left;
			else if (JustifyStr == TEXT("Center")) Justify = ETextJustify::Center;
			else if (JustifyStr == TEXT("Right"))  Justify = ETextJustify::Right;
			else { bJustifyOk = false; }
			if (bJustifyOk)
			{
				EditTextBoxWidget->SetJustification(Justify);
				SetFields.Add(TEXT("justification"));
			}
		}
		if (Params->HasField(TEXT("font_size")))
		{
			EditTextBoxWidget->WidgetStyle.TextStyle.Font.Size = static_cast<float>(Params->GetNumberField(TEXT("font_size")));
			EditTextBoxWidget->SynchronizeProperties();
			SetFields.Add(TEXT("font_size"));
		}
		if (Params->HasField(TEXT("background_color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("background_color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			EditTextBoxWidget->WidgetStyle.BackgroundColor = FSlateColor(Color);
			EditTextBoxWidget->SynchronizeProperties();
			SetFields.Add(TEXT("background_color"));
		}
	}

	// MultiLineEditableText-specific.
	if (UMultiLineEditableText* MultiTextWidget = Cast<UMultiLineEditableText>(Widget))
	{
		FString TextStr;
		if (Params->TryGetStringField(TEXT("text"), TextStr))
		{
			MultiTextWidget->SetText(FText::FromString(TextStr));
			SetFields.Add(TEXT("text"));
		}
		FString HintTextStr;
		if (Params->TryGetStringField(TEXT("hint_text"), HintTextStr))
		{
			MultiTextWidget->SetHintText(FText::FromString(HintTextStr));
			SetFields.Add(TEXT("hint_text"));
		}
		if (Params->HasField(TEXT("is_read_only")))
		{
			MultiTextWidget->SetIsReadOnly(Params->GetBoolField(TEXT("is_read_only")));
			SetFields.Add(TEXT("is_read_only"));
		}
		if (Params->HasField(TEXT("select_all_text_when_focused")))
		{
			MultiTextWidget->SetSelectAllTextWhenFocused(Params->GetBoolField(TEXT("select_all_text_when_focused")));
			SetFields.Add(TEXT("select_all_text_when_focused"));
		}
		if (Params->HasField(TEXT("revert_text_on_escape")))
		{
			MultiTextWidget->SetRevertTextOnEscape(Params->GetBoolField(TEXT("revert_text_on_escape")));
			SetFields.Add(TEXT("revert_text_on_escape"));
		}
		if (Params->HasField(TEXT("clear_keyboard_focus_on_commit")))
		{
			MultiTextWidget->SetClearKeyboardFocusOnCommit(Params->GetBoolField(TEXT("clear_keyboard_focus_on_commit")));
			SetFields.Add(TEXT("clear_keyboard_focus_on_commit"));
		}
		if (Params->HasField(TEXT("font_size")))
		{
			FSlateFontInfo FontInfo = MultiTextWidget->GetFont();
			FontInfo.Size = static_cast<float>(Params->GetNumberField(TEXT("font_size")));
			MultiTextWidget->SetFont(FontInfo);
			SetFields.Add(TEXT("font_size"));
		}
	}

	// MultiLineEditableTextBox-specific.
	if (UMultiLineEditableTextBox* MultiTextBoxWidget = Cast<UMultiLineEditableTextBox>(Widget))
	{
		FString TextStr;
		if (Params->TryGetStringField(TEXT("text"), TextStr))
		{
			MultiTextBoxWidget->SetText(FText::FromString(TextStr));
			SetFields.Add(TEXT("text"));
		}
		FString HintTextStr;
		if (Params->TryGetStringField(TEXT("hint_text"), HintTextStr))
		{
			MultiTextBoxWidget->SetHintText(FText::FromString(HintTextStr));
			SetFields.Add(TEXT("hint_text"));
		}
		FString ErrorStr;
		if (Params->TryGetStringField(TEXT("error_text"), ErrorStr))
		{
			MultiTextBoxWidget->SetError(FText::FromString(ErrorStr));
			SetFields.Add(TEXT("error_text"));
		}
		if (Params->HasField(TEXT("is_read_only")))
		{
			MultiTextBoxWidget->SetIsReadOnly(Params->GetBoolField(TEXT("is_read_only")));
			SetFields.Add(TEXT("is_read_only"));
		}
		if (Params->HasField(TEXT("foreground_color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("foreground_color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			// UMultiLineEditableTextBox::SetForegroundColor only pushes to the live
			// Slate widget, it does not write WidgetStyle.ForegroundColor — set the
			// struct field directly so the color survives blueprint recompilation.
			MultiTextBoxWidget->WidgetStyle.ForegroundColor = FSlateColor(Color);
			MultiTextBoxWidget->SynchronizeProperties();
			SetFields.Add(TEXT("foreground_color"));
		}
		if (Params->HasField(TEXT("background_color")))
		{
			const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("background_color"));
			FLinearColor Color(
				static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
				static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
				ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
			);
			MultiTextBoxWidget->WidgetStyle.BackgroundColor = FSlateColor(Color);
			MultiTextBoxWidget->SynchronizeProperties();
			SetFields.Add(TEXT("background_color"));
		}
		if (Params->HasField(TEXT("font_size")))
		{
			MultiTextBoxWidget->WidgetStyle.TextStyle.Font.Size = static_cast<float>(Params->GetNumberField(TEXT("font_size")));
			MultiTextBoxWidget->SynchronizeProperties();
			SetFields.Add(TEXT("font_size"));
		}
	}

	// ComboBoxString-specific.
	if (UComboBoxString* ComboWidget = Cast<UComboBoxString>(Widget))
	{
		// Replace the option list. Send an array; existing options are cleared first.
		const TArray<TSharedPtr<FJsonValue>>* OptionsArray = nullptr;
		if (Params->TryGetArrayField(TEXT("combo_options"), OptionsArray) && OptionsArray)
		{
			ComboWidget->ClearOptions();
			for (const TSharedPtr<FJsonValue>& Val : *OptionsArray)
			{
				if (Val.IsValid() && Val->Type == EJson::String)
				{
					ComboWidget->AddOption(Val->AsString());
				}
			}
			SetFields.Add(TEXT("combo_options"));
		}
		FString SelectedOption;
		if (Params->TryGetStringField(TEXT("selected_option"), SelectedOption))
		{
			ComboWidget->SetSelectedOption(SelectedOption);
			SetFields.Add(TEXT("selected_option"));
		}
		if (Params->HasField(TEXT("selected_index")))
		{
			ComboWidget->SetSelectedIndex(static_cast<int32>(Params->GetIntegerField(TEXT("selected_index"))));
			SetFields.Add(TEXT("selected_index"));
		}
		if (Params->HasField(TEXT("content_padding")))
		{
			const TSharedPtr<FJsonObject>& Pad = Params->GetObjectField(TEXT("content_padding"));
			FMargin Margin(
				static_cast<float>(Pad->GetNumberField(TEXT("left"))),
				static_cast<float>(Pad->GetNumberField(TEXT("top"))),
				static_cast<float>(Pad->GetNumberField(TEXT("right"))),
				static_cast<float>(Pad->GetNumberField(TEXT("bottom")))
			);
			ComboWidget->SetContentPadding(Margin);
			SetFields.Add(TEXT("content_padding"));
		}
		if (Params->HasField(TEXT("max_list_height")))
		{
			ComboWidget->SetMaxListHeight(static_cast<float>(Params->GetNumberField(TEXT("max_list_height"))));
			SetFields.Add(TEXT("max_list_height"));
		}
		if (Params->HasField(TEXT("has_down_arrow")))
		{
			ComboWidget->SetHasDownArrow(Params->GetBoolField(TEXT("has_down_arrow")));
			SetFields.Add(TEXT("has_down_arrow"));
		}
		if (Params->HasField(TEXT("enable_gamepad_navigation_mode")))
		{
			ComboWidget->SetEnableGamepadNavigationMode(Params->GetBoolField(TEXT("enable_gamepad_navigation_mode")));
			SetFields.Add(TEXT("enable_gamepad_navigation_mode"));
		}
	}

	// Throbber-specific.
	if (UThrobber* ThrobberWidget = Cast<UThrobber>(Widget))
	{
		if (Params->HasField(TEXT("number_of_pieces")))
		{
			ThrobberWidget->SetNumberOfPieces(static_cast<int32>(Params->GetIntegerField(TEXT("number_of_pieces"))));
			SetFields.Add(TEXT("number_of_pieces"));
		}
		if (Params->HasField(TEXT("animate_horizontally")))
		{
			ThrobberWidget->SetAnimateHorizontally(Params->GetBoolField(TEXT("animate_horizontally")));
			SetFields.Add(TEXT("animate_horizontally"));
		}
		if (Params->HasField(TEXT("animate_vertically")))
		{
			ThrobberWidget->SetAnimateVertically(Params->GetBoolField(TEXT("animate_vertically")));
			SetFields.Add(TEXT("animate_vertically"));
		}
		if (Params->HasField(TEXT("animate_opacity")))
		{
			ThrobberWidget->SetAnimateOpacity(Params->GetBoolField(TEXT("animate_opacity")));
			SetFields.Add(TEXT("animate_opacity"));
		}
	}

	// ListView-specific. Cast<UListView> also matches UTreeView (subclass);
	// UTileView extras get a separate branch below. Item *contents* are runtime
	// UObject*s and cannot be authored via JSON — populate from gameplay code.
	// Setters here are the design-time tuning surface (selection, scroll, spacing).
	if (UListView* ListWidget = Cast<UListView>(Widget))
	{
		FString SelectionModeStr;
		if (Params->TryGetStringField(TEXT("selection_mode"), SelectionModeStr))
		{
			ESelectionMode::Type Mode = ESelectionMode::Single;
			bool bOk = true;
			if      (SelectionModeStr == TEXT("None"))         Mode = ESelectionMode::None;
			else if (SelectionModeStr == TEXT("Single"))       Mode = ESelectionMode::Single;
			else if (SelectionModeStr == TEXT("SingleToggle")) Mode = ESelectionMode::SingleToggle;
			else if (SelectionModeStr == TEXT("Multi"))        Mode = ESelectionMode::Multi;
			else { bOk = false; }
			if (bOk)
			{
				ListWidget->SetSelectionMode(Mode);
				SetFields.Add(TEXT("selection_mode"));
			}
		}

		if (Params->HasField(TEXT("selected_index")))
		{
			ListWidget->SetSelectedIndex(static_cast<int32>(Params->GetIntegerField(TEXT("selected_index"))));
			SetFields.Add(TEXT("selected_index"));
		}

		// ListViewBase setters (also reachable on TileView/TreeView via inheritance).
		if (Params->HasField(TEXT("scroll_offset")))
		{
			ListWidget->SetScrollOffset(static_cast<float>(Params->GetNumberField(TEXT("scroll_offset"))));
			SetFields.Add(TEXT("scroll_offset"));
		}
		if (Params->HasField(TEXT("wheel_scroll_multiplier")))
		{
			ListWidget->SetWheelScrollMultiplier(static_cast<float>(Params->GetNumberField(TEXT("wheel_scroll_multiplier"))));
			SetFields.Add(TEXT("wheel_scroll_multiplier"));
		}
		FString ScrollBarVisStr;
		if (Params->TryGetStringField(TEXT("scrollbar_visibility"), ScrollBarVisStr))
		{
			ESlateVisibility Vis = ESlateVisibility::Visible;
			bool bOk = true;
			if      (ScrollBarVisStr == TEXT("Visible"))              Vis = ESlateVisibility::Visible;
			else if (ScrollBarVisStr == TEXT("Collapsed"))            Vis = ESlateVisibility::Collapsed;
			else if (ScrollBarVisStr == TEXT("Hidden"))               Vis = ESlateVisibility::Hidden;
			else if (ScrollBarVisStr == TEXT("HitTestInvisible"))     Vis = ESlateVisibility::HitTestInvisible;
			else if (ScrollBarVisStr == TEXT("SelfHitTestInvisible")) Vis = ESlateVisibility::SelfHitTestInvisible;
			else { bOk = false; }
			if (bOk)
			{
				ListWidget->SetScrollbarVisibility(Vis);
				SetFields.Add(TEXT("scrollbar_visibility"));
			}
		}
		if (Params->HasField(TEXT("allow_overscroll")))
		{
			ListWidget->SetAllowOverScroll(Params->GetBoolField(TEXT("allow_overscroll")));
			SetFields.Add(TEXT("allow_overscroll"));
		}
		if (Params->HasField(TEXT("is_pointer_scrolling_enabled")))
		{
			ListWidget->SetIsPointerScrollingEnabled(Params->GetBoolField(TEXT("is_pointer_scrolling_enabled")));
			SetFields.Add(TEXT("is_pointer_scrolling_enabled"));
		}
		if (Params->HasField(TEXT("is_gamepad_scrolling_enabled")))
		{
			ListWidget->SetIsGamepadScrollingEnabled(Params->GetBoolField(TEXT("is_gamepad_scrolling_enabled")));
			SetFields.Add(TEXT("is_gamepad_scrolling_enabled"));
		}

		// ListView-specific spacing & scroll alignment.
		if (Params->HasField(TEXT("vertical_entry_spacing")))
		{
			ListWidget->SetVerticalEntrySpacing(static_cast<float>(Params->GetNumberField(TEXT("vertical_entry_spacing"))));
			SetFields.Add(TEXT("vertical_entry_spacing"));
		}
		if (Params->HasField(TEXT("horizontal_entry_spacing")))
		{
			ListWidget->SetHorizontalEntrySpacing(static_cast<float>(Params->GetNumberField(TEXT("horizontal_entry_spacing"))));
			SetFields.Add(TEXT("horizontal_entry_spacing"));
		}
		if (Params->HasField(TEXT("scrollbar_padding")))
		{
			const TSharedPtr<FJsonObject>& Pad = Params->GetObjectField(TEXT("scrollbar_padding"));
			FMargin Margin(
				static_cast<float>(Pad->GetNumberField(TEXT("left"))),
				static_cast<float>(Pad->GetNumberField(TEXT("top"))),
				static_cast<float>(Pad->GetNumberField(TEXT("right"))),
				static_cast<float>(Pad->GetNumberField(TEXT("bottom")))
			);
			ListWidget->SetScrollBarPadding(Margin);
			SetFields.Add(TEXT("scrollbar_padding"));
		}
		FString ScrollAlignStr;
		if (Params->TryGetStringField(TEXT("scroll_into_view_alignment"), ScrollAlignStr))
		{
			EScrollIntoViewAlignment Align = EScrollIntoViewAlignment::CenterAligned;
			bool bOk = true;
			if      (ScrollAlignStr == TEXT("IntoView"))      Align = EScrollIntoViewAlignment::IntoView;
			else if (ScrollAlignStr == TEXT("TopOrLeft"))     Align = EScrollIntoViewAlignment::TopOrLeft;
			else if (ScrollAlignStr == TEXT("CenterAligned")) Align = EScrollIntoViewAlignment::CenterAligned;
			else if (ScrollAlignStr == TEXT("BottomOrRight")) Align = EScrollIntoViewAlignment::BottomOrRight;
			else { bOk = false; }
			if (bOk)
			{
				ListWidget->SetScrollIntoViewAlignment(Align);
				SetFields.Add(TEXT("scroll_into_view_alignment"));
			}
		}

		// Imperative actions. Boolean-true triggers; integer index targets.
		if (Params->HasField(TEXT("clear_list_items")) && Params->GetBoolField(TEXT("clear_list_items")))
		{
			ListWidget->ClearListItems();
			SetFields.Add(TEXT("clear_list_items"));
		}
		if (Params->HasField(TEXT("scroll_to_top")) && Params->GetBoolField(TEXT("scroll_to_top")))
		{
			ListWidget->ScrollToTop();
			SetFields.Add(TEXT("scroll_to_top"));
		}
		if (Params->HasField(TEXT("scroll_to_bottom")) && Params->GetBoolField(TEXT("scroll_to_bottom")))
		{
			ListWidget->ScrollToBottom();
			SetFields.Add(TEXT("scroll_to_bottom"));
		}
		if (Params->HasField(TEXT("request_refresh")) && Params->GetBoolField(TEXT("request_refresh")))
		{
			ListWidget->RequestRefresh();
			SetFields.Add(TEXT("request_refresh"));
		}
		if (Params->HasField(TEXT("regenerate_all_entries")) && Params->GetBoolField(TEXT("regenerate_all_entries")))
		{
			ListWidget->RegenerateAllEntries();
			SetFields.Add(TEXT("regenerate_all_entries"));
		}
		if (Params->HasField(TEXT("scroll_index_into_view")))
		{
			ListWidget->ScrollIndexIntoView(static_cast<int32>(Params->GetIntegerField(TEXT("scroll_index_into_view"))));
			SetFields.Add(TEXT("scroll_index_into_view"));
		}
	}

	// TileView extras: per-tile dimensions. EntrySizeRule and TileAlignment are
	// UPROPERTY-only (no direct setter); not exposed here — set via WBP defaults.
	if (UTileView* TileWidget = Cast<UTileView>(Widget))
	{
		if (Params->HasField(TEXT("entry_height")))
		{
			TileWidget->SetEntryHeight(static_cast<float>(Params->GetNumberField(TEXT("entry_height"))));
			SetFields.Add(TEXT("entry_height"));
		}
		if (Params->HasField(TEXT("entry_width")))
		{
			TileWidget->SetEntryWidth(static_cast<float>(Params->GetNumberField(TEXT("entry_width"))));
			SetFields.Add(TEXT("entry_width"));
		}
	}

	// TreeView extras: bulk expand / collapse actions. Per-item expansion needs
	// a runtime UObject* and is not authorable via JSON.
	if (UTreeView* TreeWidget = Cast<UTreeView>(Widget))
	{
		if (Params->HasField(TEXT("expand_all")) && Params->GetBoolField(TEXT("expand_all")))
		{
			TreeWidget->ExpandAll();
			SetFields.Add(TEXT("expand_all"));
		}
		if (Params->HasField(TEXT("collapse_all")) && Params->GetBoolField(TEXT("collapse_all")))
		{
			TreeWidget->CollapseAll();
			SetFields.Add(TEXT("collapse_all"));
		}
	}

	// WrapBox-specific.
	if (UWrapBox* WrapBoxWidget = Cast<UWrapBox>(Widget))
	{
		if (Params->HasField(TEXT("inner_slot_padding")))
		{
			const TSharedPtr<FJsonObject>& Pad = Params->GetObjectField(TEXT("inner_slot_padding"));
			FVector2D Padding(
				static_cast<float>(Pad->GetNumberField(TEXT("x"))),
				static_cast<float>(Pad->GetNumberField(TEXT("y")))
			);
			WrapBoxWidget->SetInnerSlotPadding(Padding);
			SetFields.Add(TEXT("inner_slot_padding"));
		}
		if (Params->HasField(TEXT("wrap_size")))
		{
			WrapBoxWidget->SetWrapSize(static_cast<float>(Params->GetNumberField(TEXT("wrap_size"))));
			SetFields.Add(TEXT("wrap_size"));
		}
		if (Params->HasField(TEXT("explicit_wrap_size")))
		{
			WrapBoxWidget->SetExplicitWrapSize(Params->GetBoolField(TEXT("explicit_wrap_size")));
			SetFields.Add(TEXT("explicit_wrap_size"));
		}
		FString WrapHAlignStr;
		if (Params->TryGetStringField(TEXT("h_align"), WrapHAlignStr))
		{
			EHorizontalAlignment Align = HAlign_Fill;
			bool bAlignOk = true;
			if      (WrapHAlignStr == TEXT("Fill"))   Align = HAlign_Fill;
			else if (WrapHAlignStr == TEXT("Left"))   Align = HAlign_Left;
			else if (WrapHAlignStr == TEXT("Center")) Align = HAlign_Center;
			else if (WrapHAlignStr == TEXT("Right"))  Align = HAlign_Right;
			else { bAlignOk = false; }
			if (bAlignOk)
			{
				WrapBoxWidget->SetHorizontalAlignment(Align);
				SetFields.Add(TEXT("h_align"));
			}
		}
		FString WrapOrientStr;
		if (Params->TryGetStringField(TEXT("orientation"), WrapOrientStr))
		{
			EOrientation Orient = Orient_Horizontal;
			bool bOk = true;
			if      (WrapOrientStr == TEXT("Horizontal")) Orient = Orient_Horizontal;
			else if (WrapOrientStr == TEXT("Vertical"))   Orient = Orient_Vertical;
			else { bOk = false; }
			if (bOk)
			{
				WrapBoxWidget->SetOrientation(Orient);
				SetFields.Add(TEXT("orientation"));
			}
		}
	}

	// ScaleBox-specific. Stretch + StretchDirection use namespaced enums
	// (EStretch::Type / EStretchDirection::Type), not enum class.
	if (UScaleBox* ScaleBoxWidget = Cast<UScaleBox>(Widget))
	{
		FString StretchStr;
		if (Params->TryGetStringField(TEXT("stretch"), StretchStr))
		{
			EStretch::Type S = EStretch::None;
			bool bOk = true;
			if      (StretchStr == TEXT("None"))             S = EStretch::None;
			else if (StretchStr == TEXT("Fill"))             S = EStretch::Fill;
			else if (StretchStr == TEXT("ScaleToFit"))       S = EStretch::ScaleToFit;
			else if (StretchStr == TEXT("ScaleToFitX"))      S = EStretch::ScaleToFitX;
			else if (StretchStr == TEXT("ScaleToFitY"))      S = EStretch::ScaleToFitY;
			else if (StretchStr == TEXT("ScaleToFill"))      S = EStretch::ScaleToFill;
			else if (StretchStr == TEXT("ScaleBySafeZone"))  S = EStretch::ScaleBySafeZone;
			else if (StretchStr == TEXT("UserSpecified"))    S = EStretch::UserSpecified;
			else if (StretchStr == TEXT("UserSpecifiedWithClipping")) S = EStretch::UserSpecifiedWithClipping;
			else { bOk = false; }
			if (bOk)
			{
				ScaleBoxWidget->SetStretch(S);
				SetFields.Add(TEXT("stretch"));
			}
		}
		FString StretchDirStr;
		if (Params->TryGetStringField(TEXT("stretch_direction"), StretchDirStr))
		{
			EStretchDirection::Type D = EStretchDirection::Both;
			bool bOk = true;
			if      (StretchDirStr == TEXT("Both"))           D = EStretchDirection::Both;
			else if (StretchDirStr == TEXT("DownOnly"))       D = EStretchDirection::DownOnly;
			else if (StretchDirStr == TEXT("UpOnly"))         D = EStretchDirection::UpOnly;
			else { bOk = false; }
			if (bOk)
			{
				ScaleBoxWidget->SetStretchDirection(D);
				SetFields.Add(TEXT("stretch_direction"));
			}
		}
		if (Params->HasField(TEXT("user_specified_scale")))
		{
			ScaleBoxWidget->SetUserSpecifiedScale(static_cast<float>(Params->GetNumberField(TEXT("user_specified_scale"))));
			SetFields.Add(TEXT("user_specified_scale"));
		}
		if (Params->HasField(TEXT("ignore_inherited_scale")))
		{
			ScaleBoxWidget->SetIgnoreInheritedScale(Params->GetBoolField(TEXT("ignore_inherited_scale")));
			SetFields.Add(TEXT("ignore_inherited_scale"));
		}
	}

	// SpinBox-specific.
	if (USpinBox* SpinBoxWidget = Cast<USpinBox>(Widget))
	{
		if (Params->HasField(TEXT("value")))
		{
			SpinBoxWidget->SetValue(static_cast<float>(Params->GetNumberField(TEXT("value"))));
			SetFields.Add(TEXT("value"));
		}
		if (Params->HasField(TEXT("min_value")))
		{
			SpinBoxWidget->SetMinValue(static_cast<float>(Params->GetNumberField(TEXT("min_value"))));
			SetFields.Add(TEXT("min_value"));
		}
		if (Params->HasField(TEXT("max_value")))
		{
			SpinBoxWidget->SetMaxValue(static_cast<float>(Params->GetNumberField(TEXT("max_value"))));
			SetFields.Add(TEXT("max_value"));
		}
		if (Params->HasField(TEXT("min_slider_value")))
		{
			SpinBoxWidget->SetMinSliderValue(static_cast<float>(Params->GetNumberField(TEXT("min_slider_value"))));
			SetFields.Add(TEXT("min_slider_value"));
		}
		if (Params->HasField(TEXT("max_slider_value")))
		{
			SpinBoxWidget->SetMaxSliderValue(static_cast<float>(Params->GetNumberField(TEXT("max_slider_value"))));
			SetFields.Add(TEXT("max_slider_value"));
		}
		if (Params->HasField(TEXT("delta")))
		{
			SpinBoxWidget->SetDelta(static_cast<float>(Params->GetNumberField(TEXT("delta"))));
			SetFields.Add(TEXT("delta"));
		}
		if (Params->HasField(TEXT("slider_exponent")))
		{
			SpinBoxWidget->SetSliderExponent(static_cast<float>(Params->GetNumberField(TEXT("slider_exponent"))));
			SetFields.Add(TEXT("slider_exponent"));
		}
		if (Params->HasField(TEXT("min_desired_width")))
		{
			SpinBoxWidget->SetMinDesiredWidth(static_cast<float>(Params->GetNumberField(TEXT("min_desired_width"))));
			SetFields.Add(TEXT("min_desired_width"));
		}
		if (Params->HasField(TEXT("min_fractional_digits")))
		{
			SpinBoxWidget->SetMinFractionalDigits(static_cast<int32>(Params->GetIntegerField(TEXT("min_fractional_digits"))));
			SetFields.Add(TEXT("min_fractional_digits"));
		}
		if (Params->HasField(TEXT("max_fractional_digits")))
		{
			SpinBoxWidget->SetMaxFractionalDigits(static_cast<int32>(Params->GetIntegerField(TEXT("max_fractional_digits"))));
			SetFields.Add(TEXT("max_fractional_digits"));
		}
		if (Params->HasField(TEXT("always_uses_delta_snap")))
		{
			SpinBoxWidget->SetAlwaysUsesDeltaSnap(Params->GetBoolField(TEXT("always_uses_delta_snap")));
			SetFields.Add(TEXT("always_uses_delta_snap"));
		}
		if (Params->HasField(TEXT("enable_slider")))
		{
			SpinBoxWidget->SetEnableSlider(Params->GetBoolField(TEXT("enable_slider")));
			SetFields.Add(TEXT("enable_slider"));
		}
		if (Params->HasField(TEXT("clear_keyboard_focus_on_commit")))
		{
			SpinBoxWidget->SetClearKeyboardFocusOnCommit(Params->GetBoolField(TEXT("clear_keyboard_focus_on_commit")));
			SetFields.Add(TEXT("clear_keyboard_focus_on_commit"));
		}
		if (Params->HasField(TEXT("select_all_text_on_commit")))
		{
			SpinBoxWidget->SetSelectAllTextOnCommit(Params->GetBoolField(TEXT("select_all_text_on_commit")));
			SetFields.Add(TEXT("select_all_text_on_commit"));
		}
		FString SpinJustifyStr;
		if (Params->TryGetStringField(TEXT("justification"), SpinJustifyStr))
		{
			ETextJustify::Type Justify = ETextJustify::Right;
			bool bJustifyOk = true;
			if      (SpinJustifyStr == TEXT("Left"))   Justify = ETextJustify::Left;
			else if (SpinJustifyStr == TEXT("Center")) Justify = ETextJustify::Center;
			else if (SpinJustifyStr == TEXT("Right"))  Justify = ETextJustify::Right;
			else { bJustifyOk = false; }
			if (bJustifyOk)
			{
				SpinBoxWidget->SetJustification(Justify);
				SetFields.Add(TEXT("justification"));
			}
		}
	}

	// ExpandableArea-specific.
	if (UExpandableArea* ExpAreaWidget = Cast<UExpandableArea>(Widget))
	{
		if (Params->HasField(TEXT("is_expanded")))
		{
			ExpAreaWidget->SetIsExpanded(Params->GetBoolField(TEXT("is_expanded")));
			SetFields.Add(TEXT("is_expanded"));
		}
		if (Params->HasField(TEXT("is_expanded_animated")))
		{
			ExpAreaWidget->SetIsExpanded_Animated(Params->GetBoolField(TEXT("is_expanded_animated")));
			SetFields.Add(TEXT("is_expanded_animated"));
		}
		if (Params->HasField(TEXT("max_height")))
		{
			ExpAreaWidget->SetMaxHeight(static_cast<float>(Params->GetNumberField(TEXT("max_height"))));
			SetFields.Add(TEXT("max_height"));
		}
		if (Params->HasField(TEXT("header_padding")))
		{
			const TSharedPtr<FJsonObject>& M = Params->GetObjectField(TEXT("header_padding"));
			ExpAreaWidget->SetHeaderPadding(FMargin(
				static_cast<float>(M->GetNumberField(TEXT("left"))),
				static_cast<float>(M->GetNumberField(TEXT("top"))),
				static_cast<float>(M->GetNumberField(TEXT("right"))),
				static_cast<float>(M->GetNumberField(TEXT("bottom")))
			));
			SetFields.Add(TEXT("header_padding"));
		}
		if (Params->HasField(TEXT("area_padding")))
		{
			const TSharedPtr<FJsonObject>& M = Params->GetObjectField(TEXT("area_padding"));
			ExpAreaWidget->SetAreaPadding(FMargin(
				static_cast<float>(M->GetNumberField(TEXT("left"))),
				static_cast<float>(M->GetNumberField(TEXT("top"))),
				static_cast<float>(M->GetNumberField(TEXT("right"))),
				static_cast<float>(M->GetNumberField(TEXT("bottom")))
			));
			SetFields.Add(TEXT("area_padding"));
		}
		if (Params->HasField(TEXT("border_color")))
		{
			const TSharedPtr<FJsonObject>& C = Params->GetObjectField(TEXT("border_color"));
			FLinearColor Color(
				static_cast<float>(C->GetNumberField(TEXT("r"))),
				static_cast<float>(C->GetNumberField(TEXT("g"))),
				static_cast<float>(C->GetNumberField(TEXT("b"))),
				C->HasField(TEXT("a")) ? static_cast<float>(C->GetNumberField(TEXT("a"))) : 1.0f
			);
			ExpAreaWidget->SetBorderColor(FSlateColor(Color));
			SetFields.Add(TEXT("border_color"));
		}
	}

	// CircularThrobber-specific.
	if (UCircularThrobber* CircThrobberWidget = Cast<UCircularThrobber>(Widget))
	{
		if (Params->HasField(TEXT("number_of_pieces")))
		{
			CircThrobberWidget->SetNumberOfPieces(static_cast<int32>(Params->GetIntegerField(TEXT("number_of_pieces"))));
			SetFields.Add(TEXT("number_of_pieces"));
		}
		if (Params->HasField(TEXT("period")))
		{
			CircThrobberWidget->SetPeriod(static_cast<float>(Params->GetNumberField(TEXT("period"))));
			SetFields.Add(TEXT("period"));
		}
		if (Params->HasField(TEXT("radius")))
		{
			CircThrobberWidget->SetRadius(static_cast<float>(Params->GetNumberField(TEXT("radius"))));
			SetFields.Add(TEXT("radius"));
		}
	}

	// Compile only if something actually changed
	if (SetFields.Num() > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
		PruneOrphanedWidgetVariableGuids(WidgetBP);
		FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	}

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

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
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

	// Collect variable names of the widget and all descendants BEFORE removal —
	// descendants are removed transitively and would otherwise leave stale entries
	// in WidgetVariableNameToGuidMap.
	TArray<UWidget*> ToRemove;
	CollectWidgetAndDescendants(Widget, ToRemove);
	TArray<FName> RemovedVariableNames;
	for (UWidget* W : ToRemove)
	{
		if (W && W->bIsVariable)
		{
			RemovedVariableNames.Add(W->GetFName());
		}
	}

	// UWidgetTree::RemoveWidget handles the parent unparent + tree root fixup.
	bool bRemoved = WidgetBP->WidgetTree->RemoveWidget(Widget);

	if (bRemoved)
	{
		for (const FName& VarName : RemovedVariableNames)
		{
			WidgetBP->OnVariableRemoved(VarName);
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
		PruneOrphanedWidgetVariableGuids(WidgetBP);
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

// ============================================================================
// Helper: load and validate a Widget Blueprint asset by path
// ============================================================================

static UWidgetBlueprint* LoadWidgetBlueprint(const FString& AssetPath, FString& OutError)
{
	UObject* Loaded = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
		return nullptr;
	}
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Loaded);
	if (!WBP)
	{
		OutError = FString::Printf(TEXT("Asset is not a UWidgetBlueprint: %s (is %s)"), *AssetPath, *Loaded->GetClass()->GetName());
		return nullptr;
	}
	return WBP;
}

// ============================================================================
// save_widget_blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleSaveWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	const bool bOnlyIfDirty = !Params->HasField(TEXT("only_if_dirty"))
		|| Params->GetBoolField(TEXT("only_if_dirty"));

	if (!FEpicUnrealMCPCommonUtils::AssetExists(AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset does not exist: %s"), *AssetPath));
	}

	const bool bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, bOnlyIfDirty);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bSaved);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return Result;
}

// ============================================================================
// delete_widget_blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleDeleteWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!FEpicUnrealMCPCommonUtils::AssetExists(AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset does not exist: %s"), *AssetPath));
	}

	// Class guard: refuse to delete non-widget blueprints with this command
	FString LoadError;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);
	}

	const bool bDeleted = UEditorAssetLibrary::DeleteAsset(AssetPath);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bDeleted);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetBoolField(TEXT("deleted"), bDeleted);
	if (!bDeleted)
	{
		Result->SetStringField(TEXT("error"), TEXT("DeleteAsset returned false; check output log for references or read-only state"));
	}
	return Result;
}

// ============================================================================
// rename_widget_blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleRenameWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (!Params->TryGetStringField(TEXT("source_path"), SourcePath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: source_path"));
	}
	FString DestPath;
	if (!Params->TryGetStringField(TEXT("destination_path"), DestPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: destination_path"));
	}
	if (!FEpicUnrealMCPCommonUtils::AssetExists(SourcePath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Source asset does not exist: %s"), *SourcePath));
	}
	if (FEpicUnrealMCPCommonUtils::AssetExists(DestPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Destination already exists: %s"), *DestPath));
	}

	FString LoadError;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(SourcePath, LoadError);
	if (!WBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);
	}

	const bool bRenamed = UEditorAssetLibrary::RenameAsset(SourcePath, DestPath);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bRenamed);
	Result->SetStringField(TEXT("source_path"), SourcePath);
	Result->SetStringField(TEXT("destination_path"), DestPath);
	Result->SetBoolField(TEXT("renamed"), bRenamed);
	return Result;
}

// ============================================================================
// duplicate_widget_blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleDuplicateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (!Params->TryGetStringField(TEXT("source_path"), SourcePath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: source_path"));
	}
	FString DestPath;
	if (!Params->TryGetStringField(TEXT("destination_path"), DestPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: destination_path"));
	}
	if (!FEpicUnrealMCPCommonUtils::AssetExists(SourcePath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Source asset does not exist: %s"), *SourcePath));
	}
	if (FEpicUnrealMCPCommonUtils::AssetExists(DestPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Destination already exists: %s"), *DestPath));
	}

	FString LoadError;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(SourcePath, LoadError);
	if (!WBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);
	}

	UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Duplicated != nullptr);
	Result->SetStringField(TEXT("source_path"), SourcePath);
	Result->SetStringField(TEXT("destination_path"), DestPath);
	Result->SetBoolField(TEXT("duplicated"), Duplicated != nullptr);
	if (Duplicated)
	{
		Result->SetStringField(TEXT("class_name"), Duplicated->GetClass()->GetName());
	}
	return Result;
}

// ============================================================================
// list_widget_blueprints
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleListWidgetBlueprints(const TSharedPtr<FJsonObject>& Params)
{
	const FString Directory = Params->HasField(TEXT("directory"))
		? Params->GetStringField(TEXT("directory"))
		: TEXT("/Game/");
	const int32 Limit = Params->HasField(TEXT("limit"))
		? static_cast<int32>(Params->GetNumberField(TEXT("limit")))
		: 200;
	const bool bRecursive = !Params->HasField(TEXT("recursive"))
		|| Params->GetBoolField(TEXT("recursive"));

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Directory));
	Filter.bRecursivePaths = bRecursive;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UWidgetBlueprint::StaticClass()->GetClassPathName());

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	const int32 TotalFound = AssetList.Num();
	const int32 ResultCount = FMath::Min(TotalFound, Limit);

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (int32 i = 0; i < ResultCount; ++i)
	{
		const FAssetData& Asset = AssetList[i];
		TSharedPtr<FJsonObject> AssetObj = MakeShareable(new FJsonObject);
		AssetObj->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("asset_path"), Asset.PackageName.ToString());
		AssetObj->SetStringField(TEXT("class_name"), Asset.AssetClassPath.ToString());

		FString ParentClass;
		if (Asset.GetTagValue(TEXT("ParentClass"), ParentClass))
		{
			AssetObj->SetStringField(TEXT("parent_class"), ParentClass);
		}
		AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetNumberField(TEXT("total_found"), TotalFound);
	Result->SetNumberField(TEXT("returned"), ResultCount);
	Result->SetArrayField(TEXT("widget_blueprints"), AssetsArray);
	return Result;
}

// ============================================================================
// move_widget — reparent a widget within its widget tree
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleMoveWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}
	FString NewParent;
	if (!Params->TryGetStringField(TEXT("new_parent_name"), NewParent))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: new_parent_name"));
	}

	FString LoadError;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);
	}
	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget blueprint has no WidgetTree"));
	}

	UWidget* Widget = Tree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));
	}
	UWidget* FoundParent = Tree->FindWidget(FName(*NewParent));
	UPanelWidget* NewParentPanel = Cast<UPanelWidget>(FoundParent);
	if (!NewParentPanel)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("New parent '%s' not found or is not a panel"), *NewParent));
	}
	if (Widget == NewParentPanel)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot reparent a widget under itself"));
	}
	if (Widget->GetParent() == NewParentPanel)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' is already a child of '%s'. Use reorder_widget_children to change its index within the same parent."),
			*WidgetName, *NewParent));
	}

	// Reject cycles: walk up new parent's chain to ensure widget isn't an ancestor
	{
		UWidget* Cursor = NewParentPanel;
		while (Cursor)
		{
			if (Cursor == Widget)
			{
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot reparent: would create a cycle"));
			}
			Cursor = Cursor->GetParent();
		}
	}

	UPanelWidget* OldParent = Widget->GetParent();
	if (OldParent)
	{
		OldParent->RemoveChild(Widget);
	}
	UPanelSlot* NewSlot = NewParentPanel->AddChild(Widget);
	if (!NewSlot)
	{
		if (OldParent) { OldParent->AddChild(Widget); }
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to add widget to new parent (panel may not accept additional children)"));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	PruneOrphanedWidgetVariableGuids(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("old_parent"), OldParent ? OldParent->GetName() : TEXT("(none)"));
	Result->SetStringField(TEXT("new_parent"), NewParentPanel->GetName());
	Result->SetStringField(TEXT("slot_type"), NewSlot->GetClass()->GetName());
	return Result;
}

// ============================================================================
// reorder_widget_children — change a child's index within its parent panel
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleReorderWidgetChildren(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}
	if (!Params->HasField(TEXT("new_index")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: new_index"));
	}
	const int32 NewIndex = static_cast<int32>(Params->GetNumberField(TEXT("new_index")));

	FString LoadError;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);
	}
	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget blueprint has no WidgetTree"));
	}

	UWidget* Widget = Tree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));
	}
	UPanelWidget* Parent = Widget->GetParent();
	if (!Parent)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget has no parent panel (cannot reorder root)"));
	}

	const int32 CurrentIndex = Parent->GetChildIndex(Widget);
	const int32 ChildCount = Parent->GetChildrenCount();
	const int32 ClampedNewIndex = FMath::Clamp(NewIndex, 0, ChildCount - 1);

	if (ClampedNewIndex != CurrentIndex)
	{
		Parent->ShiftChild(ClampedNewIndex, Widget);
		FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
		PruneOrphanedWidgetVariableGuids(WBP);
		FKismetEditorUtilities::CompileBlueprint(WBP);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("parent"), Parent->GetName());
	Result->SetNumberField(TEXT("old_index"), CurrentIndex);
	Result->SetNumberField(TEXT("new_index"), ClampedNewIndex);
	return Result;
}

// ============================================================================
// set_widget_slot_property — set slot props on an existing widget
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleSetWidgetSlotProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}

	FString LoadError;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);
	}
	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget blueprint has no WidgetTree"));
	}

	UWidget* Widget = Tree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));
	}

	const int32 SlotPropsApplied = ApplySlotProperties(Widget->Slot, Params);
	TArray<FString> SetFields;
	for (int32 i = 0; i < SlotPropsApplied; ++i) { SetFields.Add(TEXT("(slot)")); }

	// SizeBox-specific (widget-level, not slot, but commonly the goal when "fixing the SizeBox")
	if (USizeBox* SizeBox = Cast<USizeBox>(Widget))
	{
		if (Params->HasField(TEXT("width_override")))
		{
			SizeBox->SetWidthOverride(static_cast<float>(Params->GetNumberField(TEXT("width_override"))));
			SetFields.Add(TEXT("width_override"));
		}
		if (Params->HasField(TEXT("height_override")))
		{
			SizeBox->SetHeightOverride(static_cast<float>(Params->GetNumberField(TEXT("height_override"))));
			SetFields.Add(TEXT("height_override"));
		}
		if (Params->HasField(TEXT("min_desired_width")))
		{
			SizeBox->SetMinDesiredWidth(static_cast<float>(Params->GetNumberField(TEXT("min_desired_width"))));
			SetFields.Add(TEXT("min_desired_width"));
		}
		if (Params->HasField(TEXT("min_desired_height")))
		{
			SizeBox->SetMinDesiredHeight(static_cast<float>(Params->GetNumberField(TEXT("min_desired_height"))));
			SetFields.Add(TEXT("min_desired_height"));
		}
		if (Params->HasField(TEXT("max_desired_width")))
		{
			SizeBox->SetMaxDesiredWidth(static_cast<float>(Params->GetNumberField(TEXT("max_desired_width"))));
			SetFields.Add(TEXT("max_desired_width"));
		}
		if (Params->HasField(TEXT("max_desired_height")))
		{
			SizeBox->SetMaxDesiredHeight(static_cast<float>(Params->GetNumberField(TEXT("max_desired_height"))));
			SetFields.Add(TEXT("max_desired_height"));
		}
		if (Params->HasField(TEXT("max_aspect_ratio")))
		{
			SizeBox->SetMaxAspectRatio(static_cast<float>(Params->GetNumberField(TEXT("max_aspect_ratio"))));
			SetFields.Add(TEXT("max_aspect_ratio"));
		}
	}

	if (SetFields.Num() > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
		PruneOrphanedWidgetVariableGuids(WBP);
		FKismetEditorUtilities::CompileBlueprint(WBP);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("slot_type"), Widget->Slot ? Widget->Slot->GetClass()->GetName() : TEXT("none"));
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
// set_widget_root — Replace the WidgetTree's root widget with an existing one
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleSetWidgetRoot(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}

	FString LoadError;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);
	}
	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget blueprint has no WidgetTree"));
	}

	UWidget* NewRoot = Tree->FindWidget(FName(*WidgetName));
	if (!NewRoot)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));
	}
	if (NewRoot == Tree->RootWidget)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("widget_name"), WidgetName);
		Result->SetBoolField(TEXT("changed"), false);
		Result->SetStringField(TEXT("message"), TEXT("Widget is already the root"));
		return Result;
	}

	if (UPanelWidget* OldParent = NewRoot->GetParent())
	{
		OldParent->RemoveChild(NewRoot);
	}

	const bool bReparentOldRoot = Params->HasField(TEXT("reparent_old_root"))
		&& Params->GetBoolField(TEXT("reparent_old_root"));

	UWidget* OldRoot = Tree->RootWidget;
	const FString OldRootName = OldRoot ? OldRoot->GetName() : TEXT("(none)");

	Tree->RootWidget = NewRoot;

	bool bReparented = false;
	if (bReparentOldRoot && OldRoot)
	{
		if (UPanelWidget* NewRootPanel = Cast<UPanelWidget>(NewRoot))
		{
			if (NewRootPanel->AddChild(OldRoot))
			{
				bReparented = true;
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	PruneOrphanedWidgetVariableGuids(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("old_root"), OldRootName);
	Result->SetBoolField(TEXT("old_root_reparented"), bReparented);
	Result->SetBoolField(TEXT("changed"), true);
	if (bReparentOldRoot && !bReparented && OldRoot)
	{
		Result->SetStringField(TEXT("warning"), TEXT("reparent_old_root requested but new root is not a panel widget; old root will be GC'd"));
	}
	return Result;
}

// ============================================================================
// compile_widget_blueprint — Recompile a Widget Blueprint without other edits
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleCompileWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString LoadError;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(LoadError);
	}

	PruneOrphanedWidgetVariableGuids(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	return Result;
}

// ============================================================================
// list_required_bindwidgets — Walk parent C++ class for BindWidget meta and
// report whether the WBP currently provides each binding. Catches name/class
// mismatches before WBP compile fails on missing required binds.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleListRequiredBindWidgets(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}

	UClass* ParentClass = WidgetBP->ParentClass;
	if (!ParentClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no parent class"));
	}

	UWidgetTree* Tree = WidgetBP->WidgetTree;

	TArray<TSharedPtr<FJsonValue>> Bindings;
	int32 RequiredCount = 0;
	int32 OptionalCount = 0;
	int32 ProvidedCount = 0;
	int32 MissingRequiredCount = 0;
	int32 ClassMismatchCount = 0;

	for (TFieldIterator<FObjectProperty> PropIt(ParentClass); PropIt; ++PropIt)
	{
		FObjectProperty* Prop = *PropIt;
		if (!Prop || !Prop->PropertyClass)
		{
			continue;
		}
		if (!Prop->PropertyClass->IsChildOf(UWidget::StaticClass()))
		{
			continue;
		}

		const bool bRequired = Prop->HasMetaData(TEXT("BindWidget"));
		const bool bOptional = Prop->HasMetaData(TEXT("BindWidgetOptional")) || Prop->HasMetaData(TEXT("OptionalWidget"));
		if (!bRequired && !bOptional)
		{
			continue;
		}

		const FString PropName = Prop->GetName();
		UWidget* Found = Tree ? Tree->FindWidget(FName(*PropName)) : nullptr;
		const bool bProvided = Found != nullptr;
		const bool bClassMatches = bProvided && Found->GetClass()->IsChildOf(Prop->PropertyClass);

		if (bRequired) ++RequiredCount; else ++OptionalCount;
		if (bProvided) ++ProvidedCount;
		if (bRequired && !bProvided) ++MissingRequiredCount;
		if (bProvided && !bClassMatches) ++ClassMismatchCount;

		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("name"), PropName);
		Entry->SetStringField(TEXT("expected_class"), Prop->PropertyClass->GetName());
		Entry->SetBoolField(TEXT("required"), bRequired);
		Entry->SetBoolField(TEXT("optional"), bOptional);
		Entry->SetBoolField(TEXT("provided"), bProvided);
		if (bProvided)
		{
			Entry->SetStringField(TEXT("provided_class"), Found->GetClass()->GetName());
			Entry->SetBoolField(TEXT("class_matches"), bClassMatches);
		}
		Bindings.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	Result->SetNumberField(TEXT("required_count"), RequiredCount);
	Result->SetNumberField(TEXT("optional_count"), OptionalCount);
	Result->SetNumberField(TEXT("provided_count"), ProvidedCount);
	Result->SetNumberField(TEXT("missing_required_count"), MissingRequiredCount);
	Result->SetNumberField(TEXT("class_mismatch_count"), ClassMismatchCount);
	Result->SetArrayField(TEXT("bindings"), Bindings);
	return Result;
}

// ============================================================================
// set_widget_class_property — Set a TSubclassOf<>/TSoftClassPtr<> UPROPERTY on
// a widget by name. Covers UListViewBase::EntryWidgetClass, button/text Style
// pointers, and any other class-typed property without a dedicated tool.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleSetWidgetClassProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}
	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: property_name"));
	}
	FString ClassPath;
	const bool bHasClassPath = Params->TryGetStringField(TEXT("class_path"), ClassPath);

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}

	UWidget* Widget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found in %s"), *WidgetName, *AssetPath));
	}

	FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Property '%s' not found on widget class '%s'"),
			*PropertyName, *Widget->GetClass()->GetName()));
	}

	FClassProperty* HardClassProp = CastField<FClassProperty>(Prop);
	FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop);
	if (!HardClassProp && !SoftClassProp)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Property '%s' is not a class property (TSubclassOf<>/TSoftClassPtr<>); use set_widget_property for primitive types"),
			*PropertyName));
	}

	UClass* MetaClass = HardClassProp ? HardClassProp->MetaClass : SoftClassProp->MetaClass;
	if (!MetaClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Property '%s' has no meta class"), *PropertyName));
	}

	// Resolve target class. Empty / missing class_path clears the property.
	UClass* TargetClass = nullptr;
	if (bHasClassPath && !ClassPath.IsEmpty())
	{
		FString Resolved = ClassPath;
		if (!Resolved.EndsWith(TEXT("_C")))
		{
			// Caller likely passed the WBP path (/Game/.../WBP_Foo or
			// /Game/.../WBP_Foo.WBP_Foo); load and grab generated class.
			UObject* LoadedBP = UEditorAssetLibrary::LoadAsset(Resolved);
			if (UWidgetBlueprint* CustomWBP = Cast<UWidgetBlueprint>(LoadedBP))
			{
				TargetClass = CustomWBP->GeneratedClass;
			}
			if (UBlueprint* AsBP = Cast<UBlueprint>(LoadedBP))
			{
				if (!TargetClass)
				{
					TargetClass = AsBP->GeneratedClass;
				}
			}
		}
		if (!TargetClass)
		{
			TargetClass = LoadClass<UObject>(nullptr, *Resolved);
		}
		if (!TargetClass)
		{
			TargetClass = LoadObject<UClass>(nullptr, *Resolved);
		}
		if (!TargetClass)
		{
			TargetClass = FindFirstObject<UClass>(*Resolved, EFindFirstObjectOptions::EnsureIfAmbiguous);
			if (!TargetClass)
			{
				TargetClass = FindFirstObject<UClass>(*(TEXT("U") + Resolved), EFindFirstObjectOptions::EnsureIfAmbiguous);
			}
		}
		if (!TargetClass)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Could not resolve class_path '%s'. Pass a WBP asset path (/Game/...), an explicit _C path, or a C++ class name."),
				*ClassPath));
		}
		if (!TargetClass->IsChildOf(MetaClass))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Resolved class '%s' is not a child of required meta class '%s' for property '%s'"),
				*TargetClass->GetName(), *MetaClass->GetName(), *PropertyName));
		}
	}

	Widget->Modify();
	WidgetBP->Modify();

	if (HardClassProp)
	{
		void* ValuePtr = HardClassProp->ContainerPtrToValuePtr<void>(Widget);
		HardClassProp->SetObjectPropertyValue(ValuePtr, TargetClass);
	}
	else
	{
		void* ValuePtr = SoftClassProp->ContainerPtrToValuePtr<void>(Widget);
		FSoftObjectPtr SoftPtr(TargetClass);
		SoftClassProp->SetPropertyValue(ValuePtr, SoftPtr);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	PruneOrphanedWidgetVariableGuids(WidgetBP);
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("property_type"), HardClassProp ? TEXT("ClassProperty") : TEXT("SoftClassProperty"));
	Result->SetStringField(TEXT("meta_class"), MetaClass->GetName());
	if (TargetClass)
	{
		Result->SetStringField(TEXT("set_class"), TargetClass->GetPathName());
	}
	else
	{
		Result->SetBoolField(TEXT("cleared"), true);
	}
	return Result;
}

// ============================================================================
// set_widget_object_property — Set a UObject reference UPROPERTY (TObjectPtr<>,
// raw UObject*, TSoftObjectPtr<>) on a widget. Covers CommonUI Style refs
// (UCommonButtonBase::Style → UCommonButtonStyle, UCommonTextBlock::Style →
// UCommonTextStyle, UCommonBorder::Style → UCommonBorderStyle) and any other
// UObject-typed property — e.g., UCommonButtonBase::TriggeringInputAction.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleSetWidgetObjectProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}
	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: property_name"));
	}
	FString ObjectPath;
	const bool bHasObjectPath = Params->TryGetStringField(TEXT("object_path"), ObjectPath);

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}

	UWidget* Widget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found in %s"), *WidgetName, *AssetPath));
	}

	FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Property '%s' not found on widget class '%s'"),
			*PropertyName, *Widget->GetClass()->GetName()));
	}

	// Reject class properties — caller should use set_widget_class_property.
	if (CastField<FClassProperty>(Prop) || CastField<FSoftClassProperty>(Prop))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Property '%s' is a class property; use set_widget_class_property instead"),
			*PropertyName));
	}

	FObjectProperty* HardObjProp = CastField<FObjectProperty>(Prop);
	FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop);
	if (!HardObjProp && !SoftObjProp)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Property '%s' is not a UObject reference (TObjectPtr<>/TSoftObjectPtr<>); use set_widget_property for primitives or set_widget_class_property for class refs"),
			*PropertyName));
	}

	UClass* ExpectedClass = HardObjProp ? HardObjProp->PropertyClass : SoftObjProp->PropertyClass;
	if (!ExpectedClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Property '%s' has no expected class"), *PropertyName));
	}

	UObject* TargetObject = nullptr;
	if (bHasObjectPath && !ObjectPath.IsEmpty())
	{
		// Try as a content asset first (handles "/Game/.../MyStyle" or "/Game/.../MyStyle.MyStyle")
		TargetObject = UEditorAssetLibrary::LoadAsset(ObjectPath);
		if (!TargetObject)
		{
			TargetObject = LoadObject<UObject>(nullptr, *ObjectPath);
		}
		if (!TargetObject)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Could not resolve object_path '%s'"), *ObjectPath));
		}
		if (!TargetObject->IsA(ExpectedClass))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Resolved object '%s' (class %s) is not a '%s' for property '%s'"),
				*TargetObject->GetPathName(), *TargetObject->GetClass()->GetName(),
				*ExpectedClass->GetName(), *PropertyName));
		}
	}

	Widget->Modify();
	WidgetBP->Modify();

	if (HardObjProp)
	{
		void* ValuePtr = HardObjProp->ContainerPtrToValuePtr<void>(Widget);
		HardObjProp->SetObjectPropertyValue(ValuePtr, TargetObject);
	}
	else
	{
		void* ValuePtr = SoftObjProp->ContainerPtrToValuePtr<void>(Widget);
		FSoftObjectPtr SoftPtr(TargetObject);
		SoftObjProp->SetPropertyValue(ValuePtr, SoftPtr);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	PruneOrphanedWidgetVariableGuids(WidgetBP);
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("property_type"), HardObjProp ? TEXT("ObjectProperty") : TEXT("SoftObjectProperty"));
	Result->SetStringField(TEXT("expected_class"), ExpectedClass->GetName());
	if (TargetObject)
	{
		Result->SetStringField(TEXT("set_object"), TargetObject->GetPathName());
	}
	else
	{
		Result->SetBoolField(TEXT("cleared"), true);
	}
	return Result;
}

// ============================================================================
// create_common_style_asset — Create a UCommonButtonStyle / UCommonTextStyle /
// UCommonBorderStyle as a standalone Content Browser asset. CommonUI styles
// have no default factory, so this is the only way to get them in-editor
// without hand-rolling a UFactory subclass.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleCreateCommonStyleAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString StyleType;
	if (!Params->TryGetStringField(TEXT("style_type"), StyleType))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: style_type (button|text|border, or a class name)"));
	}
	FString AssetName;
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_name"));
	}
	FString DestinationPath;
	if (!Params->TryGetStringField(TEXT("destination_path"), DestinationPath))
	{
		DestinationPath = TEXT("/Game/UI/Styles/");
	}
	if (!DestinationPath.EndsWith(TEXT("/")))
	{
		DestinationPath += TEXT("/");
	}

	UClass* StyleClass = nullptr;
	const FString StyleTypeLower = StyleType.ToLower();
	if (StyleTypeLower == TEXT("button"))
	{
		StyleClass = UCommonButtonStyle::StaticClass();
	}
	else if (StyleTypeLower == TEXT("text"))
	{
		StyleClass = UCommonTextStyle::StaticClass();
	}
	else if (StyleTypeLower == TEXT("border"))
	{
		StyleClass = UCommonBorderStyle::StaticClass();
	}
	else
	{
		// Allow any explicit class name as long as it's a CommonUI style descendant.
		StyleClass = FEpicUnrealMCPCommonUtils::ResolveClass(StyleType);
		if (!StyleClass)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Unknown style_type '%s'. Use 'button', 'text', 'border', or a CommonUI style class name."), *StyleType));
		}
		const bool bIsCommonStyle =
			StyleClass->IsChildOf(UCommonButtonStyle::StaticClass()) ||
			StyleClass->IsChildOf(UCommonTextStyle::StaticClass()) ||
			StyleClass->IsChildOf(UCommonBorderStyle::StaticClass());
		if (!bIsCommonStyle)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Class '%s' is not a CommonUI style class (UCommonButtonStyle/UCommonTextStyle/UCommonBorderStyle)"), *StyleClass->GetName()));
		}
	}

	if (StyleClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Style class '%s' is abstract"), *StyleClass->GetName()));
	}

	const FString FullPath = DestinationPath + AssetName;
	if (FEpicUnrealMCPCommonUtils::AssetExists(FullPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset already exists: %s"), *FullPath));
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	UObject* NewStyle = NewObject<UObject>(Package, StyleClass, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!NewStyle)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to construct '%s'"), *StyleClass->GetName()));
	}

	FAssetRegistryModule::AssetCreated(NewStyle);
	Package->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(NewStyle->GetPathName(), /*bOnlyIfIsDirty*/ true);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), NewStyle->GetPathName());
	Result->SetStringField(TEXT("class"), StyleClass->GetName());
	return Result;
}

// ============================================================================
// set_named_slot_content — Fill a UNamedSlot exposed by an embedded UUserWidget
// (or any INamedSlotInterface implementer) with content already in the outer
// WBP's tree. Construct the content via add_widget_child first, then call this
// to wire it into the host's named slot.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleSetNamedSlotContent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	FString HostWidgetName;
	if (!Params->TryGetStringField(TEXT("host_widget_name"), HostWidgetName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: host_widget_name (the embedded UserWidget that exposes the named slot)"));
	}
	FString SlotName;
	if (!Params->TryGetStringField(TEXT("slot_name"), SlotName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: slot_name"));
	}
	FString ContentWidgetName;
	const bool bHasContent = Params->TryGetStringField(TEXT("content_widget_name"), ContentWidgetName);

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}

	UWidget* Host = WidgetBP->WidgetTree->FindWidget(FName(*HostWidgetName));
	if (!Host)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Host widget '%s' not found in %s"), *HostWidgetName, *AssetPath));
	}

	INamedSlotInterface* SlotHost = Cast<INamedSlotInterface>(Host);
	if (!SlotHost)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' (class %s) does not implement INamedSlotInterface — only UUserWidget-derived widgets and UNamedSlot can host named slots"),
			*HostWidgetName, *Host->GetClass()->GetName()));
	}

	TArray<FName> AvailableSlots;
	SlotHost->GetSlotNames(AvailableSlots);
	if (!AvailableSlots.Contains(FName(*SlotName)))
	{
		FString Available;
		for (const FName& S : AvailableSlots)
		{
			if (!Available.IsEmpty()) Available += TEXT(", ");
			Available += S.ToString();
		}
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Host '%s' does not expose slot '%s'. Available: [%s]"),
			*HostWidgetName, *SlotName, *Available));
	}

	UWidget* Content = nullptr;
	if (bHasContent && !ContentWidgetName.IsEmpty())
	{
		Content = WidgetBP->WidgetTree->FindWidget(FName(*ContentWidgetName));
		if (!Content)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Content widget '%s' not found in %s. Construct it via add_widget_child first."),
				*ContentWidgetName, *AssetPath));
		}
	}

	Host->Modify();
	WidgetBP->Modify();
	SlotHost->SetContentForSlot(FName(*SlotName), Content);

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	PruneOrphanedWidgetVariableGuids(WidgetBP);
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("host_widget_name"), HostWidgetName);
	Result->SetStringField(TEXT("slot_name"), SlotName);
	if (Content)
	{
		Result->SetStringField(TEXT("content_widget_name"), ContentWidgetName);
	}
	else
	{
		Result->SetBoolField(TEXT("cleared"), true);
	}
	return Result;
}

// ============================================================================
// Helper: resolve widget class from a "type" string, mirroring
// HandleAddWidgetChild's lookup so wrap_widget / replace_widget_class accept
// the same vocabulary (built-in name | C++ class | WBP asset path).
// Sets OutError on failure; OutClass is null when failed.
// ============================================================================

static UClass* ResolveWidgetClassFromTypeString(const FString& TypeString, FString& OutError)
{
	OutError.Empty();

	// Built-in shorthand map — maintained in HandleAddWidgetChild. We cannot
	// reach the static-local map from here cleanly, so this re-tries the
	// extended fallbacks (asset path / FindFirstObject) which catch every name
	// the dispatch map covers.
	UClass* Resolved = nullptr;

	const bool bLooksLikeContentPath = TypeString.StartsWith(TEXT("/"));
	if (bLooksLikeContentPath && !TypeString.EndsWith(TEXT("_C")))
	{
		UObject* LoadedBP = UEditorAssetLibrary::LoadAsset(TypeString);
		if (UWidgetBlueprint* CustomWBP = Cast<UWidgetBlueprint>(LoadedBP))
		{
			Resolved = CustomWBP->GeneratedClass;
		}
	}
	if (!Resolved)
	{
		Resolved = FindFirstObject<UClass>(*TypeString, EFindFirstObjectOptions::EnsureIfAmbiguous);
		if (!Resolved)
		{
			Resolved = FindFirstObject<UClass>(*(TEXT("U") + TypeString), EFindFirstObjectOptions::EnsureIfAmbiguous);
		}
	}

	if (!Resolved || !Resolved->IsChildOf(UWidget::StaticClass()))
	{
		OutError = FString::Printf(
			TEXT("Unknown widget type: %s. Use a built-in name (TextBlock, Button, Border, ListView, ...), a C++ class, or a Widget Blueprint asset path (/Game/...)"),
			*TypeString);
		return nullptr;
	}
	if (Resolved->HasAnyClassFlags(CLASS_Abstract))
	{
		OutError = FString::Printf(
			TEXT("Widget class '%s' is abstract; pass a concrete subclass or a WBP asset path."),
			*Resolved->GetName());
		return nullptr;
	}
	return Resolved;
}

// ============================================================================
// Helper: copy slot-property values between two slots of the *same* class.
// Property-by-property reflection copy. Caller verifies slot class equality.
// ============================================================================

static int32 CopyPanelSlotProperties(UPanelSlot* Source, UPanelSlot* Dest)
{
	if (!Source || !Dest || Source->GetClass() != Dest->GetClass()) return 0;
	int32 Copied = 0;
	for (FProperty* Prop = Source->GetClass()->PropertyLink; Prop; Prop = Prop->PropertyLinkNext)
	{
		// Skip transient / non-editable infrastructure (Parent, Content backref, etc.).
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;
		if (Prop->GetFName() == TEXT("Parent") || Prop->GetFName() == TEXT("Content")) continue;
		Prop->CopyCompleteValue_InContainer(Dest, Source);
		++Copied;
	}
	return Copied;
}

// ============================================================================
// wrap_widget — Wrap an existing widget in a new container (Border/SizeBox/
// Overlay/etc). The wrapper takes the original widget's slot in the parent;
// the original becomes the wrapper's child. Slot props on the parent side
// are copied so the visual layout is preserved when wrapping a CanvasPanel
// child (position/size/anchors). For HBox/VBox children only the slot index
// is preserved — set new wrapper-slot props via set_widget_slot_property if
// the wrapper itself needs alignment/padding inside the parent.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleWrapWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, TargetName, WrapperType, WrapperName;
	if (!Params->TryGetStringField(TEXT("asset_path"),    AssetPath))   return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	if (!Params->TryGetStringField(TEXT("widget_name"),   TargetName))  return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	if (!Params->TryGetStringField(TEXT("wrapper_type"),  WrapperType)) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: wrapper_type"));
	if (!Params->TryGetStringField(TEXT("wrapper_name"),  WrapperName)) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: wrapper_name"));

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WBP || !WBP->WidgetTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}
	UWidgetTree* Tree = WBP->WidgetTree;

	UWidget* Target = Tree->FindWidget(FName(*TargetName));
	if (!Target) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *TargetName));
	if (Tree->FindWidget(FName(*WrapperName)))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Wrapper name '%s' already in use"), *WrapperName));
	}

	FString Err;
	UClass* WrapperClass = ResolveWidgetClassFromTypeString(WrapperType, Err);
	if (!WrapperClass) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);
	if (!WrapperClass->IsChildOf(UPanelWidget::StaticClass()))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Wrapper class '%s' is not a UPanelWidget — cannot host children."),
			*WrapperClass->GetName()));
	}

	UPanelWidget* OldParent = Target->GetParent();
	const int32 OldIndex = OldParent ? OldParent->GetChildIndex(Target) : INDEX_NONE;
	UPanelSlot* OldSlot = Target->Slot;

	UPanelWidget* Wrapper = Cast<UPanelWidget>(Tree->ConstructWidget<UWidget>(WrapperClass, FName(*WrapperName)));
	if (!Wrapper) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to construct wrapper widget"));

	// Detach target from its current parent (or root) before re-parenting.
	const bool bWasRoot = (Tree->RootWidget == Target);
	if (OldParent)
	{
		OldParent->RemoveChildAt(OldIndex);
	}
	else if (bWasRoot)
	{
		Tree->RootWidget = nullptr;
	}

	// Push wrapper into the slot the target previously occupied.
	UPanelSlot* WrapperSlotInParent = nullptr;
	if (OldParent)
	{
		WrapperSlotInParent = OldParent->InsertChildAt(OldIndex, Wrapper);
		if (WrapperSlotInParent && OldSlot && WrapperSlotInParent->GetClass() == OldSlot->GetClass())
		{
			CopyPanelSlotProperties(OldSlot, WrapperSlotInParent);
		}
	}
	else if (bWasRoot)
	{
		Tree->RootWidget = Wrapper;
	}

	// Place target inside wrapper. Reuse ApplySlotProperties for any
	// wrapper-internal slot props the caller passes (padding, h_align...).
	UPanelSlot* TargetSlotInWrapper = Wrapper->AddChild(Target);
	ApplySlotProperties(TargetSlotInWrapper, Params);

	WBP->Modify();
	FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	PruneOrphanedWidgetVariableGuids(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), TargetName);
	Result->SetStringField(TEXT("wrapper_name"), WrapperName);
	Result->SetStringField(TEXT("wrapper_class"), WrapperClass->GetName());
	Result->SetStringField(TEXT("former_parent"), OldParent ? OldParent->GetName() : (bWasRoot ? TEXT("(root)") : TEXT("(orphan)")));
	Result->SetNumberField(TEXT("former_index"), OldIndex);
	return Result;
}

// ============================================================================
// set_widget_is_variable — toggle Widget->bIsVariable so the widget surfaces
// as a Blueprint variable accessible from the event graph. WBPs created via
// add_widget_child default to bIsVariable=false; without this flip they
// can't be referenced from BP code.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleSetWidgetIsVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, WidgetName;
	if (!Params->TryGetStringField(TEXT("asset_path"),  AssetPath))  return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName)) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	if (!Params->HasField(TEXT("is_variable")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: is_variable (bool)"));
	}
	const bool bIsVariable = Params->GetBoolField(TEXT("is_variable"));

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WBP || !WBP->WidgetTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}

	UWidget* Widget = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));

	const bool bBefore = Widget->bIsVariable;
	if (bBefore != bIsVariable)
	{
		Widget->Modify();
		Widget->bIsVariable = bIsVariable;
		WBP->Modify();
		FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
		PruneOrphanedWidgetVariableGuids(WBP);
		FKismetEditorUtilities::CompileBlueprint(WBP);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetBoolField(TEXT("is_variable"), bIsVariable);
	Result->SetBoolField(TEXT("changed"), bBefore != bIsVariable);
	return Result;
}

// ============================================================================
// replace_widget_class — Swap a widget's class while preserving its name,
// position in the parent panel, and (when target was a panel) its children.
// Slot props are copied when the new slot type matches the old one; otherwise
// the new widget gets the parent's default slot and the caller can re-set
// via set_widget_slot_property.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleReplaceWidgetClass(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, WidgetName, NewType;
	if (!Params->TryGetStringField(TEXT("asset_path"),       AssetPath))  return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	if (!Params->TryGetStringField(TEXT("widget_name"),      WidgetName)) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	if (!Params->TryGetStringField(TEXT("new_widget_type"),  NewType))    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: new_widget_type"));

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WBP || !WBP->WidgetTree)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}
	UWidgetTree* Tree = WBP->WidgetTree;

	UWidget* OldWidget = Tree->FindWidget(FName(*WidgetName));
	if (!OldWidget) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));

	FString Err;
	UClass* NewClass = ResolveWidgetClassFromTypeString(NewType, Err);
	if (!NewClass) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);
	if (NewClass == OldWidget->GetClass())
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("widget_name"), WidgetName);
		Result->SetBoolField(TEXT("changed"), false);
		Result->SetStringField(TEXT("note"), TEXT("New class is the same as the old class — no-op."));
		return Result;
	}

	UPanelWidget* OldParent = OldWidget->GetParent();
	const int32 OldIndex = OldParent ? OldParent->GetChildIndex(OldWidget) : INDEX_NONE;
	UPanelSlot* OldSlot = OldWidget->Slot;
	const bool bWasRoot = (Tree->RootWidget == OldWidget);

	// Capture children if both old and new are panels — we'll re-parent them
	// after the swap. Capture by iterating before we mutate the tree.
	UPanelWidget* OldAsPanel = Cast<UPanelWidget>(OldWidget);
	const bool bNewWillBePanel = NewClass->IsChildOf(UPanelWidget::StaticClass());
	TArray<UWidget*> ChildrenToMove;
	if (OldAsPanel && bNewWillBePanel)
	{
		const int32 ChildCount = OldAsPanel->GetChildrenCount();
		ChildrenToMove.Reserve(ChildCount);
		for (int32 i = 0; i < ChildCount; ++i)
		{
			if (UWidget* C = OldAsPanel->GetChildAt(i))
			{
				ChildrenToMove.Add(C);
			}
		}
	}

	// Free the name slot for the new widget by renaming the old one out of the way.
	const FName OldFName = OldWidget->GetFName();
	const FString TempName = FString::Printf(TEXT("__pendingreplace_%s"), *OldFName.ToString());
	OldWidget->Rename(*TempName, nullptr, REN_DontCreateRedirectors | REN_NonTransactional);

	UWidget* NewWidget = Tree->ConstructWidget<UWidget>(NewClass, OldFName);
	if (!NewWidget)
	{
		// Restore old name and bail.
		OldWidget->Rename(*OldFName.ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to construct replacement widget"));
	}
	NewWidget->bIsVariable = OldWidget->bIsVariable;

	// Detach old from parent / root.
	if (OldParent)
	{
		OldParent->RemoveChildAt(OldIndex);
	}
	else if (bWasRoot)
	{
		Tree->RootWidget = nullptr;
	}

	// Migrate children if both sides are panels.
	UPanelWidget* NewAsPanel = Cast<UPanelWidget>(NewWidget);
	int32 MovedChildren = 0;
	int32 DroppedChildren = 0;
	if (NewAsPanel && ChildrenToMove.Num() > 0)
	{
		for (UWidget* Child : ChildrenToMove)
		{
			if (!NewAsPanel->CanHaveMultipleChildren() && NewAsPanel->GetChildrenCount() > 0)
			{
				++DroppedChildren;
				continue;
			}
			Child->RemoveFromParent();
			if (NewAsPanel->AddChild(Child))
			{
				++MovedChildren;
			}
			else
			{
				++DroppedChildren;
			}
		}
	}
	else if (!NewAsPanel)
	{
		DroppedChildren = ChildrenToMove.Num();
	}

	// Re-attach new widget at the old slot index.
	UPanelSlot* NewSlot = nullptr;
	if (OldParent)
	{
		NewSlot = OldParent->InsertChildAt(OldIndex, NewWidget);
		if (NewSlot && OldSlot && NewSlot->GetClass() == OldSlot->GetClass())
		{
			CopyPanelSlotProperties(OldSlot, NewSlot);
		}
	}
	else if (bWasRoot)
	{
		Tree->RootWidget = NewWidget;
	}

	// Drop the old widget entirely. RemoveWidget takes care of unparenting any
	// dropped children that are still outered to the tree (we already detached
	// them via RemoveFromParent above).
	Tree->RemoveWidget(OldWidget);

	WBP->Modify();
	FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	PruneOrphanedWidgetVariableGuids(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("new_class"), NewClass->GetName());
	Result->SetNumberField(TEXT("children_moved"), MovedChildren);
	Result->SetNumberField(TEXT("children_dropped"), DroppedChildren);
	Result->SetBoolField(TEXT("slot_props_preserved"), (NewSlot && OldSlot && NewSlot->GetClass() == OldSlot->GetClass()));
	return Result;
}
