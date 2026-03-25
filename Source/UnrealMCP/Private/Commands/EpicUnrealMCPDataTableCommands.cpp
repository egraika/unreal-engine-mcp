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
