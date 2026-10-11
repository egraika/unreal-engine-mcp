#include "Commands/EpicUnrealMCPMaterialCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "MaterialEditingLibrary.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionStaticSwitch.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionMaterialAttributeLayers.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialFunction.h"

FEpicUnrealMCPMaterialCommands::FEpicUnrealMCPMaterialCommands()
{
}

FEpicUnrealMCPMaterialCommands::~FEpicUnrealMCPMaterialCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	// READ
	if (CommandType == TEXT("get_material_expressions"))      return HandleGetMaterialExpressions(Params);
	if (CommandType == TEXT("get_material_connections"))      return HandleGetMaterialConnections(Params);
	if (CommandType == TEXT("get_material_function_info"))    return HandleGetMaterialFunctionInfo(Params);
	if (CommandType == TEXT("get_material_stats"))            return HandleGetMaterialStats(Params);

	// WRITE
	if (CommandType == TEXT("material_create_expression"))    return HandleCreateExpression(Params);
	if (CommandType == TEXT("material_delete_expression"))    return HandleDeleteExpression(Params);
	if (CommandType == TEXT("material_connect_expressions"))  return HandleConnectExpressions(Params);
	if (CommandType == TEXT("material_connect_property"))     return HandleConnectProperty(Params);
	if (CommandType == TEXT("material_disconnect_input"))     return HandleDisconnectInput(Params);
	if (CommandType == TEXT("material_set_expression_position")) return HandleSetExpressionPosition(Params);
	if (CommandType == TEXT("material_set_expression_property")) return HandleSetExpressionProperty(Params);
	if (CommandType == TEXT("material_recompile"))            return HandleRecompile(Params);
	if (CommandType == TEXT("material_update_function"))      return HandleUpdateFunction(Params);
	if (CommandType == TEXT("material_save"))                 return HandleSave(Params);
	if (CommandType == TEXT("material_batch_edit"))           return HandleBatchEdit(Params);

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown material command: %s"), *CommandType));
}

// ============================================================================
// get_material_stats
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleGetMaterialStats(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("material_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: material_path"));
	}

	const FString MaterialPath = Params->GetStringField(TEXT("material_path"));
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(MaterialPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *MaterialPath));
	}

	UMaterial* Material = Cast<UMaterial>(LoadedAsset);
	if (!Material)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UMaterial: %s (is %s)"), *MaterialPath, *LoadedAsset->GetClass()->GetName()));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetStringField(TEXT("material_name"), Material->GetName());

	// Domain
	static const TMap<EMaterialDomain, FString> DomainNames = {
		{MD_Surface, TEXT("Surface")},
		{MD_DeferredDecal, TEXT("DeferredDecal")},
		{MD_LightFunction, TEXT("LightFunction")},
		{MD_Volume, TEXT("Volume")},
		{MD_PostProcess, TEXT("PostProcess")},
		{MD_UI, TEXT("UI")}
	};
	const FString* DomainName = DomainNames.Find(Material->MaterialDomain);
	Result->SetStringField(TEXT("domain"), DomainName ? *DomainName : TEXT("Unknown"));

	// Blend mode
	static const TMap<EBlendMode, FString> BlendModeNames = {
		{BLEND_Opaque, TEXT("Opaque")},
		{BLEND_Masked, TEXT("Masked")},
		{BLEND_Translucent, TEXT("Translucent")},
		{BLEND_Additive, TEXT("Additive")},
		{BLEND_Modulate, TEXT("Modulate")},
		{BLEND_AlphaComposite, TEXT("AlphaComposite")},
		{BLEND_AlphaHoldout, TEXT("AlphaHoldout")}
	};
	const FString* BlendName = BlendModeNames.Find(Material->BlendMode);
	Result->SetStringField(TEXT("blend_mode"), BlendName ? *BlendName : TEXT("Unknown"));

	// Shading model
	static const TMap<EMaterialShadingModel, FString> ShadingModelNames = {
		{MSM_Unlit, TEXT("Unlit")},
		{MSM_DefaultLit, TEXT("DefaultLit")},
		{MSM_Subsurface, TEXT("Subsurface")},
		{MSM_PreintegratedSkin, TEXT("PreintegratedSkin")},
		{MSM_ClearCoat, TEXT("ClearCoat")},
		{MSM_SubsurfaceProfile, TEXT("SubsurfaceProfile")},
		{MSM_TwoSidedFoliage, TEXT("TwoSidedFoliage")},
		{MSM_Hair, TEXT("Hair")},
		{MSM_Cloth, TEXT("Cloth")},
		{MSM_Eye, TEXT("Eye")},
		{MSM_SingleLayerWater, TEXT("SingleLayerWater")},
		{MSM_ThinTranslucent, TEXT("ThinTranslucent")},
		{MSM_Strata, TEXT("Strata")}
	};
	const FMaterialShadingModelField& ShadingModels = Material->GetShadingModels();
	TArray<TSharedPtr<FJsonValue>> ShadingModelArray;
	for (const auto& Pair : ShadingModelNames)
	{
		if (ShadingModels.HasShadingModel(Pair.Key))
		{
			ShadingModelArray.Add(MakeShareable(new FJsonValueString(Pair.Value)));
		}
	}
	Result->SetArrayField(TEXT("shading_models"), ShadingModelArray);

	Result->SetBoolField(TEXT("two_sided"), Material->IsTwoSided());
	Result->SetBoolField(TEXT("uses_material_attributes"), Material->bUseMaterialAttributes);
	Result->SetNumberField(TEXT("opacity_mask_clip_value"), Material->GetOpacityMaskClipValue());

#if WITH_EDITORONLY_DATA
	const auto& Expressions = Material->GetExpressions();
	Result->SetNumberField(TEXT("expression_count"), Expressions.Num());

	// Count expression types
	TMap<FString, int32> TypeCounts;
	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (Expr)
		{
			FString ClassName = Expr->GetClass()->GetName();
			ClassName.RemoveFromStart(TEXT("MaterialExpression"));
			TypeCounts.FindOrAdd(ClassName, 0)++;
		}
	}

	TSharedPtr<FJsonObject> TypeCountsJson = MakeShareable(new FJsonObject);
	for (const auto& Pair : TypeCounts)
	{
		TypeCountsJson->SetNumberField(Pair.Key, Pair.Value);
	}
	Result->SetObjectField(TEXT("expression_type_counts"), TypeCountsJson);

	// Count parameters
	int32 ScalarParams = 0, VectorParams = 0, TextureParams = 0, StaticSwitchParams = 0;
	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (!Expr) continue;
		if (Expr->IsA<UMaterialExpressionScalarParameter>()) ScalarParams++;
		else if (Expr->IsA<UMaterialExpressionVectorParameter>()) VectorParams++;
		else if (Expr->IsA<UMaterialExpressionTextureSampleParameter2D>()) TextureParams++;
		else if (Expr->IsA<UMaterialExpressionTextureObjectParameter>()) TextureParams++;
		else if (Expr->IsA<UMaterialExpressionStaticBoolParameter>()) StaticSwitchParams++;
		else if (Expr->IsA<UMaterialExpressionStaticSwitchParameter>()) StaticSwitchParams++;
	}

	TSharedPtr<FJsonObject> ParamCounts = MakeShareable(new FJsonObject);
	ParamCounts->SetNumberField(TEXT("scalar"), ScalarParams);
	ParamCounts->SetNumberField(TEXT("vector"), VectorParams);
	ParamCounts->SetNumberField(TEXT("texture"), TextureParams);
	ParamCounts->SetNumberField(TEXT("static_switch"), StaticSwitchParams);
	Result->SetObjectField(TEXT("parameter_counts"), ParamCounts);
#endif

	return Result;
}

// ============================================================================
// get_material_expressions
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleGetMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("material_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: material_path"));
	}

	const FString MaterialPath = Params->GetStringField(TEXT("material_path"));
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(MaterialPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *MaterialPath));
	}

	UMaterial* Material = Cast<UMaterial>(LoadedAsset);
	if (!Material)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UMaterial: %s (is %s)"), *MaterialPath, *LoadedAsset->GetClass()->GetName()));
	}

#if WITH_EDITORONLY_DATA
	const auto& Expressions = Material->GetExpressions();

	// Build index map
	TMap<UMaterialExpression*, int32> IndexMap;
	for (int32 i = 0; i < Expressions.Num(); i++)
	{
		if (Expressions[i])
		{
			IndexMap.Add(Expressions[i].Get(), i);
		}
	}

	// Serialize all expressions
	TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
	for (int32 i = 0; i < Expressions.Num(); i++)
	{
		if (Expressions[i])
		{
			TSharedPtr<FJsonObject> ExprJson = SerializeExpression(Expressions[i].Get(), i, IndexMap);
			ExpressionsArray.Add(MakeShareable(new FJsonValueObject(ExprJson)));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetStringField(TEXT("material_name"), Material->GetName());
	Result->SetNumberField(TEXT("expression_count"), Expressions.Num());
	Result->SetArrayField(TEXT("expressions"), ExpressionsArray);
	return Result;

#else
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Editor-only data not available"));
#endif
}

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::SerializeExpression(UMaterialExpression* Expr, int32 Index, const TMap<UMaterialExpression*, int32>& IndexMap)
{
	TSharedPtr<FJsonObject> ExprJson = MakeShareable(new FJsonObject);

	ExprJson->SetNumberField(TEXT("index"), Index);

	FString ClassName = Expr->GetClass()->GetName();
	ClassName.RemoveFromStart(TEXT("MaterialExpression"));
	ExprJson->SetStringField(TEXT("type"), ClassName);

	// Description
	FString Desc = Expr->GetDescription();
	if (!Desc.IsEmpty())
	{
		ExprJson->SetStringField(TEXT("description"), Desc);
	}

	// Editor position
	ExprJson->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
	ExprJson->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);

	// Inputs
	TArray<TSharedPtr<FJsonValue>> InputsArray;
	for (int32 i = 0; ; i++)
	{
		FExpressionInput* Input = Expr->GetInput(i);
		if (!Input)
		{
			break;
		}

		TSharedPtr<FJsonObject> InputJson = MakeShareable(new FJsonObject);
		InputJson->SetStringField(TEXT("name"), Expr->GetInputName(i).ToString());

		if (Input->Expression)
		{
			const int32* ConnectedIndex = IndexMap.Find(Input->Expression);
			if (ConnectedIndex)
			{
				InputJson->SetNumberField(TEXT("connected_to_index"), *ConnectedIndex);
				InputJson->SetNumberField(TEXT("connected_output_index"), Input->OutputIndex);
			}
		}

		InputsArray.Add(MakeShareable(new FJsonValueObject(InputJson)));
	}
	ExprJson->SetArrayField(TEXT("inputs"), InputsArray);

	// Outputs
	TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
	TArray<TSharedPtr<FJsonValue>> OutputsArray;
	for (int32 i = 0; i < Outputs.Num(); i++)
	{
		TSharedPtr<FJsonObject> OutputJson = MakeShareable(new FJsonObject);
		OutputJson->SetNumberField(TEXT("index"), i);
		OutputJson->SetStringField(TEXT("name"), Outputs[i].OutputName.ToString());
		OutputsArray.Add(MakeShareable(new FJsonValueObject(OutputJson)));
	}
	ExprJson->SetArrayField(TEXT("outputs"), OutputsArray);

	// Specialized properties
	TSharedPtr<FJsonObject> Props = MakeShareable(new FJsonObject);
	ExtractSpecializedProperties(Expr, Props);
	if (Props->Values.Num() > 0)
	{
		ExprJson->SetObjectField(TEXT("properties"), Props);
	}

	return ExprJson;
}

void FEpicUnrealMCPMaterialCommands::ExtractSpecializedProperties(UMaterialExpression* Expr, TSharedPtr<FJsonObject>& OutProps)
{
	// Constants
	if (UMaterialExpressionConstant* C = Cast<UMaterialExpressionConstant>(Expr))
	{
		OutProps->SetNumberField(TEXT("value"), C->R);
	}
	else if (UMaterialExpressionConstant3Vector* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
	{
		OutProps->SetStringField(TEXT("value"), FString::Printf(TEXT("(%f, %f, %f)"), C3->Constant.R, C3->Constant.G, C3->Constant.B));
	}
	else if (UMaterialExpressionConstant4Vector* C4 = Cast<UMaterialExpressionConstant4Vector>(Expr))
	{
		OutProps->SetStringField(TEXT("value"), FString::Printf(TEXT("(%f, %f, %f, %f)"), C4->Constant.R, C4->Constant.G, C4->Constant.B, C4->Constant.A));
	}
	else if (UMaterialExpressionConstant2Vector* C2 = Cast<UMaterialExpressionConstant2Vector>(Expr))
	{
		OutProps->SetStringField(TEXT("value"), FString::Printf(TEXT("(%f, %f)"), C2->R, C2->G));
	}
	// Scalar parameter
	else if (UMaterialExpressionScalarParameter* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
	{
		OutProps->SetStringField(TEXT("parameter_name"), SP->ParameterName.ToString());
		OutProps->SetStringField(TEXT("group"), SP->Group.ToString());
		OutProps->SetNumberField(TEXT("default_value"), SP->DefaultValue);
		OutProps->SetNumberField(TEXT("slider_min"), SP->SliderMin);
		OutProps->SetNumberField(TEXT("slider_max"), SP->SliderMax);
	}
	// Vector parameter
	else if (UMaterialExpressionVectorParameter* VP = Cast<UMaterialExpressionVectorParameter>(Expr))
	{
		OutProps->SetStringField(TEXT("parameter_name"), VP->ParameterName.ToString());
		OutProps->SetStringField(TEXT("group"), VP->Group.ToString());
		OutProps->SetStringField(TEXT("default_value"), FString::Printf(TEXT("(%f, %f, %f, %f)"), VP->DefaultValue.R, VP->DefaultValue.G, VP->DefaultValue.B, VP->DefaultValue.A));
	}
	// Static bool parameter
	else if (UMaterialExpressionStaticBoolParameter* SBP = Cast<UMaterialExpressionStaticBoolParameter>(Expr))
	{
		OutProps->SetStringField(TEXT("parameter_name"), SBP->ParameterName.ToString());
		OutProps->SetStringField(TEXT("group"), SBP->Group.ToString());
		OutProps->SetBoolField(TEXT("default_value"), SBP->DefaultValue);
	}
	// Static switch parameter
	else if (UMaterialExpressionStaticSwitchParameter* SSP = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
	{
		OutProps->SetStringField(TEXT("parameter_name"), SSP->ParameterName.ToString());
		OutProps->SetStringField(TEXT("group"), SSP->Group.ToString());
		OutProps->SetBoolField(TEXT("default_value"), SSP->DefaultValue);
	}
	// Texture sample (check parameter subclass first)
	else if (UMaterialExpressionTextureSampleParameter2D* TSP = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
	{
		OutProps->SetStringField(TEXT("parameter_name"), TSP->ParameterName.ToString());
		OutProps->SetStringField(TEXT("group"), TSP->Group.ToString());
		if (TSP->Texture)
		{
			OutProps->SetStringField(TEXT("texture_path"), TSP->Texture->GetPathName());
		}
	}
	else if (UMaterialExpressionTextureSample* TS = Cast<UMaterialExpressionTextureSample>(Expr))
	{
		if (TS->Texture)
		{
			OutProps->SetStringField(TEXT("texture_path"), TS->Texture->GetPathName());
		}
	}
	// Texture object parameter
	else if (UMaterialExpressionTextureObjectParameter* TOP = Cast<UMaterialExpressionTextureObjectParameter>(Expr))
	{
		OutProps->SetStringField(TEXT("parameter_name"), TOP->ParameterName.ToString());
		OutProps->SetStringField(TEXT("group"), TOP->Group.ToString());
		if (TOP->Texture)
		{
			OutProps->SetStringField(TEXT("texture_path"), TOP->Texture->GetPathName());
		}
	}
	else if (UMaterialExpressionTextureObject* TO = Cast<UMaterialExpressionTextureObject>(Expr))
	{
		if (TO->Texture)
		{
			OutProps->SetStringField(TEXT("texture_path"), TO->Texture->GetPathName());
		}
	}
	// Texture coordinate
	else if (UMaterialExpressionTextureCoordinate* TC = Cast<UMaterialExpressionTextureCoordinate>(Expr))
	{
		OutProps->SetNumberField(TEXT("coordinate_index"), TC->CoordinateIndex);
		OutProps->SetNumberField(TEXT("u_tiling"), TC->UTiling);
		OutProps->SetNumberField(TEXT("v_tiling"), TC->VTiling);
	}
	// Material function call
	else if (UMaterialExpressionMaterialFunctionCall* MFC = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
	{
		if (MFC->MaterialFunction)
		{
			OutProps->SetStringField(TEXT("function_path"), MFC->MaterialFunction->GetPathName());
			OutProps->SetStringField(TEXT("function_name"), MFC->MaterialFunction->GetName());
		}
	}
	// Function input
	else if (UMaterialExpressionFunctionInput* FI = Cast<UMaterialExpressionFunctionInput>(Expr))
	{
		OutProps->SetStringField(TEXT("input_name"), FI->InputName.ToString());
		OutProps->SetNumberField(TEXT("input_type"), static_cast<int32>(FI->InputType));
		OutProps->SetBoolField(TEXT("use_preview_as_default"), FI->bUsePreviewValueAsDefault);
	}
	// Function output
	else if (UMaterialExpressionFunctionOutput* FO = Cast<UMaterialExpressionFunctionOutput>(Expr))
	{
		OutProps->SetStringField(TEXT("output_name"), FO->OutputName.ToString());
	}
	// Component mask
	else if (UMaterialExpressionComponentMask* CM = Cast<UMaterialExpressionComponentMask>(Expr))
	{
		FString Mask;
		if (CM->R) Mask += TEXT("R");
		if (CM->G) Mask += TEXT("G");
		if (CM->B) Mask += TEXT("B");
		if (CM->A) Mask += TEXT("A");
		OutProps->SetStringField(TEXT("mask"), Mask);
	}
	// Static switch
	else if (UMaterialExpressionStaticSwitch* SS = Cast<UMaterialExpressionStaticSwitch>(Expr))
	{
		OutProps->SetBoolField(TEXT("default_value"), SS->DefaultValue);
	}
	// Custom HLSL
	else if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
	{
		OutProps->SetStringField(TEXT("code"), Custom->Code);
		OutProps->SetStringField(TEXT("output_type"), FString::FromInt(static_cast<int32>(Custom->OutputType)));
		OutProps->SetStringField(TEXT("custom_description"), Custom->Description);
	}
	// Comment
	else if (UMaterialExpressionComment* Comment = Cast<UMaterialExpressionComment>(Expr))
	{
		OutProps->SetStringField(TEXT("comment_text"), Comment->Text);
		OutProps->SetNumberField(TEXT("size_x"), Comment->SizeX);
		OutProps->SetNumberField(TEXT("size_y"), Comment->SizeY);
	}
	// MakeMaterialAttributes
	else if (UMaterialExpressionMakeMaterialAttributes* MakeMA = Cast<UMaterialExpressionMakeMaterialAttributes>(Expr))
	{
		TArray<TSharedPtr<FJsonValue>> ConnectedProps;
		auto CheckProp = [&](const FExpressionInput& Input, const FString& Name)
		{
			if (Input.Expression)
			{
				ConnectedProps.Add(MakeShareable(new FJsonValueString(Name)));
			}
		};
		CheckProp(MakeMA->BaseColor, TEXT("BaseColor"));
		CheckProp(MakeMA->Metallic, TEXT("Metallic"));
		CheckProp(MakeMA->Specular, TEXT("Specular"));
		CheckProp(MakeMA->Roughness, TEXT("Roughness"));
		CheckProp(MakeMA->Anisotropy, TEXT("Anisotropy"));
		CheckProp(MakeMA->Normal, TEXT("Normal"));
		CheckProp(MakeMA->Tangent, TEXT("Tangent"));
		CheckProp(MakeMA->EmissiveColor, TEXT("EmissiveColor"));
		CheckProp(MakeMA->Opacity, TEXT("Opacity"));
		CheckProp(MakeMA->OpacityMask, TEXT("OpacityMask"));
		CheckProp(MakeMA->WorldPositionOffset, TEXT("WorldPositionOffset"));
		CheckProp(MakeMA->SubsurfaceColor, TEXT("SubsurfaceColor"));
		CheckProp(MakeMA->ClearCoat, TEXT("ClearCoat"));
		CheckProp(MakeMA->ClearCoatRoughness, TEXT("ClearCoatRoughness"));
		CheckProp(MakeMA->AmbientOcclusion, TEXT("AmbientOcclusion"));
		CheckProp(MakeMA->Refraction, TEXT("Refraction"));
		CheckProp(MakeMA->PixelDepthOffset, TEXT("PixelDepthOffset"));
		CheckProp(MakeMA->ShadingModel, TEXT("ShadingModel"));
		CheckProp(MakeMA->Displacement, TEXT("Displacement"));
		OutProps->SetArrayField(TEXT("connected_properties"), ConnectedProps);
	}
	// SetMaterialAttributes
	else if (UMaterialExpressionSetMaterialAttributes* SetMA = Cast<UMaterialExpressionSetMaterialAttributes>(Expr))
	{
		TArray<TSharedPtr<FJsonValue>> AttrNames;
		for (int32 i = 0; i < SetMA->AttributeSetTypes.Num(); ++i)
		{
			FString Name = FMaterialAttributeDefinitionMap::GetAttributeName(SetMA->AttributeSetTypes[i]);
			AttrNames.Add(MakeShareable(new FJsonValueString(Name)));
		}
		OutProps->SetArrayField(TEXT("attribute_set_types"), AttrNames);
		OutProps->SetNumberField(TEXT("input_count"), SetMA->Inputs.Num());
	}
	// GetMaterialAttributes
	else if (UMaterialExpressionGetMaterialAttributes* GetMA = Cast<UMaterialExpressionGetMaterialAttributes>(Expr))
	{
		TArray<TSharedPtr<FJsonValue>> AttrNames;
		for (int32 i = 0; i < GetMA->AttributeGetTypes.Num(); ++i)
		{
			FString Name = FMaterialAttributeDefinitionMap::GetAttributeName(GetMA->AttributeGetTypes[i]);
			AttrNames.Add(MakeShareable(new FJsonValueString(Name)));
		}
		OutProps->SetArrayField(TEXT("attribute_get_types"), AttrNames);
	}
}

// ============================================================================
// get_material_connections
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleGetMaterialConnections(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("material_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: material_path"));
	}

	const FString MaterialPath = Params->GetStringField(TEXT("material_path"));
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(MaterialPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *MaterialPath));
	}

	UMaterial* Material = Cast<UMaterial>(LoadedAsset);
	if (!Material)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UMaterial: %s"), *MaterialPath));
	}

#if WITH_EDITORONLY_DATA
	// Build index map
	const auto& Expressions = Material->GetExpressions();
	TMap<UMaterialExpression*, int32> IndexMap;
	for (int32 i = 0; i < Expressions.Num(); i++)
	{
		if (Expressions[i])
		{
			IndexMap.Add(Expressions[i].Get(), i);
		}
	}

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor-only data available for this material"));
	}

	// Helper to serialize a material input connection
	auto SerializeInput = [&](const FExpressionInput& Input, const FString& PropertyName) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> PropJson = MakeShareable(new FJsonObject);
		PropJson->SetStringField(TEXT("property"), PropertyName);

		if (Input.Expression)
		{
			PropJson->SetBoolField(TEXT("connected"), true);
			const int32* ExprIndex = IndexMap.Find(Input.Expression);
			if (ExprIndex)
			{
				PropJson->SetNumberField(TEXT("expression_index"), *ExprIndex);
				FString ClassName = Input.Expression->GetClass()->GetName();
				ClassName.RemoveFromStart(TEXT("MaterialExpression"));
				PropJson->SetStringField(TEXT("expression_type"), ClassName);

				FString Desc = Input.Expression->GetDescription();
				if (!Desc.IsEmpty())
				{
					PropJson->SetStringField(TEXT("expression_description"), Desc);
				}
			}
			PropJson->SetNumberField(TEXT("output_index"), Input.OutputIndex);
		}
		else
		{
			PropJson->SetBoolField(TEXT("connected"), false);
		}
		return PropJson;
	};

	TArray<TSharedPtr<FJsonValue>> ConnectionsArray;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetBoolField(TEXT("uses_material_attributes"), Material->bUseMaterialAttributes);

	if (Material->bUseMaterialAttributes)
	{
		// Material Attributes mode: connections come from MakeMaterialAttributes
		// or SetMaterialAttributes nodes, not direct pins.

		// Report the terminal MaterialAttributes connection
		if (EditorData->MaterialAttributes.Expression)
		{
			TSharedPtr<FJsonObject> TerminalJson = MakeShareable(new FJsonObject);
			TerminalJson->SetStringField(TEXT("property"), TEXT("MaterialAttributes"));
			TerminalJson->SetBoolField(TEXT("connected"), true);
			const int32* TermIdx = IndexMap.Find(EditorData->MaterialAttributes.Expression);
			if (TermIdx)
			{
				TerminalJson->SetNumberField(TEXT("expression_index"), *TermIdx);
				FString TermClass = EditorData->MaterialAttributes.Expression->GetClass()->GetName();
				TermClass.RemoveFromStart(TEXT("MaterialExpression"));
				TerminalJson->SetStringField(TEXT("expression_type"), TermClass);
			}
			ConnectionsArray.Add(MakeShareable(new FJsonValueObject(TerminalJson)));
		}

		// Walk all expressions to find MakeMaterialAttributes and SetMaterialAttributes nodes
		for (const TObjectPtr<UMaterialExpression>& ExprPtr : Expressions)
		{
			if (!ExprPtr) continue;

			if (UMaterialExpressionMakeMaterialAttributes* MakeMA =
				Cast<UMaterialExpressionMakeMaterialAttributes>(ExprPtr.Get()))
			{
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->BaseColor, TEXT("BaseColor")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->Metallic, TEXT("Metallic")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->Specular, TEXT("Specular")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->Roughness, TEXT("Roughness")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->Anisotropy, TEXT("Anisotropy")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->Normal, TEXT("Normal")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->Tangent, TEXT("Tangent")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->EmissiveColor, TEXT("EmissiveColor")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->Opacity, TEXT("Opacity")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->OpacityMask, TEXT("OpacityMask")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->WorldPositionOffset, TEXT("WorldPositionOffset")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->SubsurfaceColor, TEXT("SubsurfaceColor")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->ClearCoat, TEXT("ClearCoat")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->ClearCoatRoughness, TEXT("ClearCoatRoughness")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->AmbientOcclusion, TEXT("AmbientOcclusion")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->Refraction, TEXT("Refraction")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->PixelDepthOffset, TEXT("PixelDepthOffset")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->ShadingModel, TEXT("ShadingModel")))));
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(MakeMA->Displacement, TEXT("Displacement")))));
			}
			else if (UMaterialExpressionSetMaterialAttributes* SetMA =
					 Cast<UMaterialExpressionSetMaterialAttributes>(ExprPtr.Get()))
			{
				// Inputs[0] is the pass-through MaterialAttributes input.
				// Inputs[1..N] correspond to AttributeSetTypes[0..N-1].
				for (int32 i = 0; i < SetMA->AttributeSetTypes.Num(); ++i)
				{
					const int32 InputIndex = i + 1;
					if (InputIndex < SetMA->Inputs.Num())
					{
						FString AttrName = FMaterialAttributeDefinitionMap::GetAttributeName(SetMA->AttributeSetTypes[i]);
						ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(SetMA->Inputs[InputIndex], AttrName))));
					}
				}
			}
		}
	}
	else
	{
		// Direct pin mode
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->BaseColor, TEXT("BaseColor")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->Metallic, TEXT("Metallic")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->Specular, TEXT("Specular")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->Roughness, TEXT("Roughness")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->Anisotropy, TEXT("Anisotropy")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->Normal, TEXT("Normal")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->Tangent, TEXT("Tangent")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->EmissiveColor, TEXT("EmissiveColor")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->Opacity, TEXT("Opacity")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->OpacityMask, TEXT("OpacityMask")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->WorldPositionOffset, TEXT("WorldPositionOffset")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->SubsurfaceColor, TEXT("SubsurfaceColor")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->AmbientOcclusion, TEXT("AmbientOcclusion")))));
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(SerializeInput(EditorData->PixelDepthOffset, TEXT("PixelDepthOffset")))));
	}

	Result->SetArrayField(TEXT("connections"), ConnectionsArray);

	// Also provide a filtered list of only connected properties
	TArray<TSharedPtr<FJsonValue>> ConnectedOnly;
	for (const auto& Conn : ConnectionsArray)
	{
		if (Conn->AsObject()->GetBoolField(TEXT("connected")))
		{
			ConnectedOnly.Add(Conn);
		}
	}
	Result->SetArrayField(TEXT("connected_outputs"), ConnectedOnly);

	return Result;

#else
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Editor-only data not available"));
#endif
}

// ============================================================================
// get_material_function_info
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleGetMaterialFunctionInfo(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("function_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: function_path"));
	}

	const FString FunctionPath = Params->GetStringField(TEXT("function_path"));
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(FunctionPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *FunctionPath));
	}

	UMaterialFunction* MatFunc = Cast<UMaterialFunction>(LoadedAsset);
	if (!MatFunc)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UMaterialFunction: %s (is %s)"), *FunctionPath, *LoadedAsset->GetClass()->GetName()));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("function_path"), FunctionPath);
	Result->SetStringField(TEXT("function_name"), MatFunc->GetName());
	Result->SetStringField(TEXT("description"), MatFunc->Description);
	Result->SetBoolField(TEXT("expose_to_library"), MatFunc->bExposeToLibrary);

	// Library categories
	TArray<TSharedPtr<FJsonValue>> CategoriesArray;
	for (const FText& Cat : MatFunc->LibraryCategoriesText)
	{
		CategoriesArray.Add(MakeShareable(new FJsonValueString(Cat.ToString())));
	}
	Result->SetArrayField(TEXT("library_categories"), CategoriesArray);

	// Get inputs and outputs via function expressions
#if WITH_EDITORONLY_DATA
	TArray<FFunctionExpressionInput> Inputs;
	TArray<FFunctionExpressionOutput> Outputs;
	MatFunc->GetInputsAndOutputs(Inputs, Outputs);

	TArray<TSharedPtr<FJsonValue>> InputsArray;
	for (const auto& Input : Inputs)
	{
		TSharedPtr<FJsonObject> InputJson = MakeShareable(new FJsonObject);
		if (Input.ExpressionInput)
		{
			InputJson->SetStringField(TEXT("name"), Input.ExpressionInput->InputName.ToString());
			InputJson->SetNumberField(TEXT("input_type"), static_cast<int32>(Input.ExpressionInput->InputType));
			InputJson->SetBoolField(TEXT("use_preview_as_default"), Input.ExpressionInput->bUsePreviewValueAsDefault);
		}
		InputJson->SetStringField(TEXT("id"), Input.ExpressionInputId.ToString());
		InputsArray.Add(MakeShareable(new FJsonValueObject(InputJson)));
	}
	Result->SetArrayField(TEXT("inputs"), InputsArray);

	TArray<TSharedPtr<FJsonValue>> OutputsArray;
	for (const auto& Output : Outputs)
	{
		TSharedPtr<FJsonObject> OutputJson = MakeShareable(new FJsonObject);
		if (Output.ExpressionOutput)
		{
			OutputJson->SetStringField(TEXT("name"), Output.ExpressionOutput->OutputName.ToString());
		}
		OutputJson->SetStringField(TEXT("id"), Output.ExpressionOutputId.ToString());
		OutputsArray.Add(MakeShareable(new FJsonValueObject(OutputJson)));
	}
	Result->SetArrayField(TEXT("outputs"), OutputsArray);

	// Internal expressions
	const auto FuncExpressions = MatFunc->GetExpressions();
	{
		TMap<UMaterialExpression*, int32> IndexMap;
		for (int32 i = 0; i < FuncExpressions.Num(); i++)
		{
			if (FuncExpressions[i])
			{
				IndexMap.Add(FuncExpressions[i].Get(), i);
			}
		}

		TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
		for (int32 i = 0; i < FuncExpressions.Num(); i++)
		{
			if (FuncExpressions[i])
			{
				TSharedPtr<FJsonObject> ExprJson = SerializeExpression(FuncExpressions[i].Get(), i, IndexMap);
				ExpressionsArray.Add(MakeShareable(new FJsonValueObject(ExprJson)));
			}
		}

		Result->SetNumberField(TEXT("expression_count"), FuncExpressions.Num());
		Result->SetArrayField(TEXT("expressions"), ExpressionsArray);
	}
#endif

	return Result;
}

// ============================================================================
// WRITE-side contract (applies to every material_* write command below):
//
//   1. Pre-flight: load asset, resolve indices/names against current live state.
//      Any failure returns a structured error naming WHICH check failed (e.g.,
//      "AssetNotLoadable", "ExpressionIndexOutOfRange", "InputNameNotFound",
//      "PinTypeMismatch", "ClassNotResolved").
//
//   2. Action: wrapped in FScopedTransaction so a failure cancels cleanly and
//      the editor's Undo stack stays consistent. UE supports nested transactions
//      — material_batch_edit uses one outer scope; sub-handlers open inner ones
//      that no-op the commit until the outer scope closes.
//
//   3. Post-condition: after the engine call, re-read live state to verify the
//      change actually took. UMaterialEditingLibrary::ConnectMaterialExpressions
//      famously returns true on partial failures, so we read the input back.
//      Verification failure returns a structured error WITHOUT silently
//      reporting success.
//
//   4. PostEditChange + MarkPackageDirty on every mutation so derived state
//      stays consistent and the asset is flagged for save.
// ============================================================================

namespace EpicUnrealMCPMaterialLocal
{
	// PRK feedback_unity_anon_namespace_collision: named namespace, not anonymous.

	// --- Asset resolution ---

	// Resolves either material_path or function_path. Exactly one must be set.
	// Returns true with one of OutMaterial/OutFunction non-null, false (with
	// OutError set) on resolution failure.
	bool ResolveMaterialOrFunction(
		const TSharedPtr<FJsonObject>& Params,
		UMaterial*& OutMaterial,
		UMaterialFunction*& OutFunction,
		FString& OutError)
	{
		OutMaterial = nullptr;
		OutFunction = nullptr;

		const bool bHasMat = Params->HasField(TEXT("material_path"));
		const bool bHasFn  = Params->HasField(TEXT("function_path"));
		if (bHasMat == bHasFn)
		{
			OutError = TEXT("MissingOrAmbiguousTarget: exactly one of material_path or function_path must be provided");
			return false;
		}

		const FString Path = bHasMat
			? Params->GetStringField(TEXT("material_path"))
			: Params->GetStringField(TEXT("function_path"));

		UObject* Loaded = UEditorAssetLibrary::LoadAsset(Path);
		if (!Loaded)
		{
			OutError = FString::Printf(TEXT("AssetNotLoadable: %s"), *Path);
			return false;
		}

		if (bHasMat)
		{
			OutMaterial = Cast<UMaterial>(Loaded);
			if (!OutMaterial)
			{
				OutError = FString::Printf(TEXT("AssetClassMismatch: %s is %s, expected UMaterial"),
					*Path, *Loaded->GetClass()->GetName());
				return false;
			}
		}
		else
		{
			OutFunction = Cast<UMaterialFunction>(Loaded);
			if (!OutFunction)
			{
				OutError = FString::Printf(TEXT("AssetClassMismatch: %s is %s, expected UMaterialFunction"),
					*Path, *Loaded->GetClass()->GetName());
				return false;
			}
		}
		return true;
	}

#if WITH_EDITORONLY_DATA
	// UMaterial::GetExpressions and UMaterialFunction::GetExpressions return
	// different concrete container types (TArray<TObjectPtr<...>> vs
	// TArrayView<const TObjectPtr<...>>) — normalize to raw pointers so the
	// rest of the helpers don't care which one they came from.
	TArray<UMaterialExpression*> GetExpressions(UMaterial* Mat, UMaterialFunction* Fn)
	{
		TArray<UMaterialExpression*> Result;
		auto Append = [&](auto&& View)
		{
			Result.Reserve(View.Num());
			for (const auto& E : View) Result.Add(E.Get());
		};
		if (Mat) Append(Mat->GetExpressions());
		else if (Fn) Append(Fn->GetExpressions());
		return Result;
	}
#endif

	UMaterialExpression* GetExpressionByIndex(UMaterial* Mat, UMaterialFunction* Fn, int32 Index, FString& OutError)
	{
#if WITH_EDITORONLY_DATA
		const TArray<UMaterialExpression*> Exprs = GetExpressions(Mat, Fn);
		if (Index < 0 || Index >= Exprs.Num())
		{
			OutError = FString::Printf(TEXT("ExpressionIndexOutOfRange: %d (have %d expressions)"), Index, Exprs.Num());
			return nullptr;
		}
		UMaterialExpression* Expr = Exprs[Index];
		if (!Expr)
		{
			OutError = FString::Printf(TEXT("ExpressionAtIndexNull: %d"), Index);
			return nullptr;
		}
		return Expr;
#else
		OutError = TEXT("EditorOnlyDataUnavailable");
		return nullptr;
#endif
	}

	int32 GetExpressionIndex(UMaterial* Mat, UMaterialFunction* Fn, UMaterialExpression* Target)
	{
#if WITH_EDITORONLY_DATA
		const TArray<UMaterialExpression*> Exprs = GetExpressions(Mat, Fn);
		for (int32 i = 0; i < Exprs.Num(); ++i)
		{
			if (Exprs[i] == Target) return i;
		}
#endif
		return INDEX_NONE;
	}

	// --- Expression-class resolver (cached) ---
	// Accepts: bare name ("Constant3Vector"), prefixed ("MaterialExpressionConstant3Vector"),
	// or full class path ("/Script/Engine.MaterialExpressionConstant3Vector").
	static TMap<FName, TWeakObjectPtr<UClass>> GExpressionClassCache;

	UClass* ResolveExpressionClass(const FString& InClassName, FString& OutError)
	{
		const FName Key(*InClassName);
		if (const TWeakObjectPtr<UClass>* Cached = GExpressionClassCache.Find(Key))
		{
			if (UClass* C = Cached->Get()) return C;
			GExpressionClassCache.Remove(Key);
		}

		// Full path form: load directly.
		if (InClassName.StartsWith(TEXT("/Script/")))
		{
			if (UClass* C = LoadObject<UClass>(nullptr, *InClassName))
			{
				if (C->IsChildOf(UMaterialExpression::StaticClass()))
				{
					GExpressionClassCache.Add(Key, C);
					return C;
				}
				OutError = FString::Printf(TEXT("ClassNotMaterialExpression: %s"), *InClassName);
				return nullptr;
			}
		}

		// Bare or prefixed name: scan UMaterialExpression subclasses by short name.
		const FString StrippedName = InClassName.StartsWith(TEXT("MaterialExpression"))
			? InClassName
			: FString::Printf(TEXT("MaterialExpression%s"), *InClassName);

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (!C->IsChildOf(UMaterialExpression::StaticClass())) continue;
			if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
			if (C->GetName().Equals(StrippedName, ESearchCase::IgnoreCase))
			{
				GExpressionClassCache.Add(Key, C);
				return C;
			}
		}

		OutError = FString::Printf(TEXT("ClassNotResolved: '%s' (tried '%s'); pass /Script/Engine.MaterialExpressionFoo for fully-qualified lookup"),
			*InClassName, *StrippedName);
		return nullptr;
	}

	// --- Pin lookups ---

	// Returns input index for the named input, or INDEX_NONE.
	// If InputName is empty, returns 0 (matches UMaterialEditingLibrary convention).
	int32 FindInputIndexByName(UMaterialExpression* Expr, const FString& InputName)
	{
		if (InputName.IsEmpty()) return 0;
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = Expr->GetInput(i);
			if (!Input) break;
			if (Expr->GetInputName(i).ToString().Equals(InputName, ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	int32 FindOutputIndexByName(UMaterialExpression* Expr, const FString& OutputName)
	{
		if (OutputName.IsEmpty()) return 0;
		TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
		for (int32 i = 0; i < Outputs.Num(); ++i)
		{
			if (Outputs[i].OutputName.ToString().Equals(OutputName, ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	// EMaterialValueType bitmask compatibility — connection valid iff types overlap.
	// MCT_Float covers MCT_Float1..4; MaterialAttributes is exclusive.
	bool ArePinTypesCompatible(EMaterialValueType OutType, EMaterialValueType InType)
	{
		// MaterialAttributes only connects to MaterialAttributes.
		const bool bOutIsAttrs = (OutType & MCT_MaterialAttributes) != 0;
		const bool bInIsAttrs  = (InType  & MCT_MaterialAttributes) != 0;
		if (bOutIsAttrs != bInIsAttrs) return false;
		return (static_cast<uint32>(OutType) & static_cast<uint32>(InType)) != 0;
	}

	// --- Function input/output ID repair ---
	// Per PRK memory feedback_function_input_output_id: NewObject doesn't populate
	// FGuid Id on FunctionInput/Output; without ConditionallyGenerateId(true) the
	// MaterialFunctionCall aliases every input to input 0 on save/load.
	// UMaterialEditingLibrary::CreateMaterialExpressionInFunction should populate
	// the Id, but verify and repair if not.
	void EnsureFunctionInputOutputIdValid(UMaterialExpression* Expr)
	{
		if (UMaterialExpressionFunctionInput* FI = Cast<UMaterialExpressionFunctionInput>(Expr))
		{
			if (!FI->Id.IsValid())
			{
				FI->ConditionallyGenerateId(true);
			}
		}
		else if (UMaterialExpressionFunctionOutput* FO = Cast<UMaterialExpressionFunctionOutput>(Expr))
		{
			if (!FO->Id.IsValid())
			{
				FO->ConditionallyGenerateId(true);
			}
		}
	}

	// Convenience: build a success-with-context response payload.
	TSharedPtr<FJsonObject> MakeSuccess()
	{
		TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
		R->SetBoolField(TEXT("success"), true);
		return R;
	}

	// Structured error response with stable code field for caller branching.
	TSharedPtr<FJsonObject> MakeStructuredError(const FString& Code, const FString& Detail)
	{
		TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
		R->SetBoolField(TEXT("success"), false);
		R->SetStringField(TEXT("error_code"), Code);
		R->SetStringField(TEXT("message"), Detail);
		return R;
	}

	// --- EMaterialProperty parser (for connect_property) ---
	bool ParseMaterialProperty(const FString& Name, EMaterialProperty& OutProp, FString& OutError)
	{
		static const TMap<FString, EMaterialProperty> Map = {
			{TEXT("BaseColor"),            MP_BaseColor},
			{TEXT("Metallic"),             MP_Metallic},
			{TEXT("Specular"),             MP_Specular},
			{TEXT("Roughness"),            MP_Roughness},
			{TEXT("Anisotropy"),           MP_Anisotropy},
			{TEXT("EmissiveColor"),        MP_EmissiveColor},
			{TEXT("Opacity"),              MP_Opacity},
			{TEXT("OpacityMask"),          MP_OpacityMask},
			{TEXT("Normal"),               MP_Normal},
			{TEXT("Tangent"),              MP_Tangent},
			{TEXT("WorldPositionOffset"),  MP_WorldPositionOffset},
			{TEXT("Displacement"),         MP_Displacement},
			{TEXT("SubsurfaceColor"),      MP_SubsurfaceColor},
			{TEXT("ClearCoat"),            MP_CustomData0},
			{TEXT("ClearCoatRoughness"),   MP_CustomData1},
			{TEXT("AmbientOcclusion"),     MP_AmbientOcclusion},
			{TEXT("Refraction"),           MP_Refraction},
			{TEXT("PixelDepthOffset"),     MP_PixelDepthOffset},
			{TEXT("ShadingModel"),         MP_ShadingModel},
			{TEXT("MaterialAttributes"),   MP_MaterialAttributes},
			{TEXT("FrontMaterial"),        MP_FrontMaterial}
		};
		if (const EMaterialProperty* Found = Map.Find(Name))
		{
			OutProp = *Found;
			return true;
		}
		TArray<FString> Keys; Map.GetKeys(Keys);
		OutError = FString::Printf(TEXT("UnknownMaterialProperty: '%s'; valid: %s"),
			*Name, *FString::Join(Keys, TEXT(", ")));
		return false;
	}

	// Save asset to disk. Returns true on success.
	bool SaveAsset(UObject* Asset)
	{
		if (!Asset) return false;
		return UEditorAssetLibrary::SaveLoadedAsset(Asset, /*bOnlyIfIsDirty=*/false);
	}
} // namespace EpicUnrealMCPMaterialLocal

// ============================================================================
// material_create_expression
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleCreateExpression(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	if (!Params->HasField(TEXT("expression_class")))
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("expression_class required"));
	}

	UMaterial* Mat = nullptr; UMaterialFunction* Fn = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, Mat, Fn, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	const FString ClassName = Params->GetStringField(TEXT("expression_class"));
	FString ClassErr;
	UClass* ExprClass = ResolveExpressionClass(ClassName, ClassErr);
	if (!ExprClass)
	{
		return MakeStructuredError(TEXT("ClassNotResolved"), ClassErr);
	}

	const int32 PosX = Params->HasField(TEXT("x")) ? static_cast<int32>(Params->GetNumberField(TEXT("x"))) : 0;
	const int32 PosY = Params->HasField(TEXT("y")) ? static_cast<int32>(Params->GetNumberField(TEXT("y"))) : 0;

	FScopedTransaction Transaction(NSLOCTEXT("MaterialMCP", "CreateExpression", "MCP: Create Material Expression"));

	UMaterialExpression* NewExpr = nullptr;
	if (Mat)
	{
		NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass, PosX, PosY);
	}
	else
	{
		NewExpr = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Fn, ExprClass, PosX, PosY);
	}

	if (!NewExpr)
	{
		Transaction.Cancel();
		return MakeStructuredError(TEXT("CreationReturnedNull"),
			FString::Printf(TEXT("UMaterialEditingLibrary::CreateMaterialExpression returned null for class %s"), *ExprClass->GetName()));
	}

	// Post-condition: verify it's in the expressions array AND repair FunctionInput/Output Id if needed.
	const int32 NewIndex = GetExpressionIndex(Mat, Fn, NewExpr);
	if (NewIndex == INDEX_NONE)
	{
		Transaction.Cancel();
		return MakeStructuredError(TEXT("CreationVerificationFailed"),
			TEXT("Expression created but not found in target's expressions array"));
	}

	EnsureFunctionInputOutputIdValid(NewExpr);

	(Mat ? Mat->GetPackage() : Fn->GetPackage())->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetNumberField(TEXT("expression_index"), NewIndex);
	Result->SetStringField(TEXT("expression_class"), ExprClass->GetName());
	Result->SetNumberField(TEXT("x"), NewExpr->MaterialExpressionEditorX);
	Result->SetNumberField(TEXT("y"), NewExpr->MaterialExpressionEditorY);
	return Result;
}

// ============================================================================
// material_delete_expression
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleDeleteExpression(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	if (!Params->HasField(TEXT("expression_index")))
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("expression_index required"));
	}

	UMaterial* Mat = nullptr; UMaterialFunction* Fn = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, Mat, Fn, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	const int32 Index = static_cast<int32>(Params->GetNumberField(TEXT("expression_index")));
	FString LookupErr;
	UMaterialExpression* Target = GetExpressionByIndex(Mat, Fn, Index, LookupErr);
	if (!Target)
	{
		return MakeStructuredError(TEXT("ExpressionLookupFailed"), LookupErr);
	}

	if (!Target->CanUserDeleteExpression())
	{
		return MakeStructuredError(TEXT("ExpressionNotDeletable"),
			FString::Printf(TEXT("Expression at index %d (%s) refuses user deletion"), Index, *Target->GetClass()->GetName()));
	}

#if WITH_EDITORONLY_DATA
	// Collect referencing expressions before delete so caller knows what got auto-disconnected.
	TArray<int32> ReferencingIndices;
	const TArray<UMaterialExpression*> Exprs = GetExpressions(Mat, Fn);
	for (int32 i = 0; i < Exprs.Num(); ++i)
	{
		UMaterialExpression* Other = Exprs[i];
		if (!Other || Other == Target) continue;
		for (int32 InputIdx = 0; ; ++InputIdx)
		{
			FExpressionInput* In = Other->GetInput(InputIdx);
			if (!In) break;
			if (In->Expression == Target)
			{
				ReferencingIndices.Add(i);
				break;
			}
		}
	}
#endif

	FScopedTransaction Transaction(NSLOCTEXT("MaterialMCP", "DeleteExpression", "MCP: Delete Material Expression"));

	if (Mat) UMaterialEditingLibrary::DeleteMaterialExpression(Mat, Target);
	else     UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Fn, Target);

	// Post-condition: verify removal.
	if (GetExpressionIndex(Mat, Fn, Target) != INDEX_NONE)
	{
		Transaction.Cancel();
		return MakeStructuredError(TEXT("DeleteVerificationFailed"),
			FString::Printf(TEXT("Expression still present in array after delete: index %d"), Index));
	}

	(Mat ? Mat->GetPackage() : Fn->GetPackage())->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetNumberField(TEXT("deleted_index"), Index);
	TArray<TSharedPtr<FJsonValue>> RefArray;
	for (int32 i : ReferencingIndices) RefArray.Add(MakeShareable(new FJsonValueNumber(i)));
	Result->SetArrayField(TEXT("auto_disconnected_from_expression_indices"), RefArray);
	return Result;
}

// ============================================================================
// material_connect_expressions
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleConnectExpressions(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	for (const TCHAR* Required : { TEXT("from_index"), TEXT("to_index") })
	{
		if (!Params->HasField(Required))
		{
			return MakeStructuredError(TEXT("MissingField"), FString::Printf(TEXT("%s required"), Required));
		}
	}

	UMaterial* Mat = nullptr; UMaterialFunction* Fn = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, Mat, Fn, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	const int32 FromIdx = static_cast<int32>(Params->GetNumberField(TEXT("from_index")));
	const int32 ToIdx   = static_cast<int32>(Params->GetNumberField(TEXT("to_index")));
	const FString FromOutputName = Params->HasField(TEXT("from_output_name")) ? Params->GetStringField(TEXT("from_output_name")) : FString();
	const FString ToInputName    = Params->HasField(TEXT("to_input_name"))    ? Params->GetStringField(TEXT("to_input_name"))    : FString();

	FString LookupErr;
	UMaterialExpression* From = GetExpressionByIndex(Mat, Fn, FromIdx, LookupErr);
	if (!From) return MakeStructuredError(TEXT("FromExpressionLookupFailed"), LookupErr);
	UMaterialExpression* To = GetExpressionByIndex(Mat, Fn, ToIdx, LookupErr);
	if (!To)   return MakeStructuredError(TEXT("ToExpressionLookupFailed"),   LookupErr);

	const int32 InputIdx  = FindInputIndexByName(To, ToInputName);
	if (InputIdx == INDEX_NONE)
	{
		return MakeStructuredError(TEXT("InputNameNotFound"),
			FString::Printf(TEXT("Input '%s' not found on expression index %d (%s)"),
				*ToInputName, ToIdx, *To->GetClass()->GetName()));
	}
	const int32 OutputIdx = FindOutputIndexByName(From, FromOutputName);
	if (OutputIdx == INDEX_NONE)
	{
		return MakeStructuredError(TEXT("OutputNameNotFound"),
			FString::Printf(TEXT("Output '%s' not found on expression index %d (%s)"),
				*FromOutputName, FromIdx, *From->GetClass()->GetName()));
	}

	const EMaterialValueType OutType = From->GetOutputValueType(OutputIdx);
	const EMaterialValueType InType  = To->GetInputValueType(InputIdx);
	const bool bTypesCompatible = ArePinTypesCompatible(OutType, InType);
	if (!bTypesCompatible)
	{
		return MakeStructuredError(TEXT("PinTypeMismatch"),
			FString::Printf(TEXT("Output type 0x%X (%s.%d) does not overlap input type 0x%X (%s.%d)"),
				static_cast<uint32>(OutType), *From->GetClass()->GetName(), OutputIdx,
				static_cast<uint32>(InType),  *To->GetClass()->GetName(),   InputIdx));
	}

	FScopedTransaction Transaction(NSLOCTEXT("MaterialMCP", "ConnectExpressions", "MCP: Connect Material Expressions"));

	const bool bLibraryOk = UMaterialEditingLibrary::ConnectMaterialExpressions(
		From, FromOutputName, To, ToInputName);
	if (!bLibraryOk)
	{
		Transaction.Cancel();
		return MakeStructuredError(TEXT("ConnectionRejected"),
			TEXT("UMaterialEditingLibrary::ConnectMaterialExpressions returned false"));
	}

	// Post-condition: read the input back and verify.
	FExpressionInput* VerifyInput = To->GetInput(InputIdx);
	if (!VerifyInput || VerifyInput->Expression != From || VerifyInput->OutputIndex != OutputIdx)
	{
		Transaction.Cancel();
		return MakeStructuredError(TEXT("ConnectionVerificationFailed"),
			FString::Printf(TEXT("After connect, input %d on To expression does not reference From at output %d (actual: expr=%s outputIdx=%d)"),
				InputIdx, OutputIdx,
				VerifyInput && VerifyInput->Expression ? *VerifyInput->Expression->GetName() : TEXT("null"),
				VerifyInput ? VerifyInput->OutputIndex : -1));
	}

	(Mat ? Mat->GetPackage() : Fn->GetPackage())->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetNumberField(TEXT("from_index"),        FromIdx);
	Result->SetNumberField(TEXT("from_output_index"), OutputIdx);
	Result->SetNumberField(TEXT("to_index"),          ToIdx);
	Result->SetNumberField(TEXT("to_input_index"),    InputIdx);
	return Result;
}

// ============================================================================
// material_connect_property — connect expression output to a material property pin
// (BaseColor, Roughness, etc.). Only valid for UMaterial (not UMaterialFunction).
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleConnectProperty(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	if (!Params->HasField(TEXT("material_path")))
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("material_path required (connect_property is for base materials only)"));
	}
	if (!Params->HasField(TEXT("from_index")))
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("from_index required"));
	}
	if (!Params->HasField(TEXT("property")))
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("property required (e.g., BaseColor, Roughness)"));
	}

	UMaterial* Mat = nullptr; UMaterialFunction* FnIgnored = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, Mat, FnIgnored, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	const int32 FromIdx = static_cast<int32>(Params->GetNumberField(TEXT("from_index")));
	const FString FromOutputName = Params->HasField(TEXT("from_output_name")) ? Params->GetStringField(TEXT("from_output_name")) : FString();
	const FString PropertyName   = Params->GetStringField(TEXT("property"));

	FString LookupErr;
	UMaterialExpression* From = GetExpressionByIndex(Mat, nullptr, FromIdx, LookupErr);
	if (!From) return MakeStructuredError(TEXT("FromExpressionLookupFailed"), LookupErr);

	EMaterialProperty Prop;
	FString PropErr;
	if (!ParseMaterialProperty(PropertyName, Prop, PropErr))
	{
		return MakeStructuredError(TEXT("UnknownMaterialProperty"), PropErr);
	}

	const int32 OutputIdx = FindOutputIndexByName(From, FromOutputName);
	if (OutputIdx == INDEX_NONE)
	{
		return MakeStructuredError(TEXT("OutputNameNotFound"),
			FString::Printf(TEXT("Output '%s' not found on expression index %d (%s)"),
				*FromOutputName, FromIdx, *From->GetClass()->GetName()));
	}

	FScopedTransaction Transaction(NSLOCTEXT("MaterialMCP", "ConnectProperty", "MCP: Connect Material Property"));

	const bool bLibraryOk = UMaterialEditingLibrary::ConnectMaterialProperty(From, FromOutputName, Prop);
	if (!bLibraryOk)
	{
		Transaction.Cancel();
		return MakeStructuredError(TEXT("ConnectionRejected"),
			TEXT("UMaterialEditingLibrary::ConnectMaterialProperty returned false"));
	}

	Mat->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetNumberField(TEXT("from_index"),        FromIdx);
	Result->SetNumberField(TEXT("from_output_index"), OutputIdx);
	Result->SetStringField(TEXT("property"),          PropertyName);
	return Result;
}

// ============================================================================
// material_disconnect_input
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleDisconnectInput(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	if (!Params->HasField(TEXT("expression_index")))
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("expression_index required"));
	}

	UMaterial* Mat = nullptr; UMaterialFunction* Fn = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, Mat, Fn, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	const int32 Index = static_cast<int32>(Params->GetNumberField(TEXT("expression_index")));
	const FString InputName = Params->HasField(TEXT("input_name")) ? Params->GetStringField(TEXT("input_name")) : FString();

	FString LookupErr;
	UMaterialExpression* Expr = GetExpressionByIndex(Mat, Fn, Index, LookupErr);
	if (!Expr) return MakeStructuredError(TEXT("ExpressionLookupFailed"), LookupErr);

	const int32 InputIdx = FindInputIndexByName(Expr, InputName);
	if (InputIdx == INDEX_NONE)
	{
		return MakeStructuredError(TEXT("InputNameNotFound"),
			FString::Printf(TEXT("Input '%s' not found on expression index %d (%s)"),
				*InputName, Index, *Expr->GetClass()->GetName()));
	}
	FExpressionInput* Input = Expr->GetInput(InputIdx);
	if (!Input)
	{
		return MakeStructuredError(TEXT("InputUnavailable"),
			FString::Printf(TEXT("GetInput(%d) returned null"), InputIdx));
	}

	FScopedTransaction Transaction(NSLOCTEXT("MaterialMCP", "DisconnectInput", "MCP: Disconnect Material Input"));

	Expr->Modify();
	Input->Expression = nullptr;
	Input->OutputIndex = 0;

	FPropertyChangedEvent ChangeEvent(nullptr, EPropertyChangeType::ValueSet);
	Expr->PostEditChangeProperty(ChangeEvent);

	// Verify
	FExpressionInput* Verify = Expr->GetInput(InputIdx);
	if (!Verify || Verify->Expression != nullptr)
	{
		Transaction.Cancel();
		return MakeStructuredError(TEXT("DisconnectVerificationFailed"),
			TEXT("Input still referencing an expression after disconnect"));
	}

	(Mat ? Mat->GetPackage() : Fn->GetPackage())->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetNumberField(TEXT("expression_index"), Index);
	Result->SetNumberField(TEXT("input_index"), InputIdx);
	return Result;
}

// ============================================================================
// material_set_expression_position
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleSetExpressionPosition(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	if (!Params->HasField(TEXT("expression_index")) || !Params->HasField(TEXT("x")) || !Params->HasField(TEXT("y")))
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("expression_index, x, y all required"));
	}

	UMaterial* Mat = nullptr; UMaterialFunction* Fn = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, Mat, Fn, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	const int32 Index = static_cast<int32>(Params->GetNumberField(TEXT("expression_index")));
	const int32 X = static_cast<int32>(Params->GetNumberField(TEXT("x")));
	const int32 Y = static_cast<int32>(Params->GetNumberField(TEXT("y")));

	FString LookupErr;
	UMaterialExpression* Expr = GetExpressionByIndex(Mat, Fn, Index, LookupErr);
	if (!Expr) return MakeStructuredError(TEXT("ExpressionLookupFailed"), LookupErr);

	FScopedTransaction Transaction(NSLOCTEXT("MaterialMCP", "SetExpressionPosition", "MCP: Set Material Expression Position"));
	Expr->Modify();
	Expr->MaterialExpressionEditorX = X;
	Expr->MaterialExpressionEditorY = Y;
	FPropertyChangedEvent ChangeEvent(nullptr, EPropertyChangeType::ValueSet);
	Expr->PostEditChangeProperty(ChangeEvent);

	(Mat ? Mat->GetPackage() : Fn->GetPackage())->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetNumberField(TEXT("expression_index"), Index);
	Result->SetNumberField(TEXT("x"), Expr->MaterialExpressionEditorX);
	Result->SetNumberField(TEXT("y"), Expr->MaterialExpressionEditorY);
	return Result;
}

// ============================================================================
// material_set_expression_property — generic UPROPERTY setter on a material
// expression. Delegates to FEpicUnrealMCPCommonUtils::SetObjectProperty so
// every reflection-exposed property (Constant.R, ScalarParameter.DefaultValue,
// TextureSample.Texture, MaterialFunctionCall.MaterialFunction, etc.) is
// reachable without per-class enumeration.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleSetExpressionProperty(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	for (const TCHAR* Required : { TEXT("expression_index"), TEXT("property_name"), TEXT("value") })
	{
		if (!Params->HasField(Required))
		{
			return MakeStructuredError(TEXT("MissingField"), FString::Printf(TEXT("%s required"), Required));
		}
	}

	UMaterial* Mat = nullptr; UMaterialFunction* Fn = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, Mat, Fn, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	const int32 Index = static_cast<int32>(Params->GetNumberField(TEXT("expression_index")));
	const FString PropertyName = Params->GetStringField(TEXT("property_name"));
	const TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));
	if (!Value.IsValid())
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("value required"));
	}

	FString LookupErr;
	UMaterialExpression* Expr = GetExpressionByIndex(Mat, Fn, Index, LookupErr);
	if (!Expr) return MakeStructuredError(TEXT("ExpressionLookupFailed"), LookupErr);

	// Resolve top-level property name (strip dot/bracket suffix for PreEditChange target).
	FString TopName = PropertyName;
	int32 SplitIdx = INDEX_NONE;
	int32 DotIdx = INDEX_NONE; int32 BracketIdx = INDEX_NONE;
	TopName.FindChar(TEXT('.'), DotIdx);
	TopName.FindChar(TEXT('['), BracketIdx);
	if (DotIdx != INDEX_NONE && BracketIdx != INDEX_NONE) SplitIdx = FMath::Min(DotIdx, BracketIdx);
	else if (DotIdx != INDEX_NONE) SplitIdx = DotIdx;
	else if (BracketIdx != INDEX_NONE) SplitIdx = BracketIdx;
	if (SplitIdx != INDEX_NONE) TopName = TopName.Left(SplitIdx);

	FProperty* TargetProp = Expr->GetClass()->FindPropertyByName(*TopName);
	if (!TargetProp)
	{
		return MakeStructuredError(TEXT("PropertyNotFound"),
			FString::Printf(TEXT("Expression %s has no property '%s'"), *Expr->GetClass()->GetName(), *TopName));
	}

	FScopedTransaction Transaction(NSLOCTEXT("MaterialMCP", "SetExpressionProperty", "MCP: Set Material Expression Property"));
	Expr->Modify();
	Expr->PreEditChange(TargetProp);

	FString SetErr;
	const bool bOk = FEpicUnrealMCPCommonUtils::SetObjectProperty(Expr, PropertyName, Value, SetErr);

	FPropertyChangedEvent ChangeEvent(TargetProp, EPropertyChangeType::ValueSet);
	Expr->PostEditChangeProperty(ChangeEvent);

	if (!bOk)
	{
		Transaction.Cancel();
		return MakeStructuredError(TEXT("PropertySetFailed"), SetErr);
	}

	(Mat ? Mat->GetPackage() : Fn->GetPackage())->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetNumberField(TEXT("expression_index"), Index);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	if (!SetErr.IsEmpty())
	{
		Result->SetStringField(TEXT("warning"), SetErr);
	}
	return Result;
}

// ============================================================================
// material_recompile
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleRecompile(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	if (!Params->HasField(TEXT("material_path")))
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("material_path required"));
	}

	UMaterial* Mat = nullptr; UMaterialFunction* FnIgnored = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, Mat, FnIgnored, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	UMaterialEditingLibrary::RecompileMaterial(Mat);

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetStringField(TEXT("material_path"), Mat->GetPathName());
	return Result;
}

// ============================================================================
// material_update_function — recompiles dependents
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleUpdateFunction(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	if (!Params->HasField(TEXT("function_path")))
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("function_path required"));
	}

	UMaterial* MatIgnored = nullptr; UMaterialFunction* Fn = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, MatIgnored, Fn, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	UMaterialEditingLibrary::UpdateMaterialFunction(Fn, nullptr);

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetStringField(TEXT("function_path"), Fn->GetPathName());
	return Result;
}

// ============================================================================
// material_save
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleSave(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	UMaterial* Mat = nullptr; UMaterialFunction* Fn = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, Mat, Fn, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	UObject* Asset = Mat ? static_cast<UObject*>(Mat) : static_cast<UObject*>(Fn);
	const bool bSaved = SaveAsset(Asset);
	if (!bSaved)
	{
		return MakeStructuredError(TEXT("SaveFailed"),
			FString::Printf(TEXT("UEditorAssetLibrary::SaveLoadedAsset returned false for %s"), *Asset->GetPathName()));
	}

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetStringField(TEXT("saved_asset"), Asset->GetPathName());
	return Result;
}

// ============================================================================
// material_batch_edit — runs an array of operations inside one FScopedTransaction.
// On any failure, the whole transaction is canceled.
//
// Operations array entry shape (same fields as the individual commands, minus
// material_path/function_path which are inherited from the outer params):
//
//   { "op": "create_expression",   "expression_class": "Constant", "x": 0, "y": 0 }
//   { "op": "delete_expression",   "expression_index": 7 }
//   { "op": "connect_expressions", "from_index": 3, "to_index": 5,
//                                   "from_output_name": "", "to_input_name": "A" }
//   { "op": "connect_property",    "from_index": 9, "property": "BaseColor" }
//   { "op": "disconnect_input",    "expression_index": 5, "input_name": "B" }
//   { "op": "set_expression_position", "expression_index": 5, "x": 100, "y": -50 }
//   { "op": "set_expression_property", "expression_index": 5, "property_name": "R", "value": 0.5 }
//
// Optional outer params:
//   "recompile": true   — call RecompileMaterial / UpdateMaterialFunction after success
//   "save": true        — save the asset after success (and after recompile if requested)
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleBatchEdit(const TSharedPtr<FJsonObject>& Params)
{
	using namespace EpicUnrealMCPMaterialLocal;

	if (!Params->HasField(TEXT("operations")))
	{
		return MakeStructuredError(TEXT("MissingField"), TEXT("operations array required"));
	}

	UMaterial* Mat = nullptr; UMaterialFunction* Fn = nullptr; FString ResolveErr;
	if (!ResolveMaterialOrFunction(Params, Mat, Fn, ResolveErr))
	{
		return MakeStructuredError(TEXT("TargetResolutionFailed"), ResolveErr);
	}

	const TArray<TSharedPtr<FJsonValue>>& Ops = Params->GetArrayField(TEXT("operations"));
	if (Ops.Num() == 0)
	{
		return MakeStructuredError(TEXT("EmptyOperations"), TEXT("operations array is empty"));
	}

	// Inject the resolved target path back into each child op so sub-handlers
	// can re-resolve. (FScopedTransaction nesting means inner Cancels won't
	// undo this outer transaction — but we abort manually on any failure.)
	const FString TargetField = Mat ? TEXT("material_path") : TEXT("function_path");
	const FString TargetPath  = Mat ? Mat->GetPathName()    : Fn->GetPathName();

	FScopedTransaction Transaction(NSLOCTEXT("MaterialMCP", "BatchEdit", "MCP: Material Batch Edit"));

	TArray<TSharedPtr<FJsonValue>> OpResults;
	for (int32 OpIdx = 0; OpIdx < Ops.Num(); ++OpIdx)
	{
		if (!Ops[OpIdx].IsValid() || Ops[OpIdx]->Type != EJson::Object)
		{
			Transaction.Cancel();
			return MakeStructuredError(TEXT("MalformedOperation"),
				FString::Printf(TEXT("operations[%d] is not an object"), OpIdx));
		}
		TSharedPtr<FJsonObject> Op = Ops[OpIdx]->AsObject();
		if (!Op->HasField(TEXT("op")))
		{
			Transaction.Cancel();
			return MakeStructuredError(TEXT("MalformedOperation"),
				FString::Printf(TEXT("operations[%d] missing 'op' field"), OpIdx));
		}
		const FString OpName = Op->GetStringField(TEXT("op"));

		// Inject target path.
		Op->SetStringField(TargetField, TargetPath);

		TSharedPtr<FJsonObject> OpResult;
		if      (OpName == TEXT("create_expression"))         OpResult = HandleCreateExpression(Op);
		else if (OpName == TEXT("delete_expression"))         OpResult = HandleDeleteExpression(Op);
		else if (OpName == TEXT("connect_expressions"))       OpResult = HandleConnectExpressions(Op);
		else if (OpName == TEXT("connect_property"))          OpResult = HandleConnectProperty(Op);
		else if (OpName == TEXT("disconnect_input"))          OpResult = HandleDisconnectInput(Op);
		else if (OpName == TEXT("set_expression_position"))   OpResult = HandleSetExpressionPosition(Op);
		else if (OpName == TEXT("set_expression_property"))   OpResult = HandleSetExpressionProperty(Op);
		else
		{
			Transaction.Cancel();
			return MakeStructuredError(TEXT("UnknownOperation"),
				FString::Printf(TEXT("operations[%d].op = '%s' not supported in batch_edit"), OpIdx, *OpName));
		}

		if (!OpResult.IsValid() || !OpResult->HasField(TEXT("success")) || !OpResult->GetBoolField(TEXT("success")))
		{
			Transaction.Cancel();
			TSharedPtr<FJsonObject> Err = MakeStructuredError(TEXT("BatchOperationFailed"),
				FString::Printf(TEXT("operations[%d] (op=%s) failed; transaction canceled"), OpIdx, *OpName));
			Err->SetNumberField(TEXT("failed_op_index"), OpIdx);
			Err->SetStringField(TEXT("failed_op"), OpName);
			if (OpResult.IsValid())
			{
				Err->SetObjectField(TEXT("op_response"), OpResult);
			}
			return Err;
		}

		OpResults.Add(MakeShareable(new FJsonValueObject(OpResult)));
	}

	const bool bRecompile = Params->HasField(TEXT("recompile")) && Params->GetBoolField(TEXT("recompile"));
	const bool bSave      = Params->HasField(TEXT("save"))      && Params->GetBoolField(TEXT("save"));

	if (bRecompile)
	{
		if (Mat) UMaterialEditingLibrary::RecompileMaterial(Mat);
		else     UMaterialEditingLibrary::UpdateMaterialFunction(Fn, nullptr);
	}

	bool bSaved = false;
	if (bSave)
	{
		bSaved = SaveAsset(Mat ? static_cast<UObject*>(Mat) : static_cast<UObject*>(Fn));
	}

	TSharedPtr<FJsonObject> Result = MakeSuccess();
	Result->SetStringField(TargetField, TargetPath);
	Result->SetNumberField(TEXT("operations_completed"), Ops.Num());
	Result->SetArrayField(TEXT("operation_results"), OpResults);
	if (bRecompile) Result->SetBoolField(TEXT("recompiled"), true);
	if (bSave)      Result->SetBoolField(TEXT("saved"), bSaved);
	return Result;
}
