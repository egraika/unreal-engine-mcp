#include "Commands/EpicUnrealMCPDataTableCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPPropertyUtils.h"

#include "EditorAssetLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/CurveTable.h"
#include "Curves/RichCurve.h"
#include "Curves/SimpleCurve.h"

FEpicUnrealMCPDataTableCommands::FEpicUnrealMCPDataTableCommands()
{
}

FEpicUnrealMCPDataTableCommands::~FEpicUnrealMCPDataTableCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPDataTableCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("read_data_table"))
	{
		return HandleReadDataTable(Params);
	}
	else if (CommandType == TEXT("read_data_table_row"))
	{
		return HandleReadDataTableRow(Params);
	}
	else if (CommandType == TEXT("read_curve_table"))
	{
		return HandleReadCurveTable(Params);
	}
	else if (CommandType == TEXT("update_curve_table"))
	{
		return HandleUpdateCurveTable(Params);
	}
	else if (CommandType == TEXT("create_curve_table"))
	{
		return HandleCreateCurveTable(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown DataTable command: %s"), *CommandType));
}

// ============================================================================
// read_data_table
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataTableCommands::HandleReadDataTable(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const int32 Limit = Params->HasField(TEXT("limit")) ? static_cast<int32>(Params->GetNumberField(TEXT("limit"))) : 200;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	UDataTable* DataTable = Cast<UDataTable>(LoadedAsset);
	if (!DataTable)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UDataTable: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("DataTable has no row struct: %s"), *AssetPath));
	}

	// Build schema from row struct fields
	TArray<TSharedPtr<FJsonValue>> SchemaArray;
	for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		TSharedPtr<FJsonObject> FieldObj = MakeShareable(new FJsonObject);
		FieldObj->SetStringField(TEXT("name"), Property->GetName());
		FieldObj->SetStringField(TEXT("type"), Property->GetClass()->GetName());
		SchemaArray.Add(MakeShareable(new FJsonValueObject(FieldObj)));
	}

	// Iterate rows up to limit
	const TMap<FName, uint8*>& RowMap = DataTable->GetRowMap();
	const int32 TotalRows = RowMap.Num();
	const bool bLimitApplied = TotalRows > Limit;

	TSharedPtr<FJsonObject> RowsObj = MakeShareable(new FJsonObject);
	int32 Count = 0;
	for (const auto& Pair : RowMap)
	{
		if (Count >= Limit)
		{
			break;
		}

		const FName& RowName = Pair.Key;
		const uint8* RowData = Pair.Value;

		TSharedPtr<FJsonObject> SerializedRow = FEpicUnrealMCPPropertyUtils::SerializeStructProperties(
			const_cast<UScriptStruct*>(RowStruct), RowData);

		RowsObj->SetObjectField(RowName.ToString(), SerializedRow);
		++Count;
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("row_struct_name"), RowStruct->GetName());
	Result->SetArrayField(TEXT("schema"), SchemaArray);
	Result->SetObjectField(TEXT("rows"), RowsObj);
	Result->SetNumberField(TEXT("row_count"), TotalRows);
	Result->SetBoolField(TEXT("limit_applied"), bLimitApplied);

	return Result;
}

// ============================================================================
// read_data_table_row
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataTableCommands::HandleReadDataTableRow(const TSharedPtr<FJsonObject>& Params)
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
	const FString RowName = Params->GetStringField(TEXT("row_name"));

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	UDataTable* DataTable = Cast<UDataTable>(LoadedAsset);
	if (!DataTable)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UDataTable: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("DataTable has no row struct: %s"), *AssetPath));
	}

	const uint8* RowData = DataTable->FindRowUnchecked(FName(*RowName));
	if (!RowData)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Row not found: %s"), *RowName));
	}

	TSharedPtr<FJsonObject> SerializedRow = FEpicUnrealMCPPropertyUtils::SerializeStructProperties(
		const_cast<UScriptStruct*>(RowStruct), RowData);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("row_name"), RowName);
	Result->SetStringField(TEXT("row_struct_name"), RowStruct->GetName());
	Result->SetObjectField(TEXT("data"), SerializedRow);

	return Result;
}

// ============================================================================
// CurveTable helpers
// ============================================================================

namespace
{
	FString InterpModeToString(ERichCurveInterpMode Mode)
	{
		switch (Mode)
		{
		case RCIM_Linear:   return TEXT("Linear");
		case RCIM_Constant: return TEXT("Constant");
		case RCIM_Cubic:    return TEXT("Cubic");
		case RCIM_None:     return TEXT("None");
		default:            return TEXT("Unknown");
		}
	}

	ERichCurveInterpMode StringToInterpMode(const FString& Str)
	{
		if (Str == TEXT("Linear"))   return RCIM_Linear;
		if (Str == TEXT("Constant")) return RCIM_Constant;
		if (Str == TEXT("Cubic"))    return RCIM_Cubic;
		if (Str == TEXT("None"))     return RCIM_None;
		return RCIM_Linear; // default
	}

	FString TangentModeToString(ERichCurveTangentMode Mode)
	{
		switch (Mode)
		{
		case RCTM_Auto:      return TEXT("Auto");
		case RCTM_User:      return TEXT("User");
		case RCTM_Break:     return TEXT("Break");
		case RCTM_SmartAuto: return TEXT("SmartAuto");
		case RCTM_None:      return TEXT("None");
		default:             return TEXT("Unknown");
		}
	}

	ERichCurveTangentMode StringToTangentMode(const FString& Str)
	{
		if (Str == TEXT("Auto"))      return RCTM_Auto;
		if (Str == TEXT("User"))      return RCTM_User;
		if (Str == TEXT("Break"))     return RCTM_Break;
		if (Str == TEXT("SmartAuto")) return RCTM_SmartAuto;
		if (Str == TEXT("None"))      return RCTM_None;
		return RCTM_Auto; // default
	}

	TSharedPtr<FJsonObject> SerializeRichCurveKey(const FRichCurveKey& Key)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("time"), Key.Time);
		Obj->SetNumberField(TEXT("value"), Key.Value);
		Obj->SetStringField(TEXT("interp_mode"), InterpModeToString(Key.InterpMode));
		Obj->SetStringField(TEXT("tangent_mode"), TangentModeToString(Key.TangentMode));
		Obj->SetNumberField(TEXT("arrive_tangent"), Key.ArriveTangent);
		Obj->SetNumberField(TEXT("leave_tangent"), Key.LeaveTangent);
		return Obj;
	}

	TSharedPtr<FJsonObject> SerializeSimpleCurveKey(const FSimpleCurveKey& Key)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("time"), Key.Time);
		Obj->SetNumberField(TEXT("value"), Key.Value);
		return Obj;
	}
}

// ============================================================================
// read_curve_table
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataTableCommands::HandleReadCurveTable(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	// Optional: read only specific rows
	TArray<FString> RequestedRows;
	if (Params->HasField(TEXT("row_names")))
	{
		const TArray<TSharedPtr<FJsonValue>>& RowNamesJson = Params->GetArrayField(TEXT("row_names"));
		for (const auto& Val : RowNamesJson)
		{
			RequestedRows.Add(Val->AsString());
		}
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	UCurveTable* CurveTable = Cast<UCurveTable>(LoadedAsset);
	if (!CurveTable)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UCurveTable: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	const ECurveTableMode CurveMode = CurveTable->GetCurveTableMode();
	const bool bIsRich = (CurveMode == ECurveTableMode::RichCurves);
	const bool bIsSimple = (CurveMode == ECurveTableMode::SimpleCurves);

	// Serialize all rows
	TSharedPtr<FJsonObject> RowsObj = MakeShared<FJsonObject>();
	TArray<FString> RowNamesList;
	const TMap<FName, FRealCurve*>& RowMap = CurveTable->GetRowMap();

	for (const auto& Pair : RowMap)
	{
		const FString RowName = Pair.Key.ToString();

		// If specific rows requested, skip others
		if (RequestedRows.Num() > 0 && !RequestedRows.Contains(RowName))
		{
			continue;
		}

		RowNamesList.Add(RowName);
		TSharedPtr<FJsonObject> CurveObj = MakeShared<FJsonObject>();

		if (bIsRich)
		{
			const FRichCurve* RichCurve = static_cast<const FRichCurve*>(Pair.Value);
			TArray<TSharedPtr<FJsonValue>> KeysArray;
			for (const FRichCurveKey& Key : RichCurve->GetConstRefOfKeys())
			{
				KeysArray.Add(MakeShared<FJsonValueObject>(SerializeRichCurveKey(Key)));
			}
			CurveObj->SetArrayField(TEXT("keys"), KeysArray);
			CurveObj->SetNumberField(TEXT("default_value"), RichCurve->GetDefaultValue());
		}
		else if (bIsSimple)
		{
			const FSimpleCurve* SimpleCurve = static_cast<const FSimpleCurve*>(Pair.Value);
			TArray<TSharedPtr<FJsonValue>> KeysArray;
			for (const FSimpleCurveKey& Key : SimpleCurve->GetConstRefOfKeys())
			{
				KeysArray.Add(MakeShared<FJsonValueObject>(SerializeSimpleCurveKey(Key)));
			}
			CurveObj->SetArrayField(TEXT("keys"), KeysArray);
			CurveObj->SetNumberField(TEXT("default_value"), SimpleCurve->GetDefaultValue());
		}

		RowsObj->SetObjectField(RowName, CurveObj);
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("curve_mode"), bIsRich ? TEXT("RichCurves") : (bIsSimple ? TEXT("SimpleCurves") : TEXT("Empty")));
	Result->SetNumberField(TEXT("row_count"), RowMap.Num());
	Result->SetObjectField(TEXT("rows"), RowsObj);

	// Row names list for convenience
	TArray<TSharedPtr<FJsonValue>> NamesArray;
	for (const FString& Name : RowNamesList)
	{
		NamesArray.Add(MakeShared<FJsonValueString>(Name));
	}
	Result->SetArrayField(TEXT("row_names"), NamesArray);

	return Result;
}

// ============================================================================
// update_curve_table
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataTableCommands::HandleUpdateCurveTable(const TSharedPtr<FJsonObject>& Params)
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

	UCurveTable* CurveTable = Cast<UCurveTable>(LoadedAsset);
	if (!CurveTable)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UCurveTable: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	const ECurveTableMode CurveMode = CurveTable->GetCurveTableMode();
	const bool bIsRich = (CurveMode == ECurveTableMode::RichCurves);
	const bool bIsSimple = (CurveMode == ECurveTableMode::SimpleCurves);

	TArray<FString> Errors;
	TArray<FString> UpdatedRows;
	TArray<FString> RemovedRows;

	// Handle row removal
	if (Params->HasField(TEXT("remove_rows")))
	{
		const TArray<TSharedPtr<FJsonValue>>& RemoveArr = Params->GetArrayField(TEXT("remove_rows"));
		for (const auto& Val : RemoveArr)
		{
			const FString RowName = Val->AsString();
			FName RowFName(*RowName);
			FRealCurve* Existing = CurveTable->FindCurveUnchecked(RowFName);
			if (Existing)
			{
				CurveTable->DeleteRow(RowFName);
				RemovedRows.Add(RowName);
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Row '%s' not found for removal"), *RowName));
			}
		}
	}

	// Handle row add/update via "rows" object
	if (Params->HasField(TEXT("rows")))
	{
		const TSharedPtr<FJsonObject>* RowsPtr = nullptr;
		if (!Params->TryGetObjectField(TEXT("rows"), RowsPtr) || !RowsPtr)
		{
			Errors.Add(TEXT("'rows' must be a JSON object"));
		}
		else
		{
			for (const auto& RowPair : (*RowsPtr)->Values)
			{
				const FString& RowName = RowPair.Key;
				const TSharedPtr<FJsonObject>* CurveObjPtr = nullptr;
				if (!RowPair.Value->TryGetObject(CurveObjPtr) || !CurveObjPtr)
				{
					Errors.Add(FString::Printf(TEXT("Row '%s': expected JSON object"), *RowName));
					continue;
				}
				const TSharedPtr<FJsonObject>& CurveObj = *CurveObjPtr;

				if (bIsRich || CurveMode == ECurveTableMode::Empty)
				{
					// Find or create
					FRichCurve* RichCurve = CurveTable->FindRichCurve(FName(*RowName), TEXT("MCP"), /*bWarnIfNotFound=*/false);
					if (!RichCurve)
					{
						RichCurve = &CurveTable->AddRichCurve(FName(*RowName));
					}

					// Set default value if provided
					if (CurveObj->HasField(TEXT("default_value")))
					{
						RichCurve->SetDefaultValue(static_cast<float>(CurveObj->GetNumberField(TEXT("default_value"))));
					}

					// Set keys if provided
					if (CurveObj->HasField(TEXT("keys")))
					{
						const TArray<TSharedPtr<FJsonValue>>& KeysArr = CurveObj->GetArrayField(TEXT("keys"));
						TArray<FRichCurveKey> NewKeys;
						NewKeys.Reserve(KeysArr.Num());

						for (int32 i = 0; i < KeysArr.Num(); ++i)
						{
							const TSharedPtr<FJsonObject>* KeyObjPtr = nullptr;
							if (!KeysArr[i]->TryGetObject(KeyObjPtr) || !KeyObjPtr)
							{
								Errors.Add(FString::Printf(TEXT("Row '%s' key %d: expected JSON object"), *RowName, i));
								continue;
							}
							const TSharedPtr<FJsonObject>& KeyObj = *KeyObjPtr;

							FRichCurveKey CurveKey;
							CurveKey.Time = static_cast<float>(KeyObj->GetNumberField(TEXT("time")));
							CurveKey.Value = static_cast<float>(KeyObj->GetNumberField(TEXT("value")));

							if (KeyObj->HasField(TEXT("interp_mode")))
							{
								CurveKey.InterpMode = StringToInterpMode(KeyObj->GetStringField(TEXT("interp_mode")));
							}
							if (KeyObj->HasField(TEXT("tangent_mode")))
							{
								CurveKey.TangentMode = StringToTangentMode(KeyObj->GetStringField(TEXT("tangent_mode")));
							}
							if (KeyObj->HasField(TEXT("arrive_tangent")))
							{
								CurveKey.ArriveTangent = static_cast<float>(KeyObj->GetNumberField(TEXT("arrive_tangent")));
							}
							if (KeyObj->HasField(TEXT("leave_tangent")))
							{
								CurveKey.LeaveTangent = static_cast<float>(KeyObj->GetNumberField(TEXT("leave_tangent")));
							}

							NewKeys.Add(CurveKey);
						}

						RichCurve->SetKeys(NewKeys);
					}

					UpdatedRows.Add(RowName);
				}
				else if (bIsSimple)
				{
					FSimpleCurve* SimpleCurve = CurveTable->FindSimpleCurve(FName(*RowName), TEXT("MCP"), /*bWarnIfNotFound=*/false);
					if (!SimpleCurve)
					{
						SimpleCurve = &CurveTable->AddSimpleCurve(FName(*RowName));
					}

					// Set default value if provided
					if (CurveObj->HasField(TEXT("default_value")))
					{
						SimpleCurve->SetDefaultValue(static_cast<float>(CurveObj->GetNumberField(TEXT("default_value"))));
					}

					// Set interp mode for entire curve
					if (CurveObj->HasField(TEXT("interp_mode")))
					{
						SimpleCurve->SetKeyInterpMode(StringToInterpMode(CurveObj->GetStringField(TEXT("interp_mode"))));
					}

					// Set keys if provided
					if (CurveObj->HasField(TEXT("keys")))
					{
						const TArray<TSharedPtr<FJsonValue>>& KeysArr = CurveObj->GetArrayField(TEXT("keys"));
						TArray<FSimpleCurveKey> NewKeys;
						NewKeys.Reserve(KeysArr.Num());

						for (int32 i = 0; i < KeysArr.Num(); ++i)
						{
							const TSharedPtr<FJsonObject>* KeyObjPtr = nullptr;
							if (!KeysArr[i]->TryGetObject(KeyObjPtr) || !KeyObjPtr)
							{
								Errors.Add(FString::Printf(TEXT("Row '%s' key %d: expected JSON object"), *RowName, i));
								continue;
							}
							const TSharedPtr<FJsonObject>& KeyObj = *KeyObjPtr;

							FSimpleCurveKey CurveKey;
							CurveKey.Time = static_cast<float>(KeyObj->GetNumberField(TEXT("time")));
							CurveKey.Value = static_cast<float>(KeyObj->GetNumberField(TEXT("value")));
							NewKeys.Add(CurveKey);
						}

						SimpleCurve->SetKeys(NewKeys);
					}

					UpdatedRows.Add(RowName);
				}
			}
		}
	}

	// Mark dirty
	CurveTable->MarkPackageDirty();

	// Optionally save
	const bool bSave = Params->HasField(TEXT("save")) && Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), Errors.Num() == 0);
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	TArray<TSharedPtr<FJsonValue>> UpdatedArr;
	for (const FString& Name : UpdatedRows)
	{
		UpdatedArr.Add(MakeShared<FJsonValueString>(Name));
	}
	Result->SetArrayField(TEXT("updated_rows"), UpdatedArr);

	if (RemovedRows.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> RemovedArr;
		for (const FString& Name : RemovedRows)
		{
			RemovedArr.Add(MakeShared<FJsonValueString>(Name));
		}
		Result->SetArrayField(TEXT("removed_rows"), RemovedArr);
	}

	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorArr;
		for (const FString& Err : Errors)
		{
			ErrorArr.Add(MakeShared<FJsonValueString>(Err));
		}
		Result->SetArrayField(TEXT("errors"), ErrorArr);
	}

	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}

	return Result;
}

// ============================================================================
// create_curve_table
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataTableCommands::HandleCreateCurveTable(const TSharedPtr<FJsonObject>& Params)
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

	// Determine curve mode (default: RichCurves)
	ECurveTableMode CurveMode = ECurveTableMode::RichCurves;
	if (Params->HasField(TEXT("curve_mode")))
	{
		const FString ModeStr = Params->GetStringField(TEXT("curve_mode"));
		if (ModeStr == TEXT("SimpleCurves"))
		{
			CurveMode = ECurveTableMode::SimpleCurves;
		}
		else if (ModeStr != TEXT("RichCurves"))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid curve_mode: '%s'. Use 'RichCurves' or 'SimpleCurves'"), *ModeStr));
		}
	}

	// Split asset_path into package path and asset name
	FString PackagePath, AssetName;
	AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid asset_path format: %s"), *AssetPath));
	}

	// Create the package and CurveTable
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package for: %s"), *AssetPath));
	}

	UCurveTable* CurveTable = NewObject<UCurveTable>(Package, *AssetName, RF_Standalone | RF_Public);
	if (!CurveTable)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create CurveTable object"));
	}

	// Set the curve mode by adding+removing a typed curve (UCurveTable has no direct SetCurveTableMode)
	{
		FName DummyName(TEXT("__mcp_init__"));
		if (CurveMode == ECurveTableMode::SimpleCurves)
		{
			CurveTable->AddSimpleCurve(DummyName);
		}
		else
		{
			CurveTable->AddRichCurve(DummyName);
		}
		CurveTable->DeleteRow(DummyName);
	}

	// Populate initial rows if provided
	TArray<FString> Errors;
	TArray<FString> AddedRows;
	const bool bIsRich = (CurveMode == ECurveTableMode::RichCurves);

	if (Params->HasField(TEXT("rows")))
	{
		const TSharedPtr<FJsonObject>* RowsPtr = nullptr;
		if (!Params->TryGetObjectField(TEXT("rows"), RowsPtr) || !RowsPtr)
		{
			Errors.Add(TEXT("'rows' must be a JSON object"));
		}
		else
		{
			for (const auto& RowPair : (*RowsPtr)->Values)
			{
				const FString& RowName = RowPair.Key;
				const TSharedPtr<FJsonObject>* CurveObjPtr = nullptr;
				if (!RowPair.Value->TryGetObject(CurveObjPtr) || !CurveObjPtr)
				{
					Errors.Add(FString::Printf(TEXT("Row '%s': expected JSON object"), *RowName));
					continue;
				}
				const TSharedPtr<FJsonObject>& CurveObj = *CurveObjPtr;

				if (bIsRich)
				{
					FRichCurve& RichCurve = CurveTable->AddRichCurve(FName(*RowName));

					if (CurveObj->HasField(TEXT("default_value")))
					{
						RichCurve.SetDefaultValue(static_cast<float>(CurveObj->GetNumberField(TEXT("default_value"))));
					}

					if (CurveObj->HasField(TEXT("keys")))
					{
						const TArray<TSharedPtr<FJsonValue>>& KeysArr = CurveObj->GetArrayField(TEXT("keys"));
						TArray<FRichCurveKey> NewKeys;
						NewKeys.Reserve(KeysArr.Num());

						for (int32 i = 0; i < KeysArr.Num(); ++i)
						{
							const TSharedPtr<FJsonObject>* KeyObjPtr = nullptr;
							if (!KeysArr[i]->TryGetObject(KeyObjPtr) || !KeyObjPtr)
							{
								Errors.Add(FString::Printf(TEXT("Row '%s' key %d: expected JSON object"), *RowName, i));
								continue;
							}
							const TSharedPtr<FJsonObject>& KeyObj = *KeyObjPtr;

							FRichCurveKey CurveKey;
							CurveKey.Time = static_cast<float>(KeyObj->GetNumberField(TEXT("time")));
							CurveKey.Value = static_cast<float>(KeyObj->GetNumberField(TEXT("value")));

							if (KeyObj->HasField(TEXT("interp_mode")))
							{
								CurveKey.InterpMode = StringToInterpMode(KeyObj->GetStringField(TEXT("interp_mode")));
							}
							if (KeyObj->HasField(TEXT("tangent_mode")))
							{
								CurveKey.TangentMode = StringToTangentMode(KeyObj->GetStringField(TEXT("tangent_mode")));
							}
							if (KeyObj->HasField(TEXT("arrive_tangent")))
							{
								CurveKey.ArriveTangent = static_cast<float>(KeyObj->GetNumberField(TEXT("arrive_tangent")));
							}
							if (KeyObj->HasField(TEXT("leave_tangent")))
							{
								CurveKey.LeaveTangent = static_cast<float>(KeyObj->GetNumberField(TEXT("leave_tangent")));
							}

							NewKeys.Add(CurveKey);
						}

						RichCurve.SetKeys(NewKeys);
					}
				}
				else
				{
					FSimpleCurve& SimpleCurve = CurveTable->AddSimpleCurve(FName(*RowName));

					if (CurveObj->HasField(TEXT("default_value")))
					{
						SimpleCurve.SetDefaultValue(static_cast<float>(CurveObj->GetNumberField(TEXT("default_value"))));
					}

					if (CurveObj->HasField(TEXT("interp_mode")))
					{
						SimpleCurve.SetKeyInterpMode(StringToInterpMode(CurveObj->GetStringField(TEXT("interp_mode"))));
					}

					if (CurveObj->HasField(TEXT("keys")))
					{
						const TArray<TSharedPtr<FJsonValue>>& KeysArr = CurveObj->GetArrayField(TEXT("keys"));
						TArray<FSimpleCurveKey> NewKeys;
						NewKeys.Reserve(KeysArr.Num());

						for (int32 i = 0; i < KeysArr.Num(); ++i)
						{
							const TSharedPtr<FJsonObject>* KeyObjPtr = nullptr;
							if (!KeysArr[i]->TryGetObject(KeyObjPtr) || !KeyObjPtr)
							{
								Errors.Add(FString::Printf(TEXT("Row '%s' key %d: expected JSON object"), *RowName, i));
								continue;
							}
							const TSharedPtr<FJsonObject>& KeyObj = *KeyObjPtr;

							FSimpleCurveKey CurveKey;
							CurveKey.Time = static_cast<float>(KeyObj->GetNumberField(TEXT("time")));
							CurveKey.Value = static_cast<float>(KeyObj->GetNumberField(TEXT("value")));
							NewKeys.Add(CurveKey);
						}

						SimpleCurve.SetKeys(NewKeys);
					}
				}

				AddedRows.Add(RowName);
			}
		}
	}

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(CurveTable);
	Package->MarkPackageDirty();

	// Optionally save to disk
	const bool bSave = Params->HasField(TEXT("save")) && Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), Errors.Num() == 0);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), AssetName);
	Result->SetStringField(TEXT("curve_mode"), bIsRich ? TEXT("RichCurves") : TEXT("SimpleCurves"));
	Result->SetNumberField(TEXT("row_count"), AddedRows.Num());

	if (AddedRows.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> AddedArr;
		for (const FString& Name : AddedRows)
		{
			AddedArr.Add(MakeShared<FJsonValueString>(Name));
		}
		Result->SetArrayField(TEXT("added_rows"), AddedArr);
	}

	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorArr;
		for (const FString& Err : Errors)
		{
			ErrorArr.Add(MakeShared<FJsonValueString>(Err));
		}
		Result->SetArrayField(TEXT("errors"), ErrorArr);
	}

	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}

	return Result;
}
