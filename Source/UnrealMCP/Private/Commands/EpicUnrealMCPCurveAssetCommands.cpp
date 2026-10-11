#include "Commands/EpicUnrealMCPCurveAssetCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"

namespace
{
	FString CurveAsset_InterpModeToString(ERichCurveInterpMode Mode)
	{
		switch (Mode)
		{
			case RCIM_Constant: return TEXT("Constant");
			case RCIM_Linear:   return TEXT("Linear");
			case RCIM_Cubic:    return TEXT("Cubic");
			default:            return TEXT("Linear");
		}
	}

	ERichCurveInterpMode CurveAsset_ParseInterpMode(const FString& Mode)
	{
		if (Mode.Equals(TEXT("Constant"), ESearchCase::IgnoreCase)) return RCIM_Constant;
		if (Mode.Equals(TEXT("Linear"), ESearchCase::IgnoreCase))   return RCIM_Linear;
		if (Mode.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase))    return RCIM_Cubic;
		return RCIM_Linear;
	}

	ERichCurveTangentMode CurveAsset_ParseTangentMode(const FString& Mode)
	{
		if (Mode.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))  return RCTM_Auto;
		if (Mode.Equals(TEXT("User"), ESearchCase::IgnoreCase))  return RCTM_User;
		if (Mode.Equals(TEXT("Break"), ESearchCase::IgnoreCase)) return RCTM_Break;
		if (Mode.Equals(TEXT("None"), ESearchCase::IgnoreCase))  return RCTM_None;
		return RCTM_Auto;
	}

	FString CurveAsset_TangentModeToString(ERichCurveTangentMode Mode)
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

	TSharedPtr<FJsonObject> CurveAsset_RichCurveKeyToJson(const FRichCurveKey& Key)
	{
		TSharedPtr<FJsonObject> KeyObj = MakeShareable(new FJsonObject);
		KeyObj->SetNumberField(TEXT("time"), Key.Time);
		KeyObj->SetNumberField(TEXT("value"), Key.Value);
		KeyObj->SetStringField(TEXT("interp_mode"), CurveAsset_InterpModeToString(Key.InterpMode));
		KeyObj->SetStringField(TEXT("tangent_mode"), CurveAsset_TangentModeToString(Key.TangentMode));
		KeyObj->SetNumberField(TEXT("arrive_tangent"), Key.ArriveTangent);
		KeyObj->SetNumberField(TEXT("leave_tangent"), Key.LeaveTangent);
		return KeyObj;
	}

	bool CurveAsset_ParseRichCurveKeys(const TArray<TSharedPtr<FJsonValue>>& KeysJson, TArray<FRichCurveKey>& OutKeys, FString& OutError)
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
				? CurveAsset_ParseInterpMode(KeyObj->GetStringField(TEXT("interp_mode")))
				: RCIM_Linear;
			NewKey.TangentMode = KeyObj->HasField(TEXT("tangent_mode"))
				? CurveAsset_ParseTangentMode(KeyObj->GetStringField(TEXT("tangent_mode")))
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

	UCurveFloat* LoadCurveFloat(const FString& AssetPath, TSharedPtr<FJsonObject>& OutError)
	{
		UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
		if (!LoadedAsset)
		{
			OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
			return nullptr;
		}

		UCurveFloat* Curve = Cast<UCurveFloat>(LoadedAsset);
		if (!Curve)
		{
			OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Asset is not a UCurveFloat: %s (is %s)"),
					*AssetPath, *LoadedAsset->GetClass()->GetName()));
			return nullptr;
		}

		return Curve;
	}
}

FEpicUnrealMCPCurveAssetCommands::FEpicUnrealMCPCurveAssetCommands()
{
}

FEpicUnrealMCPCurveAssetCommands::~FEpicUnrealMCPCurveAssetCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveAssetCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("list_curve_floats"))   return HandleListCurveFloats(Params);
	if (CommandType == TEXT("read_curve_float"))    return HandleReadCurveFloat(Params);
	if (CommandType == TEXT("create_curve_float"))  return HandleCreateCurveFloat(Params);
	if (CommandType == TEXT("update_curve_float"))  return HandleUpdateCurveFloat(Params);

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown CurveAsset command: %s"), *CommandType));
}

// ============================================================================
// list_curve_floats
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveAssetCommands::HandleListCurveFloats(const TSharedPtr<FJsonObject>& Params)
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
	Filter.bRecursiveClasses = false;
	Filter.ClassPaths.Add(UCurveFloat::StaticClass()->GetClassPathName());

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
// read_curve_float
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveAssetCommands::HandleReadCurveFloat(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	TSharedPtr<FJsonObject> LoadError;
	UCurveFloat* Curve = LoadCurveFloat(AssetPath, LoadError);
	if (!Curve)
	{
		return LoadError;
	}

	TArray<TSharedPtr<FJsonValue>> KeysArray;
	for (const FRichCurveKey& Key : Curve->FloatCurve.GetConstRefOfKeys())
	{
		KeysArray.Add(MakeShareable(new FJsonValueObject(CurveAsset_RichCurveKeyToJson(Key))));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("num_keys"), KeysArray.Num());
	Result->SetArrayField(TEXT("keys"), KeysArray);
	return Result;
}

// ============================================================================
// create_curve_float
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveAssetCommands::HandleCreateCurveFloat(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: name"));
	}

	const FString AssetName = Params->GetStringField(TEXT("name"));

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

	UCurveFloat* NewAsset = NewObject<UCurveFloat>(
		Package, UCurveFloat::StaticClass(), *AssetName, RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to construct UCurveFloat"));
	}

	int32 KeysWritten = 0;
	if (Params->HasField(TEXT("keys")))
	{
		const TArray<TSharedPtr<FJsonValue>>& KeysJson = Params->GetArrayField(TEXT("keys"));
		TArray<FRichCurveKey> Keys;
		FString KeyError;
		if (!CurveAsset_ParseRichCurveKeys(KeysJson, Keys, KeyError))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(KeyError);
		}
		NewAsset->FloatCurve.SetKeys(Keys);
		KeysWritten = Keys.Num();
	}

	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

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
	Result->SetNumberField(TEXT("keys_written"), KeysWritten);
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}
	return Result;
}

// ============================================================================
// update_curve_float
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPCurveAssetCommands::HandleUpdateCurveFloat(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!Params->HasField(TEXT("keys")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: keys"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	TSharedPtr<FJsonObject> LoadError;
	UCurveFloat* Curve = LoadCurveFloat(AssetPath, LoadError);
	if (!Curve)
	{
		return LoadError;
	}

	const TArray<TSharedPtr<FJsonValue>>& KeysJson = Params->GetArrayField(TEXT("keys"));
	TArray<FRichCurveKey> Keys;
	FString KeyError;
	if (!CurveAsset_ParseRichCurveKeys(KeysJson, Keys, KeyError))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(KeyError);
	}

	Curve->Modify();
	Curve->FloatCurve.SetKeys(Keys);
	Curve->MarkPackageDirty();

	const bool bSave = !Params->HasField(TEXT("save")) || Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(Curve, /*bOnlyIfIsDirty=*/false);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("num_keys"), Keys.Num());
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}
	return Result;
}
