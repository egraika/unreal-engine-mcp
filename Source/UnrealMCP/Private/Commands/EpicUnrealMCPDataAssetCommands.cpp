#include "Commands/EpicUnrealMCPDataAssetCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPPropertyUtils.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataAsset.h"
#include "AthenaAIAgentPreset.h"
#include "AthenaAITask.h"

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
	else if (CommandType == TEXT("update_data_asset"))
	{
		return HandleUpdateDataAsset(Params);
	}
	else if (CommandType == TEXT("create_data_asset"))
	{
		return HandleCreateDataAsset(Params);
	}
	else if (CommandType == TEXT("populate_athena_preset"))
	{
		return HandlePopulateAthenaPreset(Params);
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
		UClass* FilterClass = nullptr;

		if (ClassFilter.Contains(TEXT("/")))
		{
			// Full path provided (e.g. "/Script/AthenaAI.AthenaAIPreset")
			FilterClass = FindObject<UClass>(nullptr, *ClassFilter);
			if (!FilterClass)
			{
				FilterClass = LoadObject<UClass>(nullptr, *ClassFilter);
			}
			if (FilterClass)
			{
				Filter.ClassPaths.Add(FilterClass->GetClassPathName());
			}
			else
			{
				Filter.ClassPaths.Add(FTopLevelAssetPath(*ClassFilter));
			}
		}
		else
		{
			// Short name — try to resolve dynamically
			FilterClass = FindObject<UClass>(nullptr, *ClassFilter);
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
				// Cannot safely construct FTopLevelAssetPath from a short name in UE5.7
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Could not resolve class '%s'. Use full path format: /Script/ModuleName.ClassName"), *ClassFilter));
			}
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
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropertiesPtr) || !PropertiesPtr)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'properties' must be a JSON object"));
	}

	// Deserialize the provided properties into the asset
	TArray<FString> Errors;
	FEpicUnrealMCPPropertyUtils::DeserializeObjectProperties(LoadedAsset, *PropertiesPtr, Errors);

	// Collect which properties were requested
	TArray<TSharedPtr<FJsonValue>> UpdatedNames;
	for (const auto& Pair : (*PropertiesPtr)->Values)
	{
		if (!Pair.Key.StartsWith(TEXT("_")))
		{
			UpdatedNames.Add(MakeShared<FJsonValueString>(Pair.Key));
		}
	}

	// Mark dirty
	LoadedAsset->MarkPackageDirty();

	// Optionally save to disk
	const bool bSave = Params->HasField(TEXT("save")) && Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Errors.Num() == 0);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetArrayField(TEXT("updated_properties"), UpdatedNames);

	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		for (const FString& Err : Errors)
		{
			ErrorArray.Add(MakeShared<FJsonValueString>(Err));
		}
		Result->SetArrayField(TEXT("errors"), ErrorArray);
	}

	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}

	return Result;
}

// ============================================================================
// create_data_asset
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandleCreateDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("class_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: class_name"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString ClassName = Params->GetStringField(TEXT("class_name"));

	// Check if asset already exists
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset already exists: %s"), *AssetPath));
	}

	// Resolve the UClass
	UClass* AssetClass = FindObject<UClass>(nullptr, *ClassName);
	if (!AssetClass)
	{
		AssetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	}
	if (!AssetClass)
	{
		AssetClass = LoadObject<UClass>(nullptr, *ClassName);
	}
	if (!AssetClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Could not find class: %s"), *ClassName));
	}

	// Verify it's a UDataAsset subclass (or at least a UObject for general use)
	if (!AssetClass->IsChildOf(UDataAsset::StaticClass()))
	{
		// Allow any UObject but warn
		UE_LOG(LogTemp, Warning, TEXT("create_data_asset: class '%s' is not a UDataAsset subclass"), *ClassName);
	}

	// Split asset_path into package path and asset name
	// e.g. "/Game/Data/Weapons/DA_Katana" → PackagePath="/Game/Data/Weapons", AssetName="DA_Katana"
	FString PackagePath, AssetName;
	AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid asset_path format: %s"), *AssetPath));
	}

	// Create the package and asset
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package for: %s"), *AssetPath));
	}

	UObject* NewAsset = NewObject<UObject>(Package, AssetClass, *AssetName, RF_Standalone | RF_Public);
	if (!NewAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("NewObject failed for class: %s"), *ClassName));
	}

	// Notify the asset registry
	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

	// Optionally set initial properties
	TArray<FString> PropErrors;
	if (Params->HasField(TEXT("properties")))
	{
		const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
		if (Params->TryGetObjectField(TEXT("properties"), PropertiesPtr) && PropertiesPtr)
		{
			FEpicUnrealMCPPropertyUtils::DeserializeObjectProperties(NewAsset, *PropertiesPtr, PropErrors);
		}
	}

	// Optionally save to disk
	const bool bSave = Params->HasField(TEXT("save")) && Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), AssetName);
	Result->SetStringField(TEXT("class_name"), AssetClass->GetPathName());

	if (PropErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		for (const FString& Err : PropErrors)
		{
			ErrorArray.Add(MakeShared<FJsonValueString>(Err));
		}
		Result->SetArrayField(TEXT("property_errors"), ErrorArray);
	}

	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}

	return Result;
}

// ============================================================================
// Helper: resolve UClass by name (short or full path)
// ============================================================================
static UClass* TryResolveClass(const FString& Name)
{
	UClass* Found = FindObject<UClass>(nullptr, *Name);
	if (!Found) Found = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::ExactClass);
	if (!Found) Found = LoadObject<UClass>(nullptr, *Name);
	return Found;
}

static UClass* ResolveClassName(const FString& ClassName)
{
	// Try as-is first
	UClass* FoundClass = TryResolveClass(ClassName);
	if (FoundClass) return FoundClass;

	// UE5 reflected class names strip the U/A/F prefix from C++ names.
	// If user passed "UPRKAthenaTask_Foo", try "PRKAthenaTask_Foo".
	if (ClassName.StartsWith(TEXT("U")) && ClassName.Len() > 1 && FChar::IsUpper(ClassName[1]))
	{
		FString Stripped = ClassName.Mid(1);
		FoundClass = TryResolveClass(Stripped);
		if (FoundClass) return FoundClass;

		// Also try as full path: /Script/PRK.StrippedName
		FoundClass = TryResolveClass(FString::Printf(TEXT("/Script/PRK.%s"), *Stripped));
		if (FoundClass) return FoundClass;
	}

	// If short name (no dots/slashes), try as /Script/PRK.ClassName
	if (!ClassName.Contains(TEXT(".")) && !ClassName.Contains(TEXT("/")))
	{
		FoundClass = TryResolveClass(FString::Printf(TEXT("/Script/PRK.%s"), *ClassName));
		if (FoundClass) return FoundClass;
	}

	return nullptr;
}

// ============================================================================
// Helper: create a single inline task instance from JSON
// ============================================================================
static UAthenaAITask* CreateTaskFromJson(
	const TSharedPtr<FJsonObject>& TaskJson,
	UAthenaAIAgentPreset* Outer,
	FString& OutError)
{
	if (!TaskJson.IsValid())
	{
		OutError = TEXT("Null task JSON object");
		return nullptr;
	}

	if (!TaskJson->HasField(TEXT("_class")))
	{
		OutError = TEXT("Task object missing '_class' field");
		return nullptr;
	}

	const FString ClassName = TaskJson->GetStringField(TEXT("_class"));
	UClass* TaskClass = ResolveClassName(ClassName);
	if (!TaskClass)
	{
		OutError = FString::Printf(TEXT("Could not find class '%s'"), *ClassName);
		return nullptr;
	}

	if (!TaskClass->IsChildOf(UAthenaAITask::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Class '%s' is not a UAthenaAITask subclass"), *ClassName);
		return nullptr;
	}

	if (TaskClass->HasAnyClassFlags(CLASS_Abstract))
	{
		OutError = FString::Printf(TEXT("Class '%s' is abstract and cannot be instantiated"), *ClassName);
		return nullptr;
	}

	UAthenaAITask* NewTask = NewObject<UAthenaAITask>(Outer, TaskClass);
	if (!NewTask)
	{
		OutError = FString::Printf(TEXT("NewObject failed for class '%s'"), *ClassName);
		return nullptr;
	}

	// Apply any property overrides from the JSON (skip _class which is handled by DeserializeObjectProperties)
	TArray<FString> PropErrors;
	FEpicUnrealMCPPropertyUtils::DeserializeObjectProperties(NewTask, TaskJson, PropErrors);
	if (PropErrors.Num() > 0)
	{
		OutError = FString::Printf(TEXT("Task '%s' property warnings: %s"),
			*ClassName, *FString::Join(PropErrors, TEXT("; ")));
	}

	return NewTask;
}

// ============================================================================
// populate_athena_preset
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataAssetCommands::HandlePopulateAthenaPreset(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("tasks")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: tasks"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	// Load the preset asset
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	UAthenaAIAgentPreset* Preset = Cast<UAthenaAIAgentPreset>(LoadedAsset);
	if (!Preset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset '%s' is not a UAthenaAIAgentPreset (actual class: %s)"),
				*AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	// Parse tasks array
	const TArray<TSharedPtr<FJsonValue>>* TasksArrayPtr = nullptr;
	if (!Params->TryGetArrayField(TEXT("tasks"), TasksArrayPtr) || !TasksArrayPtr)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'tasks' must be a JSON array"));
	}

	// Clear existing tasks
	Preset->Tasks.Empty();

	// Create each task
	TArray<FString> Errors;
	TArray<FString> CreatedTaskNames;

	for (int32 i = 0; i < TasksArrayPtr->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* TaskObjPtr = nullptr;
		if (!(*TasksArrayPtr)[i]->TryGetObject(TaskObjPtr) || !TaskObjPtr || !TaskObjPtr->IsValid())
		{
			Errors.Add(FString::Printf(TEXT("tasks[%d]: expected JSON object"), i));
			continue;
		}

		FString TaskError;
		UAthenaAITask* NewTask = CreateTaskFromJson(*TaskObjPtr, Preset, TaskError);
		if (NewTask)
		{
			Preset->Tasks.Add(NewTask);
			CreatedTaskNames.Add(NewTask->GetClass()->GetName());
		}
		if (!TaskError.IsEmpty())
		{
			Errors.Add(FString::Printf(TEXT("tasks[%d]: %s"), i, *TaskError));
		}
	}

	// Handle FallbackTask
	if (Params->HasField(TEXT("fallback_task")))
	{
		const TSharedPtr<FJsonObject>* FallbackObjPtr = nullptr;
		if (Params->TryGetObjectField(TEXT("fallback_task"), FallbackObjPtr) && FallbackObjPtr && FallbackObjPtr->IsValid())
		{
			FString FallbackError;
			UAthenaAITask* FallbackTask = CreateTaskFromJson(*FallbackObjPtr, Preset, FallbackError);
			if (FallbackTask)
			{
				Preset->FallbackTask = FallbackTask;
			}
			if (!FallbackError.IsEmpty())
			{
				Errors.Add(FString::Printf(TEXT("fallback_task: %s"), *FallbackError));
			}
		}
	}

	// Rebuild ConsiderationTags from all tasks (replicates UpdateTaskTags)
	Preset->ConsiderationTags.Reset();
	for (UAthenaAITask* Task : Preset->Tasks)
	{
		if (Task)
		{
			for (const FGameplayTag& Tag : Task->GetConsiderations())
			{
				Preset->ConsiderationTags.AddTag(Tag);
			}
		}
	}
	if (Preset->FallbackTask)
	{
		for (const FGameplayTag& Tag : Preset->FallbackTask->GetConsiderations())
		{
			Preset->ConsiderationTags.AddTag(Tag);
		}
	}

	// Regenerate consideration curves if requested
	const bool bGenerateCurves = !Params->HasField(TEXT("generate_curves")) || Params->GetBoolField(TEXT("generate_curves"));
	bool bCurvesGenerated = false;
	if (bGenerateCurves)
	{
		UFunction* GenCurvesFunc = Preset->FindFunction(TEXT("GenerateConsiderationCurves"));
		if (GenCurvesFunc)
		{
			Preset->ProcessEvent(GenCurvesFunc, nullptr);
			bCurvesGenerated = true;
		}
		else
		{
			Errors.Add(TEXT("Could not find GenerateConsiderationCurves function (editor-only?)"));
		}
	}

	// Mark dirty
	Preset->MarkPackageDirty();

	// Optionally save
	const bool bSave = Params->HasField(TEXT("save")) && Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

		// Also save the curve table if it was modified
		if (bCurvesGenerated && Preset->ConsdrCurveTable)
		{
			const FString CurveTablePath = Preset->ConsdrCurveTable->GetPathName();
			FString PackagePath = CurveTablePath;
			int32 DotIndex;
			if (PackagePath.FindChar('.', DotIndex))
			{
				PackagePath.LeftInline(DotIndex);
			}
			UEditorAssetLibrary::SaveAsset(PackagePath, /*bOnlyIfIsDirty=*/false);
		}
	}

	// Build response — always report success if at least one task was created.
	// The bridge checks "success" + "error" (singular string) for error routing.
	const bool bHasErrors = Errors.Num() > 0;
	const bool bPartialSuccess = Preset->Tasks.Num() > 0;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bPartialSuccess || !bHasErrors);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("tasks_created"), Preset->Tasks.Num());
	Result->SetBoolField(TEXT("has_fallback_task"), Preset->FallbackTask != nullptr);
	Result->SetBoolField(TEXT("curves_generated"), bCurvesGenerated);
	Result->SetNumberField(TEXT("consideration_tags_count"), Preset->ConsiderationTags.Num());

	TArray<TSharedPtr<FJsonValue>> TaskNamesArray;
	for (const FString& Name : CreatedTaskNames)
	{
		TaskNamesArray.Add(MakeShared<FJsonValueString>(Name));
	}
	Result->SetArrayField(TEXT("task_classes"), TaskNamesArray);

	TArray<TSharedPtr<FJsonValue>> ConsTagsArray;
	for (const FGameplayTag& Tag : Preset->ConsiderationTags)
	{
		ConsTagsArray.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}
	Result->SetArrayField(TEXT("consideration_tags"), ConsTagsArray);

	if (bHasErrors)
	{
		// Set singular "error" string for bridge compatibility
		Result->SetStringField(TEXT("error"), FString::Join(Errors, TEXT("; ")));

		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		for (const FString& Err : Errors)
		{
			ErrorArray.Add(MakeShared<FJsonValueString>(Err));
		}
		Result->SetArrayField(TEXT("warnings"), ErrorArray);
	}

	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}

	return Result;
}
