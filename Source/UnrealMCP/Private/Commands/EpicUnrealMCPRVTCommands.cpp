#include "Commands/EpicUnrealMCPRVTCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureEnum.h"
#include "UObject/UnrealType.h"

FEpicUnrealMCPRVTCommands::FEpicUnrealMCPRVTCommands()
{
}

FEpicUnrealMCPRVTCommands::~FEpicUnrealMCPRVTCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPRVTCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_rvt_info"))
	{
		return HandleGetRVTInfo(Params);
	}
	else if (CommandType == TEXT("set_rvt_properties"))
	{
		return HandleSetRVTProperties(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown RVT command: %s"), *CommandType));
}

// ============================================================================
// Enum helpers
// ============================================================================

FString FEpicUnrealMCPRVTCommands::MaterialTypeToString(int32 EnumValue)
{
	static const TMap<int32, FString> Names = {
		{0, TEXT("BaseColor")},
		{1, TEXT("Mask4")},
		{2, TEXT("BaseColor_Normal_Roughness")},
		{3, TEXT("BaseColor_Normal_Specular")},
		{4, TEXT("BaseColor_Normal_Specular_YCoCg")},
		{5, TEXT("BaseColor_Normal_Specular_Mask_YCoCg")},
		{6, TEXT("WorldHeight")},
		{7, TEXT("Displacement")},
	};
	const FString* Found = Names.Find(EnumValue);
	return Found ? *Found : TEXT("Unknown");
}

int32 FEpicUnrealMCPRVTCommands::StringToMaterialType(const FString& Str)
{
	static const TMap<FString, int32> Values = {
		{TEXT("BaseColor"), 0},
		{TEXT("Mask4"), 1},
		{TEXT("BaseColor_Normal_Roughness"), 2},
		{TEXT("BaseColor_Normal_Specular"), 3},
		{TEXT("BaseColor_Normal_Specular_YCoCg"), 4},
		{TEXT("BaseColor_Normal_Specular_Mask_YCoCg"), 5},
		{TEXT("WorldHeight"), 6},
		{TEXT("Displacement"), 7},
	};
	const int32* Found = Values.Find(Str);
	return Found ? *Found : -1;
}

// ============================================================================
// get_rvt_info
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPRVTCommands::HandleGetRVTInfo(const TSharedPtr<FJsonObject>& Params)
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

	URuntimeVirtualTexture* RVT = Cast<URuntimeVirtualTexture>(LoadedAsset);
	if (!RVT)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a URuntimeVirtualTexture: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), RVT->GetName());

	// Read raw values via reflection, computed values via public getters
	UClass* RVTClass = RVT->GetClass();

	// TileCount
	{
		FProperty* Prop = FindFProperty<FProperty>(RVTClass, TEXT("TileCount"));
		if (Prop)
		{
			int32 RawValue = 0;
			Prop->GetValue_InContainer(RVT, &RawValue);
			Result->SetNumberField(TEXT("tile_count_raw"), RawValue);
		}
		Result->SetNumberField(TEXT("tile_count"), RVT->GetTileCount());
	}

	// TileSize
	{
		FProperty* Prop = FindFProperty<FProperty>(RVTClass, TEXT("TileSize"));
		if (Prop)
		{
			int32 RawValue = 0;
			Prop->GetValue_InContainer(RVT, &RawValue);
			Result->SetNumberField(TEXT("tile_size_raw"), RawValue);
		}
		Result->SetNumberField(TEXT("tile_size"), RVT->GetTileSize());
	}

	// TileBorderSize
	{
		FProperty* Prop = FindFProperty<FProperty>(RVTClass, TEXT("TileBorderSize"));
		if (Prop)
		{
			int32 RawValue = 0;
			Prop->GetValue_InContainer(RVT, &RawValue);
			Result->SetNumberField(TEXT("tile_border_size_raw"), RawValue);
		}
		Result->SetNumberField(TEXT("tile_border_size"), RVT->GetTileBorderSize());
	}

	// MaterialType
	Result->SetStringField(TEXT("material_type"), MaterialTypeToString(static_cast<int32>(RVT->GetMaterialType())));

	// Bool properties
	Result->SetBoolField(TEXT("compress_textures"), RVT->GetCompressTextures());
	Result->SetBoolField(TEXT("use_low_quality_compression"), RVT->GetLQCompression());
	Result->SetBoolField(TEXT("clear_textures"), RVT->GetClearTextures());
	Result->SetBoolField(TEXT("single_physical_space"), RVT->GetSinglePhysicalSpace());
	Result->SetBoolField(TEXT("private_space"), RVT->GetPrivateSpace());
	Result->SetBoolField(TEXT("adaptive"), RVT->GetAdaptivePageTable());
	Result->SetBoolField(TEXT("continuous_update"), RVT->GetContinuousUpdate());

	// RemoveLowMips
	Result->SetNumberField(TEXT("remove_low_mips"), RVT->GetRemoveLowMips());

	// CustomMaterialData
	{
		FVector4f CMD = RVT->GetCustomMaterialData();
		TArray<TSharedPtr<FJsonValue>> CMDArray;
		CMDArray.Add(MakeShareable(new FJsonValueNumber(CMD.X)));
		CMDArray.Add(MakeShareable(new FJsonValueNumber(CMD.Y)));
		CMDArray.Add(MakeShareable(new FJsonValueNumber(CMD.Z)));
		CMDArray.Add(MakeShareable(new FJsonValueNumber(CMD.W)));
		Result->SetArrayField(TEXT("custom_material_data"), CMDArray);
	}

	// LODGroup
	{
		UEnum* TextureGroupEnum = StaticEnum<TextureGroup>();
		if (TextureGroupEnum)
		{
			FString GroupName = TextureGroupEnum->GetNameStringByValue(RVT->GetLODGroup());
			Result->SetStringField(TEXT("lod_group"), GroupName);
		}
	}

	// Computed values
	Result->SetNumberField(TEXT("size"), RVT->GetSize());
	Result->SetNumberField(TEXT("layer_count"), RVT->GetLayerCount());
	Result->SetNumberField(TEXT("page_table_size"), RVT->GetPageTableSize());

	// Priority
	{
		static const TMap<EVTProducerPriority, FString> PriorityNames = {
			{EVTProducerPriority::Lowest, TEXT("Lowest")},
			{EVTProducerPriority::Lower, TEXT("Lower")},
			{EVTProducerPriority::Low, TEXT("Low")},
			{EVTProducerPriority::BelowNormal, TEXT("BelowNormal")},
			{EVTProducerPriority::Normal, TEXT("Normal")},
			{EVTProducerPriority::AboveNormal, TEXT("AboveNormal")},
			{EVTProducerPriority::High, TEXT("High")},
			{EVTProducerPriority::Highest, TEXT("Highest")},
		};
		const FString* PrioName = PriorityNames.Find(RVT->GetPriority());
		Result->SetStringField(TEXT("priority"), PrioName ? *PrioName : TEXT("Normal"));
		Result->SetBoolField(TEXT("use_custom_priority"), RVT->GetUseCustomPriority());
	}

	return Result;
}

// ============================================================================
// set_rvt_properties
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPRVTCommands::HandleSetRVTProperties(const TSharedPtr<FJsonObject>& Params)
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

	URuntimeVirtualTexture* RVT = Cast<URuntimeVirtualTexture>(LoadedAsset);
	if (!RVT)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a URuntimeVirtualTexture: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	const TSharedPtr<FJsonObject>& Properties = Params->GetObjectField(TEXT("properties"));
	UClass* RVTClass = RVT->GetClass();

	TArray<FString> SetFields;
	TArray<FString> Errors;

	// Helper lambda: set an int32 property via reflection
	auto SetInt32Prop = [&](const FString& JsonKey, const FString& PropName)
	{
		if (Properties->HasField(JsonKey))
		{
			FProperty* Prop = FindFProperty<FProperty>(RVTClass, *PropName);
			if (Prop)
			{
				int32 Value = static_cast<int32>(Properties->GetNumberField(JsonKey));
				Prop->SetValue_InContainer(RVT, &Value);
				SetFields.Add(JsonKey);
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Property not found: %s"), *PropName));
			}
		}
	};

	// Helper lambda: set a bool property via reflection
	auto SetBoolProp = [&](const FString& JsonKey, const FString& PropName)
	{
		if (Properties->HasField(JsonKey))
		{
			FProperty* Prop = FindFProperty<FProperty>(RVTClass, *PropName);
			if (Prop)
			{
				bool Value = Properties->GetBoolField(JsonKey);
				Prop->SetValue_InContainer(RVT, &Value);
				SetFields.Add(JsonKey);
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Property not found: %s"), *PropName));
			}
		}
	};

	// Int32 properties
	SetInt32Prop(TEXT("tile_count"), TEXT("TileCount"));
	SetInt32Prop(TEXT("tile_size"), TEXT("TileSize"));
	SetInt32Prop(TEXT("tile_border_size"), TEXT("TileBorderSize"));
	SetInt32Prop(TEXT("remove_low_mips"), TEXT("RemoveLowMips"));

	// Bool properties
	SetBoolProp(TEXT("compress_textures"), TEXT("bCompressTextures"));
	SetBoolProp(TEXT("use_low_quality_compression"), TEXT("bUseLowQualityCompression"));
	SetBoolProp(TEXT("clear_textures"), TEXT("bClearTextures"));
	SetBoolProp(TEXT("single_physical_space"), TEXT("bSinglePhysicalSpace"));
	SetBoolProp(TEXT("private_space"), TEXT("bPrivateSpace"));
	SetBoolProp(TEXT("adaptive"), TEXT("bAdaptive"));
	SetBoolProp(TEXT("continuous_update"), TEXT("bContinuousUpdate"));
	SetBoolProp(TEXT("use_custom_priority"), TEXT("bUseCustomPriority"));

	// MaterialType (enum)
	if (Properties->HasField(TEXT("material_type")))
	{
		const FString TypeStr = Properties->GetStringField(TEXT("material_type"));
		int32 TypeValue = StringToMaterialType(TypeStr);
		if (TypeValue >= 0)
		{
			FProperty* Prop = FindFProperty<FProperty>(RVTClass, TEXT("MaterialType"));
			if (Prop)
			{
				ERuntimeVirtualTextureMaterialType EnumVal = static_cast<ERuntimeVirtualTextureMaterialType>(TypeValue);
				Prop->SetValue_InContainer(RVT, &EnumVal);
				SetFields.Add(TEXT("material_type"));
			}
			else
			{
				Errors.Add(TEXT("Property not found: MaterialType"));
			}
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("Unknown material_type: %s"), *TypeStr));
		}
	}

	// CustomMaterialData (FVector4f)
	if (Properties->HasField(TEXT("custom_material_data")))
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Properties->GetArrayField(TEXT("custom_material_data"));
		if (Arr.Num() == 4)
		{
			FProperty* Prop = FindFProperty<FProperty>(RVTClass, TEXT("CustomMaterialData"));
			if (Prop)
			{
				FVector4f NewValue(
					static_cast<float>(Arr[0]->AsNumber()),
					static_cast<float>(Arr[1]->AsNumber()),
					static_cast<float>(Arr[2]->AsNumber()),
					static_cast<float>(Arr[3]->AsNumber())
				);
				Prop->SetValue_InContainer(RVT, &NewValue);
				SetFields.Add(TEXT("custom_material_data"));
			}
			else
			{
				Errors.Add(TEXT("Property not found: CustomMaterialData"));
			}
		}
		else
		{
			Errors.Add(TEXT("custom_material_data must be an array of 4 floats"));
		}
	}

	// LODGroup (TextureGroup enum)
	if (Properties->HasField(TEXT("lod_group")))
	{
		const FString GroupStr = Properties->GetStringField(TEXT("lod_group"));
		UEnum* TextureGroupEnum = StaticEnum<TextureGroup>();
		if (TextureGroupEnum)
		{
			int64 EnumValue = TextureGroupEnum->GetValueByNameString(GroupStr);
			if (EnumValue != INDEX_NONE)
			{
				FProperty* Prop = FindFProperty<FProperty>(RVTClass, TEXT("LODGroup"));
				if (Prop)
				{
					TEnumAsByte<TextureGroup> GroupVal = static_cast<TextureGroup>(EnumValue);
					Prop->SetValue_InContainer(RVT, &GroupVal);
					SetFields.Add(TEXT("lod_group"));
				}
				else
				{
					Errors.Add(TEXT("Property not found: LODGroup"));
				}
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Unknown lod_group: %s"), *GroupStr));
			}
		}
	}

	// CustomPriority (EVTProducerPriority enum)
	if (Properties->HasField(TEXT("custom_priority")))
	{
		const FString PrioStr = Properties->GetStringField(TEXT("custom_priority"));
		static const TMap<FString, EVTProducerPriority> PriorityValues = {
			{TEXT("Lowest"), EVTProducerPriority::Lowest},
			{TEXT("Lower"), EVTProducerPriority::Lower},
			{TEXT("Low"), EVTProducerPriority::Low},
			{TEXT("BelowNormal"), EVTProducerPriority::BelowNormal},
			{TEXT("Normal"), EVTProducerPriority::Normal},
			{TEXT("AboveNormal"), EVTProducerPriority::AboveNormal},
			{TEXT("High"), EVTProducerPriority::High},
			{TEXT("Highest"), EVTProducerPriority::Highest},
		};
		const EVTProducerPriority* Found = PriorityValues.Find(PrioStr);
		if (Found)
		{
			FProperty* Prop = FindFProperty<FProperty>(RVTClass, TEXT("CustomPriority"));
			if (Prop)
			{
				EVTProducerPriority Val = *Found;
				Prop->SetValue_InContainer(RVT, &Val);
				SetFields.Add(TEXT("custom_priority"));
			}
			else
			{
				Errors.Add(TEXT("Property not found: CustomPriority"));
			}
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("Unknown custom_priority: %s"), *PrioStr));
		}
	}

	// Trigger internal updates
	if (SetFields.Num() > 0)
	{
#if WITH_EDITOR
		// PostEditChange() is public on UObject, calls PostEditChangeProperty internally
		RVT->PostEditChange();
#endif
		RVT->MarkPackageDirty();
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
