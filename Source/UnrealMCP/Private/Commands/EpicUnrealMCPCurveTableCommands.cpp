#include "Commands/EpicUnrealMCPCurveTableCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/CurveTable.h"
#include "Curves/RichCurve.h"
#include "Curves/SimpleCurve.h"

namespace
{
	// -----------------------------------------------------------------------
	// Mode <-> string
	// -----------------------------------------------------------------------

	FString CurveTableModeToString(ECurveTableMode Mode)
	{
		switch (Mode)
		{
			case ECurveTableMode::RichCurves:   return TEXT("RichCurves");
			case ECurveTableMode::SimpleCurves: return TEXT("SimpleCurves");
			case ECurveTableMode::Empty:
			default:                            return TEXT("Empty");
		}
	}

	bool ParseCurveTableMode(const FString& In, ECurveTableMode& OutMode, FString& OutError)
	{
		if (In.Equals(TEXT("RichCurves"), ESearchCase::IgnoreCase))
		{
			OutMode = ECurveTableMode::RichCurves;
			return true;
		}
		if (In.Equals(TEXT("SimpleCurves"), ESearchCase::IgnoreCase))
		{
			OutMode = ECurveTableMode::SimpleCurves;
			return true;
		}
		OutError = FString::Printf(TEXT("curve_mode must be 'RichCurves' or 'SimpleCurves' (got '%s')"), *In);
		return false;
	}

	// -----------------------------------------------------------------------
	// Interp mode <-> string (shared RichCurveInterpMode enum)
	// -----------------------------------------------------------------------

	FString InterpModeToString(ERichCurveInterpMode Mode)
	{
		switch (Mode)
		{
			case RCIM_Constant: return TEXT("Constant");
			case RCIM_Linear:   return TEXT("Linear");
			case RCIM_Cubic:    return TEXT("Cubic");
			default:            return TEXT("Linear");
		}
	}

	ERichCurveInterpMode ParseInterpMode(const FString& Mode)
	{
		if (Mode.Equals(TEXT("Constant"), ESearchCase::IgnoreCase)) return RCIM_Constant;
		if (Mode.Equals(TEXT("Linear"), ESearchCase::IgnoreCase))   return RCIM_Linear;
		if (Mode.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase))    return RCIM_Cubic;
		return RCIM_Linear;
	}

	ERichCurveTangentMode ParseTangentMode(const FString& Mode)
	{
		if (Mode.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))     return RCTM_Auto;
		if (Mode.Equals(TEXT("User"), ESearchCase::IgnoreCase))     return RCTM_User;
		if (Mode.Equals(TEXT("Break"), ESearchCase::IgnoreCase))    return RCTM_Break;
		if (Mode.Equals(TEXT("None"), ESearchCase::IgnoreCase))     return RCTM_None;
		return RCTM_Auto;
	}

	FString TangentModeToString(ERichCurveTangentMode Mode)
	{
		switch (Mode)
		{
			case RCTM_Auto:  return TEXT("Auto");
			case RCTM_User:  return TEXT("User");
			case RCTM_Break: return TEXT("Break");
			case RCTM_None:  return TEXT("None");
			default:         return TEXT("Auto");
		}
	}

	// -----------------------------------------------------------------------
	// Rich / Simple key <-> JSON
	// -----------------------------------------------------------------------

	TSharedPtr<FJsonObject> RichCurveKeyToJson(const FRichCurveKey& Key)
	{
		TSharedPtr<FJsonObject> KeyObj = MakeShareable(new FJsonObject);
		KeyObj->SetNumberField(TEXT("time"), Key.Time);
		KeyObj->SetNumberField(TEXT("value"), Key.Value);
		KeyObj->SetStringField(TEXT("interp_mode"), InterpModeToString(Key.InterpMode));
		KeyObj->SetStringField(TEXT("tangent_mode"), TangentModeToString(Key.TangentMode));
		KeyObj->SetNumberField(TEXT("arrive_tangent"), Key.ArriveTangent);
		KeyObj->SetNumberField(TEXT("leave_tangent"), Key.LeaveTangent);
		return KeyObj;
	}

	TSharedPtr<FJsonObject> SimpleCurveKeyToJson(const FSimpleCurveKey& Key)
	{
		TSharedPtr<FJsonObject> KeyObj = MakeShareable(new FJsonObject);
		KeyObj->SetNumberField(TEXT("time"), Key.Time);
		KeyObj->SetNumberField(TEXT("value"), Key.Value);
		return KeyObj;
	}

	bool ParseRichCurveKeys(const TArray<TSharedPtr<FJsonValue>>& KeysJson, TArray<FRichCurveKey>& OutKeys, FString& OutError)
	{
		for (const TSharedPtr<FJsonValue>& KeyVal : KeysJson)
		{
			const TSharedPtr<FJsonObject>* KeyObjPtr = nullptr;
			if (!KeyVal->TryGetObject(KeyObjPtr) || !KeyObjPtr || !KeyObjPtr->IsValid())
			{
				OutError = TEXT("Each key must be a JSON object with 'time' and 'value' fields");
				return false;
			}
			const TSharedPtr<FJsonObject>& KeyObj = *KeyObjPtr;

			if (!KeyObj->HasField(TEXT("time")) || !KeyObj->HasField(TEXT("value")))
			{
				OutError = TEXT("Each key must have 'time' and 'value' fields");
				return false;
			}

			FRichCurveKey NewKey;
			NewKey.Time = static_cast<float>(KeyObj->GetNumberField(TEXT("time")));
			NewKey.Value = static_cast<float>(KeyObj->GetNumberField(TEXT("value")));
			NewKey.InterpMode = KeyObj->HasField(TEXT("interp_mode"))
				? ParseInterpMode(KeyObj->GetStringField(TEXT("interp_mode")))
				: RCIM_Linear;
			NewKey.TangentMode = KeyObj->HasField(TEXT("tangent_mode"))
				? ParseTangentMode(KeyObj->GetStringField(TEXT("tangent_mode")))
				: RCTM_Auto;
			if (KeyObj->HasField(TEXT("arrive_tangent")))
			{
				NewKey.ArriveTangent = static_cast<float>(KeyObj->GetNumberField(TEXT("arrive_tangent")));
			}
			if (KeyObj->HasField(TEXT("leave_tangent")))
			{
				NewKey.LeaveTangent = static_cast<float>(KeyObj->GetNumberField(TEXT("leave_tangent")));
			}

			OutKeys.Add(NewKey);
		}
		return true;
	}

	bool ParseSimpleCurveKeys(const TArray<TSharedPtr<FJsonValue>>& KeysJson, TArray<FSimpleCurveKey>& OutKeys, FString& OutError)
	{
		for (const TSharedPtr<FJsonValue>& KeyVal : KeysJson)
		{
			const TSharedPtr<FJsonObject>* KeyObjPtr = nullptr;
			if (!KeyVal->TryGetObject(KeyObjPtr) || !KeyObjPtr || !KeyObjPtr->IsValid())
			{
				OutError = TEXT("Each key must be a JSON object with 'time' and 'value' fields");
				return false;
			}
			const TSharedPtr<FJsonObject>& KeyObj = *KeyObjPtr;

			if (!KeyObj->HasField(TEXT("time")) || !KeyObj->HasField(TEXT("value")))
			{
				OutError = TEXT("Each key must have 'time' and 'value' fields");
				return false;
			}

			FSimpleCurveKey NewKey;
			NewKey.Time = static_cast<float>(KeyObj->GetNumberField(TEXT("time")));
			NewKey.Value = static_cast<float>(KeyObj->GetNumberField(TEXT("value")));
			OutKeys.Add(NewKey);
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// Asset load helper — returns nullptr with OutError populated on failure
	// -----------------------------------------------------------------------

	UCurveTable* LoadCurveTable(const FString& AssetPath, TSharedPtr<FJsonObject>& OutError)
	{
		UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
		if (!LoadedAsset)
		{
			OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
			return nullptr;
		}

		UCurveTable* Table = Cast<UCurveTable>(LoadedAsset);
		if (!Table)
		{
			OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Asset is not a UCurveTable: %s (is %s)"),
					*AssetPath, *LoadedAsset->GetClass()->GetName()));
			return nullptr;
		}

		return Table;
	}

	// -----------------------------------------------------------------------
	// Populate a row on a table — returns false (with OutError) on mode mismatch
	// -----------------------------------------------------------------------

	bool ApplyRichKeysToTable(UCurveTable* Table, const FName& RowName, const TArray<FRichCurveKey>& Keys, FString& OutError)
	{
		const ECurveTableMode Mode = Table->GetCurveTableMode();
		if (Mode == ECurveTableMode::SimpleCurves)
		{
			OutError = TEXT("Cannot write a RichCurve row: table is locked to SimpleCurves mode. Pass simple keys or use a different table.");
			return false;
		}

		FRichCurve& Curve = Table->AddRichCurve(RowName);
		Curve.SetKeys(Keys);
		return true;
	}

	bool ApplySimpleKeysToTable(UCurveTable* Table, const FName& RowName, const TArray<FSimpleCurveKey>& Keys, ERichCurveInterpMode InterpMode, FString& OutError)
	{
		const ECurveTableMode Mode = Table->GetCurveTableMode();
		if (Mode == ECurveTableMode::RichCurves)
		{
			OutError = TEXT("Cannot write a SimpleCurve row: table is locked to RichCurves mode. Pass rich keys or use a different table.");
			return false;
		}

		FSimpleCurve& Curve = Table->AddSimpleCurve(RowName);
		Curve.SetKeys(Keys);
		Curve.SetKeyInterpMode(InterpMode);
		return true;
	}
}

FEpicUnrealMCPCurveTableCommands::FEpicUnrealMCPCurveTableCommands()
{
}

FEpicUnrealMCPCurveTableCommands::~FEpicUnrealMCPCurveTableCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveTableCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("list_curve_tables"))       return HandleListCurveTables(Params);
	if (CommandType == TEXT("read_curve_table"))        return HandleReadCurveTable(Params);
	if (CommandType == TEXT("read_curve_table_row"))    return HandleReadCurveTableRow(Params);
	if (CommandType == TEXT("create_curve_table"))      return HandleCreateCurveTable(Params);
	if (CommandType == TEXT("update_curve_table_row"))  return HandleUpdateCurveTableRow(Params);
	if (CommandType == TEXT("delete_curve_table_row"))  return HandleDeleteCurveTableRow(Params);

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown CurveTable command: %s"), *CommandType));
}

// ============================================================================
// list_curve_tables
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveTableCommands::HandleListCurveTables(const TSharedPtr<FJsonObject>& Params)
{
	const FString Directory = Params->HasField(TEXT("directory"))
		? Params->GetStringField(TEXT("directory"))
		: TEXT("/Game/");

	const int32 Limit = Params->HasField(TEXT("limit"))
		? static_cast<int32>(Params->GetNumberField(TEXT("limit")))
		: 100;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Directory));
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UCurveTable::StaticClass()->GetClassPathName());

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
// read_curve_table
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveTableCommands::HandleReadCurveTable(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	TSharedPtr<FJsonObject> LoadError;
	UCurveTable* Table = LoadCurveTable(AssetPath, LoadError);
	if (!Table)
	{
		return LoadError;
	}

	const ECurveTableMode Mode = Table->GetCurveTableMode();

	TArray<TSharedPtr<FJsonValue>> RowsArray;
	int32 RowCount = 0;

	if (Mode == ECurveTableMode::RichCurves)
	{
		for (const auto& Pair : Table->GetRichCurveRowMap())
		{
			TSharedPtr<FJsonObject> RowObj = MakeShareable(new FJsonObject);
			RowObj->SetStringField(TEXT("name"), Pair.Key.ToString());

			TArray<TSharedPtr<FJsonValue>> KeysArray;
			if (Pair.Value)
			{
				for (const FRichCurveKey& Key : Pair.Value->GetConstRefOfKeys())
				{
					KeysArray.Add(MakeShareable(new FJsonValueObject(RichCurveKeyToJson(Key))));
				}
			}
			RowObj->SetArrayField(TEXT("keys"), KeysArray);
			RowsArray.Add(MakeShareable(new FJsonValueObject(RowObj)));
			++RowCount;
		}
	}
	else if (Mode == ECurveTableMode::SimpleCurves)
	{
		for (const auto& Pair : Table->GetSimpleCurveRowMap())
		{
			TSharedPtr<FJsonObject> RowObj = MakeShareable(new FJsonObject);
			RowObj->SetStringField(TEXT("name"), Pair.Key.ToString());
			if (Pair.Value)
			{
				RowObj->SetStringField(TEXT("interp_mode"), InterpModeToString(Pair.Value->GetKeyInterpMode()));
			}

			TArray<TSharedPtr<FJsonValue>> KeysArray;
			if (Pair.Value)
			{
				for (const FSimpleCurveKey& Key : Pair.Value->GetConstRefOfKeys())
				{
					KeysArray.Add(MakeShareable(new FJsonValueObject(SimpleCurveKeyToJson(Key))));
				}
			}
			RowObj->SetArrayField(TEXT("keys"), KeysArray);
			RowsArray.Add(MakeShareable(new FJsonValueObject(RowObj)));
			++RowCount;
		}
	}
	// Empty mode — no rows to serialize

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("curve_mode"), CurveTableModeToString(Mode));
	Result->SetNumberField(TEXT("row_count"), RowCount);
	Result->SetArrayField(TEXT("rows"), RowsArray);
	return Result;
}

// ============================================================================
// read_curve_table_row
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveTableCommands::HandleReadCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("row_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: row_name"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString RowNameStr = Params->GetStringField(TEXT("row_name"));
	const FName RowName(*RowNameStr);

	TSharedPtr<FJsonObject> LoadError;
	UCurveTable* Table = LoadCurveTable(AssetPath, LoadError);
	if (!Table)
	{
		return LoadError;
	}

	const ECurveTableMode Mode = Table->GetCurveTableMode();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("row_name"), RowNameStr);
	Result->SetStringField(TEXT("curve_mode"), CurveTableModeToString(Mode));

	TArray<TSharedPtr<FJsonValue>> KeysArray;

	if (Mode == ECurveTableMode::RichCurves)
	{
		FRichCurve* const* Found = Table->GetRichCurveRowMap().Find(RowName);
		if (!Found || !*Found)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Row not found: %s"), *RowNameStr));
		}
		for (const FRichCurveKey& Key : (*Found)->GetConstRefOfKeys())
		{
			KeysArray.Add(MakeShareable(new FJsonValueObject(RichCurveKeyToJson(Key))));
		}
	}
	else if (Mode == ECurveTableMode::SimpleCurves)
	{
		FSimpleCurve* const* Found = Table->GetSimpleCurveRowMap().Find(RowName);
		if (!Found || !*Found)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Row not found: %s"), *RowNameStr));
		}
		Result->SetStringField(TEXT("interp_mode"), InterpModeToString((*Found)->GetKeyInterpMode()));
		for (const FSimpleCurveKey& Key : (*Found)->GetConstRefOfKeys())
		{
			KeysArray.Add(MakeShareable(new FJsonValueObject(SimpleCurveKeyToJson(Key))));
		}
	}
	else
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Curve table is empty: %s"), *AssetPath));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("keys"), KeysArray);
	return Result;
}

// ============================================================================
// create_curve_table
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveTableCommands::HandleCreateCurveTable(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: name"));
	}
	if (!Params->HasField(TEXT("curve_mode")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: curve_mode ('RichCurves' or 'SimpleCurves')"));
	}

	const FString AssetName = Params->GetStringField(TEXT("name"));
	const FString ModeStr = Params->GetStringField(TEXT("curve_mode"));

	ECurveTableMode RequestedMode = ECurveTableMode::Empty;
	FString ParseError;
	if (!ParseCurveTableMode(ModeStr, RequestedMode, ParseError))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ParseError);
	}

	FString PackagePath = Params->HasField(TEXT("package_path"))
		? Params->GetStringField(TEXT("package_path"))
		: TEXT("/Game/Data/");
	if (!PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath += TEXT("/");
	}

	const FString FullPath = PackagePath + AssetName;
	if (FEpicUnrealMCPCommonUtils::AssetExists(FullPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset already exists: %s"), *FullPath));
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	UCurveTable* NewAsset = NewObject<UCurveTable>(
		Package, UCurveTable::StaticClass(), *AssetName, RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to construct UCurveTable"));
	}

	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

	TArray<FString> RowErrors;
	int32 RowsWritten = 0;

	if (Params->HasField(TEXT("rows")))
	{
		const TArray<TSharedPtr<FJsonValue>>& RowsArray = Params->GetArrayField(TEXT("rows"));
		for (const TSharedPtr<FJsonValue>& RowVal : RowsArray)
		{
			const TSharedPtr<FJsonObject>* RowObjPtr = nullptr;
			if (!RowVal->TryGetObject(RowObjPtr) || !RowObjPtr || !RowObjPtr->IsValid())
			{
				RowErrors.Add(TEXT("Row entry is not a JSON object"));
				continue;
			}
			const TSharedPtr<FJsonObject>& RowObj = *RowObjPtr;

			if (!RowObj->HasField(TEXT("name")) || !RowObj->HasField(TEXT("keys")))
			{
				RowErrors.Add(TEXT("Row missing 'name' or 'keys' field"));
				continue;
			}

			const FString RowNameStr = RowObj->GetStringField(TEXT("name"));
			const FName RowName(*RowNameStr);
			const TArray<TSharedPtr<FJsonValue>>& KeysJson = RowObj->GetArrayField(TEXT("keys"));

			if (RequestedMode == ECurveTableMode::RichCurves)
			{
				TArray<FRichCurveKey> Keys;
				FString KeyError;
				if (!ParseRichCurveKeys(KeysJson, Keys, KeyError))
				{
					RowErrors.Add(FString::Printf(TEXT("Row '%s': %s"), *RowNameStr, *KeyError));
					continue;
				}
				FString ApplyError;
				if (!ApplyRichKeysToTable(NewAsset, RowName, Keys, ApplyError))
				{
					RowErrors.Add(FString::Printf(TEXT("Row '%s': %s"), *RowNameStr, *ApplyError));
					continue;
				}
				++RowsWritten;
			}
			else // SimpleCurves
			{
				TArray<FSimpleCurveKey> Keys;
				FString KeyError;
				if (!ParseSimpleCurveKeys(KeysJson, Keys, KeyError))
				{
					RowErrors.Add(FString::Printf(TEXT("Row '%s': %s"), *RowNameStr, *KeyError));
					continue;
				}
				const ERichCurveInterpMode InterpMode = RowObj->HasField(TEXT("interp_mode"))
					? ParseInterpMode(RowObj->GetStringField(TEXT("interp_mode")))
					: RCIM_Linear;
				FString ApplyError;
				if (!ApplySimpleKeysToTable(NewAsset, RowName, Keys, InterpMode, ApplyError))
				{
					RowErrors.Add(FString::Printf(TEXT("Row '%s': %s"), *RowNameStr, *ApplyError));
					continue;
				}
				++RowsWritten;
			}
		}

		if (RowsWritten > 0)
		{
			Package->MarkPackageDirty();
		}
	}

	const bool bSave = !Params->HasField(TEXT("save")) || Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(NewAsset, /*bOnlyIfIsDirty=*/false);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_name"), AssetName);
	Result->SetStringField(TEXT("asset_path"), FullPath);
	Result->SetStringField(TEXT("curve_mode"), CurveTableModeToString(NewAsset->GetCurveTableMode()));
	Result->SetNumberField(TEXT("rows_written"), RowsWritten);
	if (RowErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorValues;
		for (const FString& Err : RowErrors)
		{
			ErrorValues.Add(MakeShareable(new FJsonValueString(Err)));
		}
		Result->SetArrayField(TEXT("row_errors"), ErrorValues);
	}
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}
	return Result;
}

// ============================================================================
// update_curve_table_row
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveTableCommands::HandleUpdateCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("row_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: row_name"));
	}
	if (!Params->HasField(TEXT("keys")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: keys"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString RowNameStr = Params->GetStringField(TEXT("row_name"));
	const FName RowName(*RowNameStr);
	const bool bCreateIfMissing = Params->HasField(TEXT("create_if_missing"))
		&& Params->GetBoolField(TEXT("create_if_missing"));

	TSharedPtr<FJsonObject> LoadError;
	UCurveTable* Table = LoadCurveTable(AssetPath, LoadError);
	if (!Table)
	{
		return LoadError;
	}

	ECurveTableMode Mode = Table->GetCurveTableMode();

	// For an Empty table, caller must specify the mode for the first row.
	if (Mode == ECurveTableMode::Empty)
	{
		if (!Params->HasField(TEXT("curve_mode")))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				TEXT("Curve table is empty; 'curve_mode' ('RichCurves' or 'SimpleCurves') is required to seed the first row"));
		}
		FString ParseError;
		if (!ParseCurveTableMode(Params->GetStringField(TEXT("curve_mode")), Mode, ParseError))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ParseError);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>& KeysJson = Params->GetArrayField(TEXT("keys"));

	if (Mode == ECurveTableMode::RichCurves)
	{
		FRichCurve* const* Found = Table->GetRichCurveRowMap().Find(RowName);
		if (!Found && !bCreateIfMissing)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Row not found: %s (pass create_if_missing=true to add it)"), *RowNameStr));
		}

		TArray<FRichCurveKey> Keys;
		FString KeyError;
		if (!ParseRichCurveKeys(KeysJson, Keys, KeyError))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(KeyError);
		}

		Table->Modify();
		if (!Found)
		{
			FString ApplyError;
			if (!ApplyRichKeysToTable(Table, RowName, Keys, ApplyError))
			{
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ApplyError);
			}
		}
		else
		{
			(*Found)->SetKeys(Keys);
		}
	}
	else // SimpleCurves
	{
		FSimpleCurve* const* Found = Table->GetSimpleCurveRowMap().Find(RowName);
		if (!Found && !bCreateIfMissing)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Row not found: %s (pass create_if_missing=true to add it)"), *RowNameStr));
		}

		TArray<FSimpleCurveKey> Keys;
		FString KeyError;
		if (!ParseSimpleCurveKeys(KeysJson, Keys, KeyError))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(KeyError);
		}
		const ERichCurveInterpMode InterpMode = Params->HasField(TEXT("interp_mode"))
			? ParseInterpMode(Params->GetStringField(TEXT("interp_mode")))
			: (Found && *Found ? (*Found)->GetKeyInterpMode() : RCIM_Linear);

		Table->Modify();
		if (!Found)
		{
			FString ApplyError;
			if (!ApplySimpleKeysToTable(Table, RowName, Keys, InterpMode, ApplyError))
			{
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ApplyError);
			}
		}
		else
		{
			(*Found)->SetKeys(Keys);
			(*Found)->SetKeyInterpMode(InterpMode);
		}
	}

	Table->MarkPackageDirty();

	const bool bSave = !Params->HasField(TEXT("save")) || Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(Table, /*bOnlyIfIsDirty=*/false);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("row_name"), RowNameStr);
	Result->SetStringField(TEXT("curve_mode"), CurveTableModeToString(Table->GetCurveTableMode()));
	Result->SetNumberField(TEXT("num_keys"), KeysJson.Num());
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}
	return Result;
}

// ============================================================================
// delete_curve_table_row
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveTableCommands::HandleDeleteCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("row_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: row_name"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString RowNameStr = Params->GetStringField(TEXT("row_name"));
	const FName RowName(*RowNameStr);

	TSharedPtr<FJsonObject> LoadError;
	UCurveTable* Table = LoadCurveTable(AssetPath, LoadError);
	if (!Table)
	{
		return LoadError;
	}

	if (!Table->GetRowMap().Contains(RowName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Row not found: %s"), *RowNameStr));
	}

	Table->Modify();
	Table->RemoveRow(RowName);
	Table->MarkPackageDirty();

	const bool bSave = !Params->HasField(TEXT("save")) || Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(Table, /*bOnlyIfIsDirty=*/false);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("row_name"), RowNameStr);
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}
	return Result;
}
