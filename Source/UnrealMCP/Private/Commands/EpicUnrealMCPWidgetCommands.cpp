#include "Commands/EpicUnrealMCPWidgetCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPPropertyUtils.h"

#include "EditorAssetLibrary.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/WrapBox.h"
#include "Components/GridPanel.h"
#include "Components/UniformGridPanel.h"
#include "Components/Spacer.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/EditableTextBox.h"
#include "Components/RichTextBlock.h"
#include "Components/WidgetSwitcher.h"

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
	else if (CommandType == TEXT("set_widget_properties"))
	{
		return HandleSetWidgetProperties(Params);
	}
	else if (CommandType == TEXT("remove_widget"))
	{
		return HandleRemoveWidget(Params);
	}
	else if (CommandType == TEXT("replace_widget_root"))
	{
		return HandleReplaceWidgetRoot(Params);
	}
	else if (CommandType == TEXT("compile_widget_blueprint"))
	{
		return HandleCompileWidgetBlueprint(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown Widget command: %s"), *CommandType));
}

// ============================================================================
// Shared Helpers
// ============================================================================

UWidgetBlueprint* FEpicUnrealMCPWidgetCommands::LoadWidgetBlueprint(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutError)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
		return nullptr;
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
		return nullptr;
	}

	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBlueprint)
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UWidgetBlueprint: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
		return nullptr;
	}

	if (!WidgetBlueprint->WidgetTree)
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget blueprint has no WidgetTree"));
		return nullptr;
	}

	return WidgetBlueprint;
}

UClass* FEpicUnrealMCPWidgetCommands::ResolveWidgetClass(const FString& ClassName)
{
	// If it looks like a full path (contains /), try direct load
	if (ClassName.Contains(TEXT("/")))
	{
		UClass* FoundClass = FindObject<UClass>(nullptr, *ClassName);
		if (!FoundClass)
		{
			FoundClass = LoadObject<UClass>(nullptr, *ClassName);
		}
		return FoundClass;
	}

	// Try exact name
	UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);

	// Try with U prefix
	if (!FoundClass && !ClassName.StartsWith(TEXT("U")))
	{
		FString WithPrefix = TEXT("U") + ClassName;
		FoundClass = FindFirstObject<UClass>(*WithPrefix, EFindFirstObjectOptions::ExactClass);
	}

	// Try /Script/UMG.ClassName
	if (!FoundClass)
	{
		FString ScriptPath = FString::Printf(TEXT("/Script/UMG.%s"), *ClassName);
		FoundClass = FindObject<UClass>(nullptr, *ScriptPath);
		if (!FoundClass)
		{
			FoundClass = LoadObject<UClass>(nullptr, *ScriptPath);
		}
	}

	// Try /Script/UMG.UClassName
	if (!FoundClass && !ClassName.StartsWith(TEXT("U")))
	{
		FString ScriptPath = FString::Printf(TEXT("/Script/UMG.U%s"), *ClassName);
		FoundClass = FindObject<UClass>(nullptr, *ScriptPath);
		if (!FoundClass)
		{
			FoundClass = LoadObject<UClass>(nullptr, *ScriptPath);
		}
	}

	return FoundClass;
}

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

static FString SizeRuleToString(ESlateSizeRule::Type Rule)
{
	switch (Rule)
	{
	case ESlateSizeRule::Automatic:	return TEXT("Automatic");
	case ESlateSizeRule::Fill:	return TEXT("Fill");
	default:			return TEXT("Unknown");
	}
}

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
	TSharedPtr<FJsonObject> Error;
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(Params, Error);
	if (!WidgetBlueprint)
	{
		return Error;
	}

	if (!Params->HasField(TEXT("widget_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}

	const FString WidgetName = Params->GetStringField(TEXT("widget_name"));
	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;

	UWidget* FoundWidget = WidgetTree->FindWidget(FName(*WidgetName));
	if (!FoundWidget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
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
// create_widget_blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleCreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	// Check if asset already exists
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset already exists: %s"), *AssetPath));
	}

	// Resolve parent class (default: UUserWidget)
	UClass* ParentClass = UUserWidget::StaticClass();
	if (Params->HasField(TEXT("parent_class")))
	{
		const FString ParentClassName = Params->GetStringField(TEXT("parent_class"));
		if (!ParentClassName.IsEmpty() && ParentClassName != TEXT("UserWidget"))
		{
			UClass* ResolvedParent = ResolveWidgetClass(ParentClassName);
			if (!ResolvedParent || !ResolvedParent->IsChildOf(UUserWidget::StaticClass()))
			{
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent class '%s' is not a UUserWidget subclass"), *ParentClassName));
			}
			ParentClass = ResolvedParent;
		}
	}

	// Resolve root widget class (default: UCanvasPanel)
	UClass* RootWidgetClass = UCanvasPanel::StaticClass();
	if (Params->HasField(TEXT("root_widget_class")))
	{
		const FString RootClassName = Params->GetStringField(TEXT("root_widget_class"));
		if (!RootClassName.IsEmpty())
		{
			UClass* ResolvedRoot = ResolveWidgetClass(RootClassName);
			if (!ResolvedRoot || !ResolvedRoot->IsChildOf(UPanelWidget::StaticClass()))
			{
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Root widget class '%s' is not a UPanelWidget subclass"), *RootClassName));
			}
			RootWidgetClass = ResolvedRoot;
		}
	}

	// Split asset_path into package path and asset name
	FString PackagePath, AssetName;
	AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid asset_path format: %s"), *AssetPath));
	}

	// Create the package
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package for: %s"), *AssetPath));
	}

	// Create the widget blueprint via factory
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ParentClass;
	Factory->AddToRoot(); // Prevent GC during creation

	UObject* NewAsset = Factory->FactoryCreateNew(
		UWidgetBlueprint::StaticClass(),
		Package,
		*AssetName,
		RF_Standalone | RF_Public,
		nullptr,
		GWarn
	);

	Factory->RemoveFromRoot();

	if (!NewAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("FactoryCreateNew failed to create Widget Blueprint"));
	}

	UWidgetBlueprint* NewBP = CastChecked<UWidgetBlueprint>(NewAsset);

	// Set root widget if the factory didn't create one (or if we want a different root)
	if (NewBP->WidgetTree && !NewBP->WidgetTree->RootWidget && RootWidgetClass)
	{
		UWidget* Root = NewBP->WidgetTree->ConstructWidget<UWidget>(RootWidgetClass);
		NewBP->WidgetTree->RootWidget = Root;
	}

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), AssetName);
	Result->SetStringField(TEXT("parent_class"), ParentClass->GetName());

	if (NewBP->WidgetTree && NewBP->WidgetTree->RootWidget)
	{
		Result->SetStringField(TEXT("root_widget_name"), NewBP->WidgetTree->RootWidget->GetName());
		Result->SetStringField(TEXT("root_widget_class"), NewBP->WidgetTree->RootWidget->GetClass()->GetName());
	}

	return Result;
}

// ============================================================================
// add_widget_child
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleAddWidgetChild(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(Params, Error);
	if (!WidgetBlueprint)
	{
		return Error;
	}

	if (!Params->HasField(TEXT("widget_class")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_class"));
	}
	if (!Params->HasField(TEXT("widget_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}

	const FString WidgetClassName = Params->GetStringField(TEXT("widget_class"));
	const FString WidgetName = Params->GetStringField(TEXT("widget_name"));
	const FString ParentName = Params->HasField(TEXT("parent_name")) ? Params->GetStringField(TEXT("parent_name")) : TEXT("");
	const int32 Index = Params->HasField(TEXT("index")) ? static_cast<int32>(Params->GetNumberField(TEXT("index"))) : -1;

	// Resolve widget class
	UClass* WidgetClass = ResolveWidgetClass(WidgetClassName);
	if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass()))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Could not resolve widget class: %s"), *WidgetClassName));
	}

	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;

	// Check name uniqueness
	FName FinalName(*WidgetName);
	if (WidgetTree->FindWidget(FinalName))
	{
		FinalName = MakeUniqueObjectName(WidgetTree, WidgetClass, FinalName);
	}

	// Construct widget
	UWidget* NewWidget = WidgetTree->ConstructWidget<UWidget>(WidgetClass, FinalName);
	if (!NewWidget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to construct widget of class: %s"), *WidgetClassName));
	}

	WidgetBlueprint->Modify();

	FString ActualParentName;

	if (!ParentName.IsEmpty())
	{
		// Add as child of specified parent
		UWidget* ParentWidget = WidgetTree->FindWidget(FName(*ParentName));
		if (!ParentWidget)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent widget not found: %s"), *ParentName));
		}

		UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentWidget);
		if (!ParentPanel)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent widget '%s' is not a panel widget (class: %s)"), *ParentName, *ParentWidget->GetClass()->GetName()));
		}

		UPanelSlot* Slot = nullptr;
		if (Index >= 0)
		{
			Slot = ParentPanel->InsertChildAt(Index, NewWidget);
		}
		else
		{
			Slot = ParentPanel->AddChild(NewWidget);
		}

		if (!Slot)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to add child to '%s'. Parent may only accept one child (class: %s)"), *ParentName, *ParentWidget->GetClass()->GetName()));
		}

		ActualParentName = ParentName;
	}
	else
	{
		// Set as root
		if (WidgetTree->RootWidget)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Root widget already exists. Specify parent_name to add as child, or use replace_widget_root to change the root."));
		}
		WidgetTree->RootWidget = NewWidget;
		ActualParentName = TEXT("[root]");
	}

	// Apply properties if provided
	TArray<FString> PropErrors;
	if (Params->HasField(TEXT("properties")))
	{
		const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
		if (Params->TryGetObjectField(TEXT("properties"), PropertiesPtr) && PropertiesPtr)
		{
			FEpicUnrealMCPPropertyUtils::DeserializeObjectProperties(NewWidget, *PropertiesPtr, PropErrors);
		}
	}

	// Apply slot properties if provided (only works if widget has a slot, i.e., has a parent)
	if (Params->HasField(TEXT("slot_properties")) && NewWidget->Slot)
	{
		const TSharedPtr<FJsonObject>* SlotPropertiesPtr = nullptr;
		if (Params->TryGetObjectField(TEXT("slot_properties"), SlotPropertiesPtr) && SlotPropertiesPtr)
		{
			FEpicUnrealMCPPropertyUtils::DeserializeObjectProperties(NewWidget->Slot, *SlotPropertiesPtr, PropErrors);
		}
	}

	// Mark as variable so it can be referenced in the BP graph
	NewWidget->bIsVariable = true;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), FinalName.ToString());
	Result->SetStringField(TEXT("widget_class"), WidgetClass->GetName());
	Result->SetStringField(TEXT("parent"), ActualParentName);

	if (FinalName.ToString() != WidgetName)
	{
		Result->SetStringField(TEXT("requested_name"), WidgetName);
		Result->SetStringField(TEXT("note"), TEXT("Name was modified to ensure uniqueness"));
	}

	if (NewWidget->Slot)
	{
		Result->SetStringField(TEXT("slot_type"), NewWidget->Slot->GetClass()->GetName());
	}

	if (PropErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		for (const FString& Err : PropErrors)
		{
			ErrorArray.Add(MakeShared<FJsonValueString>(Err));
		}
		Result->SetArrayField(TEXT("property_errors"), ErrorArray);
	}

	return Result;
}

// ============================================================================
// set_widget_properties
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleSetWidgetProperties(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(Params, Error);
	if (!WidgetBlueprint)
	{
		return Error;
	}

	if (!Params->HasField(TEXT("widget_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}

	const FString WidgetName = Params->GetStringField(TEXT("widget_name"));
	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
	}

	WidgetBlueprint->Modify();

	TArray<FString> PropErrors;

	// Apply widget properties
	if (Params->HasField(TEXT("properties")))
	{
		const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
		if (Params->TryGetObjectField(TEXT("properties"), PropertiesPtr) && PropertiesPtr)
		{
			FEpicUnrealMCPPropertyUtils::DeserializeObjectProperties(Widget, *PropertiesPtr, PropErrors);
		}
	}

	// Apply slot properties
	if (Params->HasField(TEXT("slot_properties")) && Widget->Slot)
	{
		const TSharedPtr<FJsonObject>* SlotPropertiesPtr = nullptr;
		if (Params->TryGetObjectField(TEXT("slot_properties"), SlotPropertiesPtr) && SlotPropertiesPtr)
		{
			FEpicUnrealMCPPropertyUtils::DeserializeObjectProperties(Widget->Slot, *SlotPropertiesPtr, PropErrors);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("widget_class"), Widget->GetClass()->GetName());

	if (PropErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		for (const FString& Err : PropErrors)
		{
			ErrorArray.Add(MakeShared<FJsonValueString>(Err));
		}
		Result->SetArrayField(TEXT("property_errors"), ErrorArray);
	}

	return Result;
}

// ============================================================================
// remove_widget
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleRemoveWidget(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(Params, Error);
	if (!WidgetBlueprint)
	{
		return Error;
	}

	if (!Params->HasField(TEXT("widget_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: widget_name"));
	}

	const FString WidgetName = Params->GetStringField(TEXT("widget_name"));
	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;
	UWidget* Widget = WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
	}

	WidgetBlueprint->Modify();

	const FString WidgetClass = Widget->GetClass()->GetName();
	const bool bWasRoot = (Widget == WidgetTree->RootWidget);

	bool bRemoved = WidgetTree->RemoveWidget(Widget);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bRemoved);
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("widget_class"), WidgetClass);
	Result->SetBoolField(TEXT("was_root"), bWasRoot);

	if (!bRemoved)
	{
		Result->SetStringField(TEXT("error"), TEXT("WidgetTree::RemoveWidget returned false"));
	}

	return Result;
}

// ============================================================================
// replace_widget_root
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleReplaceWidgetRoot(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(Params, Error);
	if (!WidgetBlueprint)
	{
		return Error;
	}

	if (!Params->HasField(TEXT("new_root_class")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: new_root_class"));
	}

	const FString NewRootClassName = Params->GetStringField(TEXT("new_root_class"));
	const FString NewRootName = Params->HasField(TEXT("new_root_name")) ? Params->GetStringField(TEXT("new_root_name")) : TEXT("");
	const bool bMigrateChildren = !Params->HasField(TEXT("migrate_children")) || Params->GetBoolField(TEXT("migrate_children"));

	// Resolve new root class
	UClass* NewRootClass = ResolveWidgetClass(NewRootClassName);
	if (!NewRootClass || !NewRootClass->IsChildOf(UPanelWidget::StaticClass()))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("New root class '%s' is not a UPanelWidget subclass"), *NewRootClassName));
	}

	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;

	WidgetBlueprint->Modify();

	// Construct new root
	FName RootName = NewRootName.IsEmpty() ? NAME_None : FName(*NewRootName);
	UWidget* NewRoot = WidgetTree->ConstructWidget<UWidget>(NewRootClass, RootName);
	if (!NewRoot)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to construct widget of class: %s"), *NewRootClassName));
	}

	UPanelWidget* NewRootPanel = CastChecked<UPanelWidget>(NewRoot);

	int32 ChildrenMigrated = 0;
	FString OldRootName = TEXT("(none)");
	FString OldRootClass = TEXT("(none)");

	// Migrate children from old root if requested
	UWidget* OldRoot = WidgetTree->RootWidget;
	if (OldRoot)
	{
		OldRootName = OldRoot->GetName();
		OldRootClass = OldRoot->GetClass()->GetName();

		if (bMigrateChildren)
		{
			UPanelWidget* OldPanel = Cast<UPanelWidget>(OldRoot);
			if (OldPanel)
			{
				// Collect children first (modifying while iterating is unsafe)
				TArray<UWidget*> Children;
				for (int32 i = 0; i < OldPanel->GetChildrenCount(); ++i)
				{
					Children.Add(OldPanel->GetChildAt(i));
				}

				// Remove from old root and add to new
				for (UWidget* Child : Children)
				{
					OldPanel->RemoveChild(Child);
					if (NewRootPanel->AddChild(Child))
					{
						ChildrenMigrated++;
					}
				}
			}
		}

		WidgetTree->RemoveWidget(OldRoot);
	}

	WidgetTree->RootWidget = NewRoot;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("new_root_name"), NewRoot->GetName());
	Result->SetStringField(TEXT("new_root_class"), NewRoot->GetClass()->GetName());
	Result->SetStringField(TEXT("old_root_name"), OldRootName);
	Result->SetStringField(TEXT("old_root_class"), OldRootClass);
	Result->SetNumberField(TEXT("children_migrated"), ChildrenMigrated);

	return Result;
}

// ============================================================================
// compile_widget_blueprint
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWidgetCommands::HandleCompileWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(Params, Error);
	if (!WidgetBlueprint)
	{
		return Error;
	}

	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
	Result->SetBoolField(TEXT("compiled"), true);

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
