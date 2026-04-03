#include "Commands/EpicUnrealMCPDataAssetCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPPropertyUtils.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataAsset.h"

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
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
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

	// --- Resolve class from string ---
	FString ClassName = DataAssetClassName;
	UClass* AssetClass = nullptr;

	// Step 1: Try FindFirstObject as-given
	AssetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);

	// Step 2: Try with U prefix
	if (!AssetClass && !ClassName.StartsWith(TEXT("U")))
	{
		ClassName = TEXT("U") + ClassName;
		AssetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
	}

	// Step 3: Try LoadClass via /Script/PRK
	if (!AssetClass)
	{
		const FString PRKClassPath = FString::Printf(TEXT("/Script/PRK.%s"), *ClassName);
		AssetClass = LoadClass<UDataAsset>(nullptr, *PRKClassPath);
	}

	// Step 4: Try /Script/Engine for engine types
	if (!AssetClass)
	{
		const FString EngineClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
		AssetClass = LoadClass<UDataAsset>(nullptr, *EngineClassPath);
	}

	if (!AssetClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Could not find class: %s"), *DataAssetClassName));
	}

	if (!AssetClass->IsChildOf(UDataAsset::StaticClass()))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Class '%s' is not a UDataAsset subclass"), *AssetClass->GetName()));
	}

	// --- Create package and asset ---
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	UDataAsset* NewAsset = NewObject<UDataAsset>(Package, AssetClass, *AssetName, RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to create data asset of class: %s"), *AssetClass->GetName()));
	}

	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

	// --- Set optional properties (best-effort) ---
	TArray<FString> PropertyErrors;
	if (Params->HasField(TEXT("properties")))
	{
		const TSharedPtr<FJsonObject> Properties = Params->GetObjectField(TEXT("properties"));
		if (Properties.IsValid())
		{
			for (const auto& Pair : Properties->Values)
			{
				FString ErrorMsg;
				if (!FEpicUnrealMCPCommonUtils::SetObjectProperty(NewAsset, Pair.Key, Pair.Value, ErrorMsg))
				{
					PropertyErrors.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *ErrorMsg));
				}
			}

			if (PropertyErrors.Num() < Properties->Values.Num())
			{
				Package->MarkPackageDirty();
			}
		}
	}

	// --- Build response ---
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_name"), AssetName);
	Result->SetStringField(TEXT("asset_path"), FullPath);
	Result->SetStringField(TEXT("class_name"), AssetClass->GetName());

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
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	// Set properties
	TArray<FString> PropertyErrors;
	int32 PropertiesSet = 0;
	const TSharedPtr<FJsonObject> Properties = Params->GetObjectField(TEXT("properties"));
	if (Properties.IsValid())
	{
		for (const auto& Pair : Properties->Values)
		{
			FString ErrorMsg;
			if (FEpicUnrealMCPCommonUtils::SetObjectProperty(LoadedAsset, Pair.Key, Pair.Value, ErrorMsg))
			{
				PropertiesSet++;
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

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("properties_set"), PropertiesSet);

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
