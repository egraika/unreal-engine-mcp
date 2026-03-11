#include "Commands/EpicUnrealMCPMPCCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "Materials/MaterialParameterCollection.h"

FEpicUnrealMCPMPCCommands::FEpicUnrealMCPMPCCommands()
{
}

FEpicUnrealMCPMPCCommands::~FEpicUnrealMCPMPCCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPMPCCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_mpc_parameters"))
	{
		return HandleGetMPCParameters(Params);
	}
	else if (CommandType == TEXT("set_mpc_parameters"))
	{
		return HandleSetMPCParameters(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown MPC command: %s"), *CommandType));
}

// ============================================================================
// get_mpc_parameters
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMPCCommands::HandleGetMPCParameters(const TSharedPtr<FJsonObject>& Params)
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

	UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(LoadedAsset);
	if (!MPC)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UMaterialParameterCollection: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), MPC->GetName());

	// Base/parent collection
	UMaterialParameterCollection* BaseMPC = MPC->GetBaseParameterCollection();
	if (BaseMPC)
	{
		Result->SetStringField(TEXT("base_collection"), BaseMPC->GetPathName());
	}

	// Scalar parameters
	TArray<TSharedPtr<FJsonValue>> ScalarArray;
	for (const FCollectionScalarParameter& Param : MPC->ScalarParameters)
	{
		TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
		ParamObj->SetStringField(TEXT("name"), Param.ParameterName.ToString());
		ParamObj->SetStringField(TEXT("id"), Param.Id.ToString());
		ParamObj->SetNumberField(TEXT("default_value"), Param.DefaultValue);
		ScalarArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
	}
	Result->SetArrayField(TEXT("scalar_parameters"), ScalarArray);

	// Vector parameters
	TArray<TSharedPtr<FJsonValue>> VectorArray;
	for (const FCollectionVectorParameter& Param : MPC->VectorParameters)
	{
		TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
		ParamObj->SetStringField(TEXT("name"), Param.ParameterName.ToString());
		ParamObj->SetStringField(TEXT("id"), Param.Id.ToString());

		TSharedPtr<FJsonObject> ColorObj = MakeShareable(new FJsonObject);
		ColorObj->SetNumberField(TEXT("r"), Param.DefaultValue.R);
		ColorObj->SetNumberField(TEXT("g"), Param.DefaultValue.G);
		ColorObj->SetNumberField(TEXT("b"), Param.DefaultValue.B);
		ColorObj->SetNumberField(TEXT("a"), Param.DefaultValue.A);
		ParamObj->SetObjectField(TEXT("default_value"), ColorObj);

		VectorArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
	}
	Result->SetArrayField(TEXT("vector_parameters"), VectorArray);

	Result->SetNumberField(TEXT("scalar_count"), MPC->ScalarParameters.Num());
	Result->SetNumberField(TEXT("vector_count"), MPC->VectorParameters.Num());

	return Result;
}

// ============================================================================
// set_mpc_parameters
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMPCCommands::HandleSetMPCParameters(const TSharedPtr<FJsonObject>& Params)
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

	UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(LoadedAsset);
	if (!MPC)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UMaterialParameterCollection: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	const bool bHasScalars = Params->HasField(TEXT("scalar_parameters"));
	const bool bHasVectors = Params->HasField(TEXT("vector_parameters"));

	if (!bHasScalars && !bHasVectors)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("At least one of scalar_parameters or vector_parameters must be provided"));
	}

	TArray<FString> SetFields;
	TArray<FString> Errors;

	// Process scalar parameters
	if (bHasScalars)
	{
		const TArray<TSharedPtr<FJsonValue>>& ScalarArr = Params->GetArrayField(TEXT("scalar_parameters"));
		for (const TSharedPtr<FJsonValue>& Entry : ScalarArr)
		{
			const TSharedPtr<FJsonObject>& ParamObj = Entry->AsObject();
			if (!ParamObj.IsValid() || !ParamObj->HasField(TEXT("name")) || !ParamObj->HasField(TEXT("value")))
			{
				Errors.Add(TEXT("Scalar parameter entry missing 'name' or 'value' field"));
				continue;
			}

			const FString ParamName = ParamObj->GetStringField(TEXT("name"));
			const float ParamValue = static_cast<float>(ParamObj->GetNumberField(TEXT("value")));

			bool bFound = false;
			const FName ParamFName(*ParamName);
			for (FCollectionScalarParameter& Param : MPC->ScalarParameters)
			{
				if (Param.ParameterName == ParamFName)
				{
					Param.DefaultValue = ParamValue;
					SetFields.Add(FString::Printf(TEXT("scalar:%s"), *ParamName));
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				Errors.Add(FString::Printf(TEXT("Scalar parameter not found: %s"), *ParamName));
			}
		}
	}

	// Process vector parameters
	if (bHasVectors)
	{
		const TArray<TSharedPtr<FJsonValue>>& VectorArr = Params->GetArrayField(TEXT("vector_parameters"));
		for (const TSharedPtr<FJsonValue>& Entry : VectorArr)
		{
			const TSharedPtr<FJsonObject>& ParamObj = Entry->AsObject();
			if (!ParamObj.IsValid() || !ParamObj->HasField(TEXT("name")))
			{
				Errors.Add(TEXT("Vector parameter entry missing 'name' field"));
				continue;
			}

			const FString ParamName = ParamObj->GetStringField(TEXT("name"));

			if (!ParamObj->HasField(TEXT("r")) || !ParamObj->HasField(TEXT("g")) ||
				!ParamObj->HasField(TEXT("b")) || !ParamObj->HasField(TEXT("a")))
			{
				Errors.Add(FString::Printf(TEXT("Vector parameter '%s' missing r/g/b/a fields"), *ParamName));
				continue;
			}

			FLinearColor Color;
			Color.R = static_cast<float>(ParamObj->GetNumberField(TEXT("r")));
			Color.G = static_cast<float>(ParamObj->GetNumberField(TEXT("g")));
			Color.B = static_cast<float>(ParamObj->GetNumberField(TEXT("b")));
			Color.A = static_cast<float>(ParamObj->GetNumberField(TEXT("a")));

			bool bFound = false;
			const FName ParamFName(*ParamName);
			for (FCollectionVectorParameter& Param : MPC->VectorParameters)
			{
				if (Param.ParameterName == ParamFName)
				{
					Param.DefaultValue = Color;
					SetFields.Add(FString::Printf(TEXT("vector:%s"), *ParamName));
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				Errors.Add(FString::Printf(TEXT("Vector parameter not found: %s"), *ParamName));
			}
		}
	}

	// Trigger updates
	if (SetFields.Num() > 0)
	{
#if WITH_EDITOR
		MPC->PostEditChange();
#endif
		MPC->MarkPackageDirty();
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Errors.Num() == 0);
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	TArray<TSharedPtr<FJsonValue>> SetArray;
	for (const FString& Field : SetFields)
	{
		SetArray.Add(MakeShareable(new FJsonValueString(Field)));
	}
	Result->SetArrayField(TEXT("fields_set"), SetArray);

	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		for (const FString& Err : Errors)
		{
			ErrorArray.Add(MakeShareable(new FJsonValueString(Err)));
		}
		Result->SetArrayField(TEXT("errors"), ErrorArray);
	}

	return Result;
}
