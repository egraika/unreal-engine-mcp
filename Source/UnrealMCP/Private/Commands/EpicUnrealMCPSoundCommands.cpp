#include "Commands/EpicUnrealMCPSoundCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"

FEpicUnrealMCPSoundCommands::FEpicUnrealMCPSoundCommands()
{
}

FEpicUnrealMCPSoundCommands::~FEpicUnrealMCPSoundCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPSoundCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_sound_info"))
	{
		return HandleGetSoundInfo(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown Sound command: %s"), *CommandType));
}

// ============================================================================
// get_sound_info
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPSoundCommands::HandleGetSoundInfo(const TSharedPtr<FJsonObject>& Params)
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

	// --- USoundWave ---
	if (USoundWave* SoundWave = Cast<USoundWave>(LoadedAsset))
	{
		Result->SetStringField(TEXT("asset_type"), TEXT("SoundWave"));
		Result->SetStringField(TEXT("asset_name"), SoundWave->GetName());
		Result->SetNumberField(TEXT("duration"), SoundWave->Duration);
		Result->SetNumberField(TEXT("sample_rate"), SoundWave->GetSampleRateForCurrentPlatform());
		Result->SetNumberField(TEXT("num_channels"), SoundWave->NumChannels);
		Result->SetBoolField(TEXT("is_looping"), SoundWave->bLooping);
		Result->SetNumberField(TEXT("sound_group"), static_cast<int32>(SoundWave->SoundGroup));
		Result->SetNumberField(TEXT("volume"), SoundWave->Volume);

		// Resource size (estimated total including bulk data)
		const SIZE_T ResourceSize = SoundWave->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal);
		if (ResourceSize > 0)
		{
			Result->SetNumberField(TEXT("resource_size_bytes"), static_cast<double>(ResourceSize));
		}

		// Sound class
		if (USoundClass* SoundClass = SoundWave->GetSoundClass())
		{
			Result->SetStringField(TEXT("sound_class"), SoundClass->GetName());
		}
		else
		{
			Result->SetStringField(TEXT("sound_class"), TEXT("None"));
		}

		// Attenuation
		if (SoundWave->AttenuationSettings)
		{
			Result->SetStringField(TEXT("attenuation_settings"), SoundWave->AttenuationSettings->GetPathName());
		}

		// Streaming info
		Result->SetBoolField(TEXT("is_streaming"), SoundWave->IsStreaming());

		return Result;
	}

	// --- USoundCue ---
	if (USoundCue* SoundCue = Cast<USoundCue>(LoadedAsset))
	{
		Result->SetStringField(TEXT("asset_type"), TEXT("SoundCue"));
		Result->SetStringField(TEXT("asset_name"), SoundCue->GetName());
		Result->SetNumberField(TEXT("duration"), SoundCue->Duration);
		Result->SetNumberField(TEXT("volume_multiplier"), SoundCue->VolumeMultiplier);
		Result->SetNumberField(TEXT("pitch_multiplier"), SoundCue->PitchMultiplier);
		Result->SetNumberField(TEXT("max_distance"), SoundCue->GetMaxDistance());

		// Attenuation settings
		if (SoundCue->AttenuationSettings)
		{
			Result->SetStringField(TEXT("attenuation_settings"), SoundCue->AttenuationSettings->GetPathName());
		}
		else
		{
			Result->SetStringField(TEXT("attenuation_settings"), TEXT("None"));
		}

		// Sound class
		if (USoundClass* SoundClass = SoundCue->GetSoundClass())
		{
			Result->SetStringField(TEXT("sound_class"), SoundClass->GetName());
		}
		else
		{
			Result->SetStringField(TEXT("sound_class"), TEXT("None"));
		}

		// Subtitles priority
		Result->SetNumberField(TEXT("subtitle_priority"), SoundCue->GetSubtitlePriority());

		// Node graph
		if (SoundCue->FirstNode)
		{
			TSharedPtr<FJsonObject> NodeGraph = SerializeSoundCueNode(SoundCue->FirstNode, 0);
			if (NodeGraph.IsValid())
			{
				Result->SetObjectField(TEXT("node_graph"), NodeGraph);
			}
		}

		// Total node count
#if WITH_EDITORONLY_DATA
		if (SoundCue->AllNodes.Num() > 0)
		{
			Result->SetNumberField(TEXT("total_node_count"), SoundCue->AllNodes.Num());
		}
#endif

		return Result;
	}

	// --- Fallback: USoundBase ---
	if (USoundBase* SoundBase = Cast<USoundBase>(LoadedAsset))
	{
		Result->SetStringField(TEXT("asset_type"), SoundBase->GetClass()->GetName());
		Result->SetStringField(TEXT("asset_name"), SoundBase->GetName());
		Result->SetNumberField(TEXT("duration"), SoundBase->Duration);
		Result->SetNumberField(TEXT("max_distance"), SoundBase->GetMaxDistance());

		if (USoundClass* SoundClass = SoundBase->GetSoundClass())
		{
			Result->SetStringField(TEXT("sound_class"), SoundClass->GetName());
		}
		else
		{
			Result->SetStringField(TEXT("sound_class"), TEXT("None"));
		}

		return Result;
	}

	// Not a sound asset at all
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Asset is not a sound type: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
}

// ============================================================================
// SerializeSoundCueNode — recursive node graph serializer
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPSoundCommands::SerializeSoundCueNode(USoundNode* Node, int32 Depth)
{
	if (!Node || Depth > 20)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
	NodeObj->SetStringField(TEXT("class_name"), Node->GetClass()->GetName());
	NodeObj->SetStringField(TEXT("node_name"), Node->GetName());

	// --- USoundNodeWavePlayer: include the referenced SoundWave path ---
	if (USoundNodeWavePlayer* WavePlayer = Cast<USoundNodeWavePlayer>(Node))
	{
		if (USoundWave* Wave = WavePlayer->GetSoundWave())
		{
			NodeObj->SetStringField(TEXT("sound_wave"), Wave->GetPathName());
			NodeObj->SetNumberField(TEXT("wave_duration"), Wave->Duration);
		}
		else
		{
			NodeObj->SetStringField(TEXT("sound_wave"), TEXT("None"));
		}
		NodeObj->SetBoolField(TEXT("is_looping"), WavePlayer->bLooping);
	}

	// --- USoundNodeAttenuation: include override settings ---
	if (USoundNodeAttenuation* AttenNode = Cast<USoundNodeAttenuation>(Node))
	{
		NodeObj->SetBoolField(TEXT("attenuation_override"), AttenNode->bOverrideAttenuation);
		if (!AttenNode->bOverrideAttenuation && AttenNode->AttenuationSettings)
		{
			NodeObj->SetStringField(TEXT("attenuation_asset"), AttenNode->AttenuationSettings->GetPathName());
		}
		else if (AttenNode->bOverrideAttenuation)
		{
			// Report a few key inline override values
			TSharedPtr<FJsonObject> OverrideObj = MakeShareable(new FJsonObject);
			const FSoundAttenuationSettings& Settings = AttenNode->AttenuationOverrides;
			OverrideObj->SetBoolField(TEXT("attenuate"), Settings.bAttenuate);
			OverrideObj->SetBoolField(TEXT("spatialize"), Settings.bSpatialize);
			OverrideObj->SetNumberField(TEXT("falloff_distance"), Settings.FalloffDistance);
			NodeObj->SetObjectField(TEXT("attenuation_overrides"), OverrideObj);
		}
	}

	// --- USoundNodeModulator: volume/pitch min/max ---
	if (USoundNodeModulator* ModNode = Cast<USoundNodeModulator>(Node))
	{
		NodeObj->SetNumberField(TEXT("volume_min"), ModNode->VolumeMin);
		NodeObj->SetNumberField(TEXT("volume_max"), ModNode->VolumeMax);
		NodeObj->SetNumberField(TEXT("pitch_min"), ModNode->PitchMin);
		NodeObj->SetNumberField(TEXT("pitch_max"), ModNode->PitchMax);
	}

	// --- USoundNodeRandom: weights ---
	if (USoundNodeRandom* RandNode = Cast<USoundNodeRandom>(Node))
	{
		TArray<TSharedPtr<FJsonValue>> WeightsArray;
		for (float Weight : RandNode->Weights)
		{
			WeightsArray.Add(MakeShareable(new FJsonValueNumber(Weight)));
		}
		NodeObj->SetArrayField(TEXT("weights"), WeightsArray);
		NodeObj->SetBoolField(TEXT("randomize_without_replacement"), RandNode->bRandomizeWithoutReplacement);
	}

	// --- Recurse into children ---
	TArray<TSharedPtr<FJsonValue>> ChildrenArray;
	for (USoundNode* ChildNode : Node->ChildNodes)
	{
		TSharedPtr<FJsonObject> ChildObj = SerializeSoundCueNode(ChildNode, Depth + 1);
		if (ChildObj.IsValid())
		{
			ChildrenArray.Add(MakeShareable(new FJsonValueObject(ChildObj)));
		}
		else
		{
			ChildrenArray.Add(MakeShareable(new FJsonValueNull()));
		}
	}
	NodeObj->SetArrayField(TEXT("children"), ChildrenArray);

	return NodeObj;
}
