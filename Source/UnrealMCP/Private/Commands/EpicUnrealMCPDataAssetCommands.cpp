#include "Commands/EpicUnrealMCPDataAssetCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPPropertyUtils.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "UObject/EnumProperty.h"

FEpicUnrealMCPDataAssetCommands::FEpicUnrealMCPDataAssetCommands()
{
}

FEpicUnrealMCPDataAssetCommands::~FEpicUnrealMCPDataAssetCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("read_data_asset"))
	{
		return HandleReadDataAsset(Params);
	}
	else if (CommandType == TEXT("list_data_assets"))
	{
		return HandleListDataAssets(Params);
	}
	else if (CommandType == TEXT("create_data_asset"))
	{
		return HandleCreateDataAsset(Params);
	}
	else if (CommandType == TEXT("update_data_asset"))
	{
		return HandleUpdateDataAsset(Params);
	}
	else if (CommandType == TEXT("set_dataasset_property"))
	{
		return HandleSetDataAssetProperty(Params);
	}
	else if (CommandType == TEXT("get_dataasset_property"))
	{
		return HandleGetDataAssetProperty(Params);
	}
	else if (CommandType == TEXT("describe_data_asset_class"))
	{
		return HandleDescribeDataAssetClass(Params);
	}
	else if (CommandType == TEXT("list_data_asset_classes"))
	{
		return HandleListDataAssetClasses(Params);
	}
	else if (CommandType == TEXT("save_data_asset"))
	{
		return HandleSaveDataAsset(Params);
	}
	else if (CommandType == TEXT("delete_data_asset"))
	{
		return HandleDeleteDataAsset(Params);
	}
	else if (CommandType == TEXT("rename_data_asset"))
	{
		return HandleRenameDataAsset(Params);
	}
	else if (CommandType == TEXT("duplicate_data_asset"))
	{
		return HandleDuplicateDataAsset(Params);
	}
	else if (CommandType == TEXT("data_asset_array_remove"))
	{
		return HandleArrayRemove(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown DataAsset command: %s"), *CommandType));
}

// ============================================================================
// read_data_asset
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params)
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

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), LoadedAsset->GetName());
	Result->SetStringField(TEXT("class_name"), LoadedAsset->GetClass()->GetName());
	Result->SetStringField(TEXT("class_path"), LoadedAsset->GetClass()->GetPathName());

	// Serialize all UPROPERTY fields via reflection
	TSharedPtr<FJsonObject> Properties = FEpicUnrealMCPPropertyUtils::SerializeObjectProperties(LoadedAsset);
	if (Properties.IsValid())
	{
		Result->SetObjectField(TEXT("properties"), Properties);
	}

	return Result;
}

// ============================================================================
// list_data_assets
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleListDataAssets(const TSharedPtr<FJsonObject>& Params)
{
	const FString Directory = Params->HasField(TEXT("directory"))
		? Params->GetStringField(TEXT("directory"))
		: TEXT("/Game/");

	const int32 Limit = Params->HasField(TEXT("limit"))
		? static_cast<int32>(Params->GetNumberField(TEXT("limit")))
		: 100;

	const bool bHasClassFilter = Params->HasField(TEXT("class_filter"));
	const FString ClassFilter = bHasClassFilter
		? Params->GetStringField(TEXT("class_filter"))
		: FString();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Directory));
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	if (bHasClassFilter)
	{
		// Try to resolve the class by full path first, then by short name
		UClass* FilterClass = FindObject<UClass>(nullptr, *ClassFilter);
		if (!FilterClass)
		{
			FilterClass = FindFirstObject<UClass>(*ClassFilter, EFindFirstObjectOptions::ExactClass);
		}

		if (FilterClass)
		{
			Filter.ClassPaths.Add(FilterClass->GetClassPathName());
		}
		else
		{
			// Attempt to use the string directly as a top-level asset path
			// Format: "/Script/ModuleName.ClassName"
			Filter.ClassPaths.Add(FTopLevelAssetPath(*ClassFilter));
		}
	}
	else
	{
		// Default: list all UDataAsset-derived assets
		Filter.ClassPaths.Add(UDataAsset::StaticClass()->GetClassPathName());
	}

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
		AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetNumberField(TEXT("total_found"), TotalFound);
	Result->SetNumberField(TEXT("returned"), ResultCount);
	Result->SetArrayField(TEXT("assets"), AssetsArray);

	return Result;
}

// ============================================================================
// create_data_asset
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleCreateDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	// --- Required params ---
	if (!Params->HasField(TEXT("name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: name"));
	}
	if (!Params->HasField(TEXT("data_asset_class")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: data_asset_class"));
	}

	const FString AssetName = Params->GetStringField(TEXT("name"));
	const FString DataAssetClassName = Params->GetStringField(TEXT("data_asset_class"));

	// --- Optional params ---
	FString PackagePath = Params->HasField(TEXT("package_path"))
		? Params->GetStringField(TEXT("package_path"))
		: TEXT("/Game/Data/");

	// Normalize trailing slash
	if (!PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath += TEXT("/");
	}

	// --- Check for existing asset ---
	const FString FullPath = PackagePath + AssetName;
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset already exists: %s"), *FullPath));
	}

	// --- Resolve class from string (probes all loaded modules) ---
	UClass* AssetClass = FEpicUnrealMCPCommonUtils::ResolveClass(DataAssetClassName);
	if (!AssetClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Could not find class: %s"), *DataAssetClassName));
	}

	// Asset-suitability check: any concrete UObject subclass is fair game, except
	// AActor / UActorComponent which use different creation paths (placement,
	// AddComponent) and would never round-trip as standalone Content Browser assets.
	// This intentionally allows plain UObject-derived data containers (e.g.,
	// classes that extend UObject directly rather than UDataAsset/UPrimaryDataAsset).
	if (AssetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Class '%s' is abstract/deprecated and cannot be instantiated"), *AssetClass->GetName()));
	}
	if (AssetClass->IsChildOf(AActor::StaticClass()) || AssetClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Class '%s' is an Actor/Component, not a Content Browser asset class"), *AssetClass->GetName()));
	}

	// --- Create package and asset ---
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	UObject* NewAsset = NewObject<UObject>(Package, AssetClass, *AssetName, RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to create data asset of class: %s"), *AssetClass->GetName()));
	}

	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

	// --- Set optional properties (best-effort) ---
	TArray<FString> PropertyErrors;
	int32 CreatePropertiesSet = 0;
	if (Params->HasField(TEXT("properties")))
	{
		const TSharedPtr<FJsonObject> Properties = Params->GetObjectField(TEXT("properties"));
		if (Properties.IsValid())
		{
			for (const auto& Pair : Properties->Values)
			{
				FProperty* TargetProp = NewAsset->GetClass()->FindPropertyByName(*Pair.Key);
				if (TargetProp)
				{
					NewAsset->PreEditChange(TargetProp);
				}

				FString ErrorMsg;
				const bool bOk = FEpicUnrealMCPCommonUtils::SetObjectProperty(NewAsset, FString(*Pair.Key), Pair.Value, ErrorMsg);

				if (TargetProp)
				{
					FPropertyChangedEvent ChangeEvent(TargetProp, EPropertyChangeType::ValueSet);
					NewAsset->PostEditChangeProperty(ChangeEvent);
				}

				if (bOk)
				{
					++CreatePropertiesSet;
					if (!ErrorMsg.IsEmpty())
					{
						PropertyErrors.Add(FString::Printf(TEXT("%s [partial]: %s"), *Pair.Key, *ErrorMsg));
					}
				}
				else
				{
					PropertyErrors.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *ErrorMsg));
				}
			}

			// Track success-count separately from PropertyErrors. With the new
			// partial-success contract, a property can land in PropertyErrors as
			// a [partial] warning even though it set state, so the old
			// `PropertyErrors.Num() < Properties->Values.Num()` heuristic
			// stops marking the package dirty when every property partially
			// succeeded.
			if (CreatePropertiesSet > 0)
			{
				Package->MarkPackageDirty();
			}
		}
	}

	// --- Optional save ---
	const bool bSave = Params->HasField(TEXT("save")) && Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(NewAsset, /*bOnlyIfIsDirty=*/false);
	}

	// --- Build response ---
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_name"), AssetName);
	Result->SetStringField(TEXT("asset_path"), FullPath);
	Result->SetStringField(TEXT("class_name"), AssetClass->GetName());
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}

	TSharedPtr<FJsonObject> SerializedProps = FEpicUnrealMCPPropertyUtils::SerializeObjectProperties(NewAsset);
	if (SerializedProps.IsValid())
	{
		Result->SetObjectField(TEXT("properties"), SerializedProps);
	}

	if (PropertyErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarningsArray;
		for (const FString& Err : PropertyErrors)
		{
			WarningsArray.Add(MakeShareable(new FJsonValueString(Err)));
		}
		Result->SetArrayField(TEXT("property_warnings"), WarningsArray);
	}

	return Result;
}

// ============================================================================
// update_data_asset
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleUpdateDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("properties")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: properties"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	// Set properties — fire PreEditChange/PostEditChangeProperty around each so derived
	// state on the asset stays consistent.
	TArray<FString> PropertyErrors;
	int32 PropertiesSet = 0;
	const TSharedPtr<FJsonObject> Properties = Params->GetObjectField(TEXT("properties"));
	if (Properties.IsValid())
	{
		for (const auto& Pair : Properties->Values)
		{
			// Resolve the top-level property (first path segment). Dot/bracket paths are
			// still supported by SetObjectProperty — we just need SOMETHING to hand to the
			// PreEditChange/PostEditChangeProperty callbacks.
			FString TopName = FString(*Pair.Key);
			int32 DotIdx = INDEX_NONE;
			int32 BracketIdx = INDEX_NONE;
			TopName.FindChar(TEXT('.'), DotIdx);
			TopName.FindChar(TEXT('['), BracketIdx);
			int32 SplitIdx = INDEX_NONE;
			if (DotIdx != INDEX_NONE && BracketIdx != INDEX_NONE) SplitIdx = FMath::Min(DotIdx, BracketIdx);
			else if (DotIdx != INDEX_NONE) SplitIdx = DotIdx;
			else if (BracketIdx != INDEX_NONE) SplitIdx = BracketIdx;
			if (SplitIdx != INDEX_NONE)
			{
				TopName = TopName.Left(SplitIdx);
			}

			FProperty* TargetProp = LoadedAsset->GetClass()->FindPropertyByName(*TopName);
			if (TargetProp)
			{
				LoadedAsset->PreEditChange(TargetProp);
			}

			FString ErrorMsg;
			const bool bOk = FEpicUnrealMCPCommonUtils::SetObjectProperty(LoadedAsset, FString(*Pair.Key), Pair.Value, ErrorMsg);

			if (TargetProp)
			{
				FPropertyChangedEvent ChangeEvent(TargetProp, EPropertyChangeType::ValueSet);
				LoadedAsset->PostEditChangeProperty(ChangeEvent);
			}

			if (bOk)
			{
				PropertiesSet++;
				// ErrorMsg is non-empty on partial success — e.g., a TArray<FStruct>
				// where most elements set but one element's TSoftObjectPtr or
				// FGameplayTag failed to resolve. Surface as warning so the caller
				// doesn't treat properties_set as full success.
				if (!ErrorMsg.IsEmpty())
				{
					PropertyErrors.Add(FString::Printf(TEXT("%s [partial]: %s"), *Pair.Key, *ErrorMsg));
				}
			}
			else
			{
				PropertyErrors.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *ErrorMsg));
			}
		}
	}

	if (PropertiesSet > 0)
	{
		LoadedAsset->GetPackage()->MarkPackageDirty();
	}

	// Optional save
	const bool bSave = Params->HasField(TEXT("save")) && Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(LoadedAsset, /*bOnlyIfIsDirty=*/false);
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("properties_set"), PropertiesSet);
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}

	TSharedPtr<FJsonObject> SerializedProps = FEpicUnrealMCPPropertyUtils::SerializeObjectProperties(LoadedAsset);
	if (SerializedProps.IsValid())
	{
		Result->SetObjectField(TEXT("properties"), SerializedProps);
	}

	if (PropertyErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarningsArray;
		for (const FString& Err : PropertyErrors)
		{
			WarningsArray.Add(MakeShareable(new FJsonValueString(Err)));
		}
		Result->SetArrayField(TEXT("property_warnings"), WarningsArray);
	}

	return Result;
}

// ============================================================================
// set_dataasset_property / get_dataasset_property — focused single-path verified
// access. update_data_asset takes a whole {properties} blob and serializes the
// ENTIRE asset back; you can't target one deep path (FamilyRules[2].SoftTarget)
// nor cleanly verify just it. These two operate on ONE property_path:
//   • set writes via the same SetObjectProperty machinery (handles TSoftClassPtr /
//     TSoftObjectPtr / object-ref / array-element / nested-struct paths) and then
//     READS BACK that exact path, returning stored_value so the caller diffs
//     intent vs persisted state in one call — kills the soft-ref silent-drop trap.
//   • get reads ONE path (incl. indexed/dotted), which read_data_asset cannot.
// Works on any loaded UObject asset, not just UDataAsset.
// ============================================================================

namespace
{
	// Resolve a (possibly dotted/indexed) path and serialize the value there.
	// Returns null with OutError set on resolution failure.
	TSharedPtr<FJsonValue> ReadPropertyValueAtPath(UObject* Asset, const FString& Path, FProperty*& OutLeaf, FString& OutError)
	{
		OutLeaf = nullptr;
		FProperty* Leaf = nullptr;
		void* Container = nullptr;
		if (!FEpicUnrealMCPCommonUtils::ResolvePropertyPath(Asset, Path, Leaf, Container, OutError))
		{
			return nullptr;
		}
		OutLeaf = Leaf;
		// Array-element leaves have Offset_Internal 0, so this yields the element address;
		// struct/object leaves yield the field address — uniform for both.
		void* ValueAddr = Leaf->ContainerPtrToValuePtr<void>(Container);
		return FEpicUnrealMCPPropertyUtils::SerializePropertyValue(Leaf, ValueAddr);
	}

	// First path segment, for PreEditChange/PostEditChangeProperty on the top-level property.
	FString TopLevelPropName(const FString& Path)
	{
		FString TopName = Path;
		int32 DotIdx = INDEX_NONE, BracketIdx = INDEX_NONE;
		TopName.FindChar(TEXT('.'), DotIdx);
		TopName.FindChar(TEXT('['), BracketIdx);
		int32 SplitIdx = INDEX_NONE;
		if (DotIdx != INDEX_NONE && BracketIdx != INDEX_NONE) SplitIdx = FMath::Min(DotIdx, BracketIdx);
		else if (DotIdx != INDEX_NONE) SplitIdx = DotIdx;
		else if (BracketIdx != INDEX_NONE) SplitIdx = BracketIdx;
		return (SplitIdx != INDEX_NONE) ? TopName.Left(SplitIdx) : TopName;
	}
}

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleSetDataAssetProperty(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	if (!Params->HasField(TEXT("property_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: property_path"));
	if (!Params->HasField(TEXT("value")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: value"));

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString PropertyPath = Params->GetStringField(TEXT("property_path"));
	const TSharedPtr<FJsonValue> Value = Params->Values.FindRef(TEXT("value"));

	UObject* Asset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!Asset)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));

	FProperty* TopProp = Asset->GetClass()->FindPropertyByName(*TopLevelPropName(PropertyPath));
	if (TopProp)
	{
		Asset->PreEditChange(TopProp);
	}

	FString ErrorMsg;
	const bool bOk = FEpicUnrealMCPCommonUtils::SetObjectProperty(Asset, PropertyPath, Value, ErrorMsg);

	if (TopProp)
	{
		FPropertyChangedEvent ChangeEvent(TopProp, EPropertyChangeType::ValueSet);
		Asset->PostEditChangeProperty(ChangeEvent);
	}

	if (!bOk)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyPath, *ErrorMsg));
	}

	Asset->GetPackage()->MarkPackageDirty();

	const bool bSave = Params->HasField(TEXT("save")) && Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(Asset, /*bOnlyIfIsDirty=*/false);
	}

	// Read back the SAME path so the caller can verify intent vs persisted state.
	FProperty* Leaf = nullptr;
	FString ReadErr;
	TSharedPtr<FJsonValue> Stored = ReadPropertyValueAtPath(Asset, PropertyPath, Leaf, ReadErr);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("property_path"), PropertyPath);
	if (Stored.IsValid())
	{
		Result->SetField(TEXT("stored_value"), Stored);
	}
	else if (!ReadErr.IsEmpty())
	{
		Result->SetStringField(TEXT("readback_error"), ReadErr);
	}
	if (Leaf)
	{
		Result->SetStringField(TEXT("value_type"), Leaf->GetCPPType());
	}
	// ErrorMsg non-empty with bOk true = partial success (e.g. one array element's soft-ref
	// failed to resolve while the rest set). Surface it rather than masking with success=true.
	if (!ErrorMsg.IsEmpty())
	{
		Result->SetStringField(TEXT("warning"), ErrorMsg);
	}
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleGetDataAssetProperty(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	if (!Params->HasField(TEXT("property_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: property_path"));

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString PropertyPath = Params->GetStringField(TEXT("property_path"));

	UObject* Asset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!Asset)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));

	FProperty* Leaf = nullptr;
	FString ReadErr;
	TSharedPtr<FJsonValue> Val = ReadPropertyValueAtPath(Asset, PropertyPath, Leaf, ReadErr);
	if (!Leaf)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Could not resolve '%s': %s"), *PropertyPath, *ReadErr));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("property_path"), PropertyPath);
	if (Val.IsValid())
	{
		Result->SetField(TEXT("value"), Val);
	}
	Result->SetStringField(TEXT("value_type"), Leaf->GetCPPType());
	Result->SetStringField(TEXT("property_class"), Leaf->GetClass()->GetName());
	return Result;
}

// ============================================================================
// data_asset_array_remove — remove element(s) from a TArray property in place.
//
// update_data_asset's only verb for a TArray is whole-container replace: it
// calls FScriptArrayHelper::EmptyValues() then rebuilds from the supplied JSON,
// round-tripping EVERY element through JSON. For arrays of complex structs an
// imperfect round-trip silently corrupts the elements you meant to keep.
// This handler removes only the targeted slice via RemoveValues — the kept
// elements stay as native bytes (never serialized), so order is preserved
// exactly and nothing else can be mangled.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleArrayRemove(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("property_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field: property_path (path to the array property, e.g. \"SurfaceTypes\" or \"Config.Layers\")"));
	}
	if (!Params->HasField(TEXT("index")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: index"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString PropertyPath = Params->GetStringField(TEXT("property_path"));
	const int32 Index = static_cast<int32>(Params->GetNumberField(TEXT("index")));
	const int32 Count = Params->HasField(TEXT("count"))
		? FMath::Max(1, static_cast<int32>(Params->GetNumberField(TEXT("count"))))
		: 1;

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	FProperty* Leaf = nullptr;
	void* Container = nullptr;
	FString Error;
	if (!FEpicUnrealMCPCommonUtils::ResolvePropertyPath(LoadedAsset, PropertyPath, Leaf, Container, Error))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
	}

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(Leaf);
	if (!ArrayProp)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("'%s' does not resolve to an array property (got %s). Point property_path at the array itself, not at an element."),
				*PropertyPath, Leaf ? *Leaf->GetClass()->GetName() : TEXT("null")));
	}

	void* ArrayAddr = ArrayProp->ContainerPtrToValuePtr<void>(Container);
	FScriptArrayHelper Helper(ArrayProp, ArrayAddr);
	const int32 OldSize = Helper.Num();

	if (Index < 0 || Index >= OldSize)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Index %d out of range for '%s' (size %d)"), Index, *PropertyPath, OldSize));
	}
	const int32 RemoveCount = FMath::Min(Count, OldSize - Index);

	// Fire PreEdit/PostEdit on the TOP-LEVEL property so derived asset state
	// (e.g. a MegaMaterial's generated variants) rebuilds. Mirrors the pattern
	// in HandleUpdateDataAsset.
	FString TopName = PropertyPath;
	int32 DotIdx = INDEX_NONE;
	int32 BracketIdx = INDEX_NONE;
	TopName.FindChar(TEXT('.'), DotIdx);
	TopName.FindChar(TEXT('['), BracketIdx);
	int32 SplitIdx = INDEX_NONE;
	if (DotIdx != INDEX_NONE && BracketIdx != INDEX_NONE) SplitIdx = FMath::Min(DotIdx, BracketIdx);
	else if (DotIdx != INDEX_NONE) SplitIdx = DotIdx;
	else if (BracketIdx != INDEX_NONE) SplitIdx = BracketIdx;
	if (SplitIdx != INDEX_NONE) TopName = TopName.Left(SplitIdx);

	FProperty* TopProp = LoadedAsset->GetClass()->FindPropertyByName(*TopName);
	if (TopProp)
	{
		LoadedAsset->PreEditChange(TopProp);
	}

	Helper.RemoveValues(Index, RemoveCount);

	if (TopProp)
	{
		FPropertyChangedEvent ChangeEvent(TopProp, EPropertyChangeType::ArrayRemove);
		LoadedAsset->PostEditChangeProperty(ChangeEvent);
	}

	LoadedAsset->GetPackage()->MarkPackageDirty();

	const bool bSave = Params->HasField(TEXT("save")) ? Params->GetBoolField(TEXT("save")) : true;
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(LoadedAsset, /*bOnlyIfIsDirty=*/false);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("property_path"), PropertyPath);
	Result->SetNumberField(TEXT("removed_count"), RemoveCount);
	Result->SetNumberField(TEXT("old_size"), OldSize);
	Result->SetNumberField(TEXT("new_size"), Helper.Num());
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}
	return Result;
}

// ============================================================================
// describe_data_asset_class — reflection-based schema for a UDataAsset class
// ============================================================================

namespace
{
	TSharedPtr<FJsonObject> DescribeProperty(FProperty* Property, int32 Depth);

	TSharedPtr<FJsonObject> DescribeStruct(UScriptStruct* Struct, int32 Depth)
	{
		TSharedPtr<FJsonObject> StructObj = MakeShareable(new FJsonObject);
		if (!Struct)
		{
			return StructObj;
		}
		StructObj->SetStringField(TEXT("struct_name"), Struct->GetName());
		StructObj->SetStringField(TEXT("struct_path"), Struct->GetPathName());

		// Depth-cap recursion to avoid explosion on deeply nested engine types.
		if (Depth >= 4)
		{
			return StructObj;
		}

		TArray<TSharedPtr<FJsonValue>> FieldsArray;
		for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
		{
			FProperty* SubProp = *PropIt;
			if (SubProp->HasAnyPropertyFlags(CPF_Deprecated))
			{
				continue;
			}
			FieldsArray.Add(MakeShareable(new FJsonValueObject(DescribeProperty(SubProp, Depth + 1))));
		}
		StructObj->SetArrayField(TEXT("fields"), FieldsArray);
		return StructObj;
	}

	TSharedPtr<FJsonObject> DescribeProperty(FProperty* Property, int32 Depth)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("name"), Property->GetName());
		Obj->SetStringField(TEXT("type"), Property->GetClass()->GetName());
		Obj->SetStringField(TEXT("cpp_type"), Property->GetCPPType());

#if WITH_EDITOR
		const FString Category = Property->GetMetaData(TEXT("Category"));
		if (!Category.IsEmpty()) Obj->SetStringField(TEXT("category"), Category);
		const FString Tooltip = Property->GetMetaData(TEXT("ToolTip"));
		if (!Tooltip.IsEmpty()) Obj->SetStringField(TEXT("tooltip"), Tooltip);

		for (const TCHAR* Key : { TEXT("ClampMin"), TEXT("ClampMax"), TEXT("UIMin"), TEXT("UIMax") })
		{
			const FString MetaVal = Property->GetMetaData(Key);
			if (!MetaVal.IsEmpty())
			{
				Obj->SetStringField(Key, MetaVal);
			}
		}
#endif

		TArray<TSharedPtr<FJsonValue>> Flags;
		if (Property->HasAnyPropertyFlags(CPF_Edit)) Flags.Add(MakeShareable(new FJsonValueString(TEXT("EditAnywhere"))));
		if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible)) Flags.Add(MakeShareable(new FJsonValueString(TEXT("BlueprintReadOnly"))));
		if (!Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly) && Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
			Flags.Add(MakeShareable(new FJsonValueString(TEXT("BlueprintReadWrite"))));
		if (Property->HasAnyPropertyFlags(CPF_Config)) Flags.Add(MakeShareable(new FJsonValueString(TEXT("Config"))));
		if (Property->HasAnyPropertyFlags(CPF_Transient)) Flags.Add(MakeShareable(new FJsonValueString(TEXT("Transient"))));
		if (Flags.Num() > 0) Obj->SetArrayField(TEXT("flags"), Flags);

		// Enum values
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
		{
			if (UEnum* E = EnumProp->GetEnum())
			{
				Obj->SetStringField(TEXT("enum_name"), E->GetName());
				TArray<TSharedPtr<FJsonValue>> Vals;
				for (int32 i = 0; i < E->NumEnums() - 1; ++i) // skip _MAX sentinel
				{
					TSharedPtr<FJsonObject> V = MakeShareable(new FJsonObject);
					V->SetStringField(TEXT("name"), E->GetNameStringByIndex(i));
					V->SetNumberField(TEXT("value"), static_cast<double>(E->GetValueByIndex(i)));
					Vals.Add(MakeShareable(new FJsonValueObject(V)));
				}
				Obj->SetArrayField(TEXT("enum_values"), Vals);
			}
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
		{
			if (UEnum* E = ByteProp->GetIntPropertyEnum())
			{
				Obj->SetStringField(TEXT("enum_name"), E->GetName());
				TArray<TSharedPtr<FJsonValue>> Vals;
				for (int32 i = 0; i < E->NumEnums() - 1; ++i)
				{
					TSharedPtr<FJsonObject> V = MakeShareable(new FJsonObject);
					V->SetStringField(TEXT("name"), E->GetNameStringByIndex(i));
					V->SetNumberField(TEXT("value"), static_cast<double>(E->GetValueByIndex(i)));
					Vals.Add(MakeShareable(new FJsonValueObject(V)));
				}
				Obj->SetArrayField(TEXT("enum_values"), Vals);
			}
		}

		// Struct schema (recurse once)
		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			Obj->SetObjectField(TEXT("struct"), DescribeStruct(StructProp->Struct, Depth));
		}

		// Array / Set / Map element schema
		if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
		{
			Obj->SetObjectField(TEXT("element"), DescribeProperty(ArrayProp->Inner, Depth + 1));
		}
		if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
		{
			Obj->SetObjectField(TEXT("element"), DescribeProperty(SetProp->ElementProp, Depth + 1));
		}
		if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
		{
			Obj->SetObjectField(TEXT("key"), DescribeProperty(MapProp->KeyProp, Depth + 1));
			Obj->SetObjectField(TEXT("value"), DescribeProperty(MapProp->ValueProp, Depth + 1));
		}

		// Object / class reference constraints
		if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
		{
			if (ClassProp->MetaClass) Obj->SetStringField(TEXT("meta_class"), ClassProp->MetaClass->GetPathName());
		}
		else if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
		{
			if (SoftClassProp->MetaClass) Obj->SetStringField(TEXT("meta_class"), SoftClassProp->MetaClass->GetPathName());
		}
		else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
		{
			if (ObjProp->PropertyClass) Obj->SetStringField(TEXT("object_class"), ObjProp->PropertyClass->GetPathName());
			// Signal that this property requires the {"class":..., "properties":...}
			// JSON form rather than an asset path — instanced subobjects are owned by
			// their outer and can't be cross-referenced between assets.
			if (Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance))
			{
				Obj->SetBoolField(TEXT("instanced"), true);
			}
#if WITH_EDITOR
			const FString AllowedClasses = Property->GetMetaData(TEXT("AllowedClasses"));
			if (!AllowedClasses.IsEmpty()) Obj->SetStringField(TEXT("allowed_classes"), AllowedClasses);
#endif
		}

		return Obj;
	}
}

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleDescribeDataAssetClass(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("data_asset_class")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: data_asset_class"));
	}

	const FString ClassName = Params->GetStringField(TEXT("data_asset_class"));
	UClass* AssetClass = FEpicUnrealMCPCommonUtils::ResolveClass(ClassName);
	if (!AssetClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Could not find class: %s"), *ClassName));
	}

	const bool bIncludeInherited = !Params->HasField(TEXT("include_inherited"))
		|| Params->GetBoolField(TEXT("include_inherited"));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("class_name"), AssetClass->GetName());
	Result->SetStringField(TEXT("class_path"), AssetClass->GetPathName());
	Result->SetBoolField(TEXT("is_data_asset"), AssetClass->IsChildOf(UDataAsset::StaticClass()));

	TArray<TSharedPtr<FJsonValue>> Hierarchy;
	for (UClass* Super = AssetClass->GetSuperClass(); Super != nullptr; Super = Super->GetSuperClass())
	{
		Hierarchy.Add(MakeShareable(new FJsonValueString(Super->GetPathName())));
	}
	Result->SetArrayField(TEXT("class_hierarchy"), Hierarchy);

	TArray<TSharedPtr<FJsonValue>> PropsArray;
	const EFieldIterationFlags IterFlags = bIncludeInherited
		? EFieldIterationFlags::IncludeSuper
		: EFieldIterationFlags::None;
	for (TFieldIterator<FProperty> PropIt(AssetClass, IterFlags); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}
		PropsArray.Add(MakeShareable(new FJsonValueObject(DescribeProperty(Property, 0))));
	}
	Result->SetArrayField(TEXT("properties"), PropsArray);

	return Result;
}

// ============================================================================
// list_data_asset_classes — enumerate UDataAsset subclasses loaded in the editor
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleListDataAssetClasses(const TSharedPtr<FJsonObject>& Params)
{
	const int32 Limit = Params->HasField(TEXT("limit"))
		? static_cast<int32>(Params->GetNumberField(TEXT("limit")))
		: 500;
	const FString NameFilter = Params->HasField(TEXT("name_contains"))
		? Params->GetStringField(TEXT("name_contains"))
		: FString();

	TArray<TSharedPtr<FJsonValue>> Classes;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (!C || !C->IsChildOf(UDataAsset::StaticClass())) continue;
		if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
		if (!NameFilter.IsEmpty() && !C->GetName().Contains(NameFilter)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("class_name"), C->GetName());
		Entry->SetStringField(TEXT("class_path"), C->GetPathName());
		UClass* Super = C->GetSuperClass();
		Entry->SetStringField(TEXT("parent_class"), Super ? Super->GetName() : FString());
		Classes.Add(MakeShareable(new FJsonValueObject(Entry)));

		if (Classes.Num() >= Limit) break;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("returned"), Classes.Num());
	Result->SetArrayField(TEXT("classes"), Classes);
	return Result;
}

// ============================================================================
// save_data_asset — persist a loaded asset to disk
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleSaveDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const bool bOnlyIfDirty = !Params->HasField(TEXT("only_if_dirty"))
		|| Params->GetBoolField(TEXT("only_if_dirty"));

	const bool bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, bOnlyIfDirty);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bSaved);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return Result;
}

// ============================================================================
// delete_data_asset
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleDeleteDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	if (!FEpicUnrealMCPCommonUtils::AssetExists(AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset does not exist: %s"), *AssetPath));
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
// rename_data_asset
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleRenameDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("source_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: source_path"));
	}
	if (!Params->HasField(TEXT("destination_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: destination_path"));
	}

	const FString SourcePath = Params->GetStringField(TEXT("source_path"));
	const FString DestPath = Params->GetStringField(TEXT("destination_path"));

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

	const bool bRenamed = UEditorAssetLibrary::RenameAsset(SourcePath, DestPath);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bRenamed);
	Result->SetStringField(TEXT("source_path"), SourcePath);
	Result->SetStringField(TEXT("destination_path"), DestPath);
	Result->SetBoolField(TEXT("renamed"), bRenamed);
	return Result;
}

// ============================================================================
// duplicate_data_asset
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleDuplicateDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("source_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: source_path"));
	}
	if (!Params->HasField(TEXT("destination_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: destination_path"));
	}

	const FString SourcePath = Params->GetStringField(TEXT("source_path"));
	const FString DestPath = Params->GetStringField(TEXT("destination_path"));

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
