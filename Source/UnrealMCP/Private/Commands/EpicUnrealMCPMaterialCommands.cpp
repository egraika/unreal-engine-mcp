#include "Commands/EpicUnrealMCPMaterialCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
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
#include "Materials/MaterialFunction.h"

FEpicUnrealMCPMaterialCommands::FEpicUnrealMCPMaterialCommands()
{
}

FEpicUnrealMCPMaterialCommands::~FEpicUnrealMCPMaterialCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPMaterialCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_material_expressions"))
	{
		return HandleGetMaterialExpressions(Params);
	}
	else if (CommandType == TEXT("get_material_connections"))
	{
		return HandleGetMaterialConnections(Params);
	}
	else if (CommandType == TEXT("get_material_function_info"))
	{
		return HandleGetMaterialFunctionInfo(Params);
	}
	else if (CommandType == TEXT("get_material_stats"))
	{
		return HandleGetMaterialStats(Params);
	}

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

	// Material output properties from EditorOnlyData
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

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
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
