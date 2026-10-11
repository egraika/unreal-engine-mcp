#include "Commands/EpicUnrealMCPDataTableCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPPropertyUtils.h"

#include "EditorAssetLibrary.h"
#include "Engine/DataTable.h"

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
	else if (CommandType == TEXT("write_data_table_row"))
	{
		return HandleWriteDataTableRow(Params);
	}
	else if (CommandType == TEXT("write_data_table_rows"))
	{
		return HandleWriteDataTableRows(Params);
	}
	else if (CommandType == TEXT("delete_data_table_row"))
	{
		return HandleDeleteDataTableRow(Params);
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

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
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

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
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
// Shared helpers for row write path
// ============================================================================

namespace
{
	UDataTable* LoadDataTable(const FString& AssetPath, TSharedPtr<FJsonObject>& OutError)
	{
		UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
		if (!LoadedAsset)
		{
			OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
			return nullptr;
		}

		UDataTable* Table = Cast<UDataTable>(LoadedAsset);
		if (!Table)
		{
			OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Asset is not a UDataTable: %s (is %s)"),
					*AssetPath, *LoadedAsset->GetClass()->GetName()));
			return nullptr;
		}

		return Table;
	}

	// Apply JSON fields to an in-memory row buffer. Returns number of fields set.
	// Fields that fail are appended to OutFieldErrors.
	int32 ApplyFieldsToRow(UScriptStruct* RowStruct, void* RowData,
	                        const TSharedPtr<FJsonObject>& Fields,
	                        UObject* Owner,
	                        TArray<FString>& OutFieldErrors)
	{
		if (!Fields.IsValid())
		{
			return 0;
		}

		int32 FieldsSet = 0;
		for (const auto& Pair : Fields->Values)
		{
			FString FieldError;
			if (FEpicUnrealMCPCommonUtils::SetStructField(RowStruct, RowData, FString(*Pair.Key), Pair.Value, Owner, FieldError))
			{
				++FieldsSet;
				// Capture nested partial-failure warnings (TArray<FStruct> with one
				// failing TSoftObjectPtr or unregistered FGameplayTag, etc.) — the
				// dispatcher now populates OutErrorMessage on partial success.
				if (!FieldError.IsEmpty())
				{
					OutFieldErrors.Add(FString::Printf(TEXT("%s [partial]: %s"), *Pair.Key, *FieldError));
				}
			}
			else
			{
				OutFieldErrors.Add(FieldError.IsEmpty()
					? FString::Printf(TEXT("Failed to set field: %s"), *Pair.Key)
					: FieldError);
			}
		}
		return FieldsSet;
	}

	// Write one row onto the table. New rows are allocated via AddRow (deep copy);
	// existing rows are mutated in place. merge=true preserves untouched fields on update.
	// Returns per-row summary; appends per-field errors to OutFieldErrors.
	TSharedPtr<FJsonObject> WriteSingleRow(UDataTable* Table,
	                                        const FName& RowName,
	                                        const TSharedPtr<FJsonObject>& Fields,
	                                        bool bMerge,
	                                        bool bCreateIfMissing,
	                                        TArray<FString>& OutFieldErrors)
	{
		TSharedPtr<FJsonObject> RowResult = MakeShareable(new FJsonObject);
		RowResult->SetStringField(TEXT("row_name"), RowName.ToString());

		UScriptStruct* RowStruct = const_cast<UScriptStruct*>(Table->GetRowStruct());
		if (!RowStruct)
		{
			RowResult->SetBoolField(TEXT("success"), false);
			RowResult->SetStringField(TEXT("error"), TEXT("DataTable has no row struct"));
			return RowResult;
		}

		uint8* ExistingRowData = Table->FindRowUnchecked(RowName);
		const bool bExists = ExistingRowData != nullptr;

		if (!bExists && !bCreateIfMissing)
		{
			RowResult->SetBoolField(TEXT("success"), false);
			RowResult->SetStringField(TEXT("error"),
				FString::Printf(TEXT("Row not found: %s (pass create_if_missing=true to add it)"),
					*RowName.ToString()));
			return RowResult;
		}

		RowResult->SetBoolField(TEXT("existed"), bExists);

		if (bExists)
		{
			// Mutate in place. If merge=false, reset to defaults first so only the supplied
			// fields are populated (matching the feel of "replace row").
			if (!bMerge)
			{
				RowStruct->ClearScriptStruct(ExistingRowData);
			}

			const int32 FieldsSet = ApplyFieldsToRow(RowStruct, ExistingRowData, Fields, Table, OutFieldErrors);
			RowResult->SetBoolField(TEXT("success"), true);
			RowResult->SetNumberField(TEXT("fields_set"), FieldsSet);
			RowResult->SetBoolField(TEXT("created"), false);

			Table->HandleDataTableChanged(RowName);
			return RowResult;
		}

		// New row — allocate a scratch buffer, init defaults, apply fields, AddRow copies in.
		const int32 StructureSize = RowStruct->GetStructureSize();
		TArray<uint8> Buffer;
		Buffer.SetNumZeroed(StructureSize);
		RowStruct->InitializeStruct(Buffer.GetData());

		const int32 FieldsSet = ApplyFieldsToRow(RowStruct, Buffer.GetData(), Fields, Table, OutFieldErrors);

		Table->AddRow(RowName, Buffer.GetData(), RowStruct);

		RowStruct->DestroyStruct(Buffer.GetData());

		RowResult->SetBoolField(TEXT("success"), true);
		RowResult->SetNumberField(TEXT("fields_set"), FieldsSet);
		RowResult->SetBoolField(TEXT("created"), true);
		return RowResult;
	}
}

// ============================================================================
// write_data_table_row — create or update a single row
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataTableCommands::HandleWriteDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("row_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: row_name"));
	}
	if (!Params->HasField(TEXT("fields")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: fields (JSON object of row member values)"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString RowNameStr = Params->GetStringField(TEXT("row_name"));
	const FName RowName(*RowNameStr);
	const bool bCreateIfMissing = !Params->HasField(TEXT("create_if_missing"))
		|| Params->GetBoolField(TEXT("create_if_missing"));
	const bool bMerge = !Params->HasField(TEXT("merge"))
		|| Params->GetBoolField(TEXT("merge"));

	TSharedPtr<FJsonObject> LoadError;
	UDataTable* Table = LoadDataTable(AssetPath, LoadError);
	if (!Table)
	{
		return LoadError;
	}

	const TSharedPtr<FJsonObject> Fields = Params->GetObjectField(TEXT("fields"));
	if (!Fields.IsValid())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'fields' must be a JSON object"));
	}

	Table->Modify();

	TArray<FString> FieldErrors;
	TSharedPtr<FJsonObject> RowResult = WriteSingleRow(Table, RowName, Fields, bMerge, bCreateIfMissing, FieldErrors);

	if (!RowResult->GetBoolField(TEXT("success")))
	{
		return RowResult;
	}

	Table->MarkPackageDirty();

	const bool bSave = !Params->HasField(TEXT("save")) || Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(Table, /*bOnlyIfIsDirty=*/false);
	}

	// Build response — includes the post-write serialized row for verification.
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("row_name"), RowNameStr);
	Result->SetBoolField(TEXT("created"), RowResult->GetBoolField(TEXT("created")));
	Result->SetNumberField(TEXT("fields_set"), RowResult->GetNumberField(TEXT("fields_set")));
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}

	if (uint8* WrittenRow = Table->FindRowUnchecked(RowName))
	{
		TSharedPtr<FJsonObject> SerializedRow = FEpicUnrealMCPPropertyUtils::SerializeStructProperties(
			const_cast<UScriptStruct*>(Table->GetRowStruct()), WrittenRow);
		Result->SetObjectField(TEXT("data"), SerializedRow);
	}

	if (FieldErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Warnings;
		for (const FString& Err : FieldErrors)
		{
			Warnings.Add(MakeShareable(new FJsonValueString(Err)));
		}
		Result->SetArrayField(TEXT("field_warnings"), Warnings);
	}

	return Result;
}

// ============================================================================
// write_data_table_rows — bulk create/update
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataTableCommands::HandleWriteDataTableRows(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("rows")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: rows (object keyed by row name, each value a fields object)"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const bool bCreateIfMissing = !Params->HasField(TEXT("create_if_missing"))
		|| Params->GetBoolField(TEXT("create_if_missing"));
	const bool bMerge = !Params->HasField(TEXT("merge"))
		|| Params->GetBoolField(TEXT("merge"));
	const bool bClearFirst = Params->HasField(TEXT("clear_existing"))
		&& Params->GetBoolField(TEXT("clear_existing"));

	TSharedPtr<FJsonObject> LoadError;
	UDataTable* Table = LoadDataTable(AssetPath, LoadError);
	if (!Table)
	{
		return LoadError;
	}

	const TSharedPtr<FJsonObject> RowsObj = Params->GetObjectField(TEXT("rows"));
	if (!RowsObj.IsValid())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'rows' must be a JSON object keyed by row name"));
	}

	Table->Modify();

	if (bClearFirst)
	{
		Table->EmptyTable();
	}

	TArray<TSharedPtr<FJsonValue>> RowResults;
	TArray<FString> AllFieldErrors;
	int32 RowsCreated = 0;
	int32 RowsUpdated = 0;
	int32 RowsFailed = 0;

	for (const auto& Pair : RowsObj->Values)
	{
		const FName RowName(*Pair.Key);
		TSharedPtr<FJsonObject> FieldsObj;

		// Accept either a direct fields object or an envelope {"fields": {...}}.
		if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Entry = Pair.Value->AsObject();
			if (Entry->HasField(TEXT("fields")) && Entry->Values.Num() == 1)
			{
				FieldsObj = Entry->GetObjectField(TEXT("fields"));
			}
			else
			{
				FieldsObj = Entry;
			}
		}

		if (!FieldsObj.IsValid())
		{
			TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject);
			Err->SetStringField(TEXT("row_name"), Pair.Key);
			Err->SetBoolField(TEXT("success"), false);
			Err->SetStringField(TEXT("error"), TEXT("Row entry must be a JSON object of field values"));
			RowResults.Add(MakeShareable(new FJsonValueObject(Err)));
			++RowsFailed;
			continue;
		}

		TArray<FString> RowErrors;
		TSharedPtr<FJsonObject> RowResult = WriteSingleRow(Table, RowName, FieldsObj, bMerge, bCreateIfMissing, RowErrors);
		RowResults.Add(MakeShareable(new FJsonValueObject(RowResult)));

		if (RowResult->GetBoolField(TEXT("success")))
		{
			if (RowResult->GetBoolField(TEXT("created"))) ++RowsCreated;
			else ++RowsUpdated;
		}
		else
		{
			++RowsFailed;
		}

		for (const FString& Err : RowErrors)
		{
			AllFieldErrors.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *Err));
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
	Result->SetNumberField(TEXT("rows_created"), RowsCreated);
	Result->SetNumberField(TEXT("rows_updated"), RowsUpdated);
	Result->SetNumberField(TEXT("rows_failed"), RowsFailed);
	Result->SetArrayField(TEXT("row_results"), RowResults);
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}
	if (AllFieldErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Warnings;
		for (const FString& Err : AllFieldErrors)
		{
			Warnings.Add(MakeShareable(new FJsonValueString(Err)));
		}
		Result->SetArrayField(TEXT("field_warnings"), Warnings);
	}

	return Result;
}

// ============================================================================
// delete_data_table_row
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPDataTableCommands::HandleDeleteDataTableRow(const TSharedPtr<FJsonObject>& Params)
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
	UDataTable* Table = LoadDataTable(AssetPath, LoadError);
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
	Table->HandleDataTableChanged(RowName);
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
	Result->SetBoolField(TEXT("deleted"), true);
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}
	return Result;
}
