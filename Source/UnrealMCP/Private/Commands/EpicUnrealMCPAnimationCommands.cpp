#include "Commands/EpicUnrealMCPAnimationCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Animation/Skeleton.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Animation/PreviewAssetAttachComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimEnums.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/VariableFrameStrippingSettings.h"
#include "Animation/BlendProfile.h"
#include "Animation/AnimMetaData.h"
#include "AlphaBlend.h"
#include "JsonObjectConverter.h"

FEpicUnrealMCPAnimationCommands::FEpicUnrealMCPAnimationCommands()
{
}

FEpicUnrealMCPAnimationCommands::~FEpicUnrealMCPAnimationCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("analyze_anim_montage"))
	{
		return HandleAnalyzeAnimMontage(Params);
	}
	else if (CommandType == TEXT("analyze_anim_sequence"))
	{
		return HandleAnalyzeAnimSequence(Params);
	}
	else if (CommandType == TEXT("list_animation_assets"))
	{
		return HandleListAnimationAssets(Params);
	}
	else if (CommandType == TEXT("get_skeletal_mesh_info"))
	{
		return HandleGetSkeletalMeshInfo(Params);
	}
	else if (CommandType == TEXT("add_socket_to_skeleton"))
	{
		return HandleAddSocketToSkeleton(Params);
	}
	else if (CommandType == TEXT("modify_socket"))
	{
		return HandleModifySocket(Params);
	}
	else if (CommandType == TEXT("remove_socket"))
	{
		return HandleRemoveSocket(Params);
	}
	else if (CommandType == TEXT("preview_mesh_on_socket"))
	{
		return HandlePreviewMeshOnSocket(Params);
	}
	else if (CommandType == TEXT("clear_socket_preview"))
	{
		return HandleClearSocketPreview(Params);
	}
	else if (CommandType == TEXT("capture_socket_preview"))
	{
		return HandleCaptureSocketPreview(Params);
	}
	else if (CommandType == TEXT("create_anim_montage"))
	{
		return HandleCreateAnimMontage(Params);
	}
	else if (CommandType == TEXT("add_montage_section"))
	{
		return HandleAddMontageSection(Params);
	}
	else if (CommandType == TEXT("remove_montage_section"))
	{
		return HandleRemoveMontageSection(Params);
	}
	else if (CommandType == TEXT("set_montage_section_link"))
	{
		return HandleSetMontageSectionLink(Params);
	}
	else if (CommandType == TEXT("update_montage_section"))
	{
		return HandleUpdateMontageSection(Params);
	}
	else if (CommandType == TEXT("add_montage_notify"))
	{
		return HandleAddMontageNotify(Params);
	}
	else if (CommandType == TEXT("remove_montage_notify"))
	{
		return HandleRemoveMontageNotify(Params);
	}
	else if (CommandType == TEXT("update_montage_notify"))
	{
		return HandleUpdateMontageNotify(Params);
	}
	else if (CommandType == TEXT("add_montage_notify_track"))
	{
		return HandleAddMontageNotifyTrack(Params);
	}
	else if (CommandType == TEXT("rename_montage_notify_track"))
	{
		return HandleRenameMontageNotifyTrack(Params);
	}
	else if (CommandType == TEXT("remove_montage_notify_track"))
	{
		return HandleRemoveMontageNotifyTrack(Params);
	}
	else if (CommandType == TEXT("add_montage_segment"))
	{
		return HandleAddMontageSegment(Params);
	}
	else if (CommandType == TEXT("remove_montage_segment"))
	{
		return HandleRemoveMontageSegment(Params);
	}
	else if (CommandType == TEXT("update_montage_segment"))
	{
		return HandleUpdateMontageSegment(Params);
	}
	else if (CommandType == TEXT("set_montage_blend_times"))
	{
		return HandleSetMontageBlendTimes(Params);
	}
	else if (CommandType == TEXT("set_montage_root_motion"))
	{
		return HandleSetMontageRootMotion(Params);
	}
	else if (CommandType == TEXT("set_montage_sync_group"))
	{
		return HandleSetMontageSyncGroup(Params);
	}
	else if (CommandType == TEXT("set_montage_playback"))
	{
		return HandleSetMontagePlayback(Params);
	}
	else if (CommandType == TEXT("set_montage_time_stretch"))
	{
		return HandleSetMontageTimeStretch(Params);
	}
	else if (CommandType == TEXT("set_montage_preview_base_pose"))
	{
		return HandleSetMontagePreviewBasePose(Params);
	}
	else if (CommandType == TEXT("add_montage_slot"))
	{
		return HandleAddMontageSlot(Params);
	}
	else if (CommandType == TEXT("rename_montage_slot"))
	{
		return HandleRenameMontageSlot(Params);
	}
	else if (CommandType == TEXT("remove_montage_slot"))
	{
		return HandleRemoveMontageSlot(Params);
	}
	else if (CommandType == TEXT("set_montage_notify_track_color"))
	{
		return HandleSetMontageNotifyTrackColor(Params);
	}
	else if (CommandType == TEXT("add_section_metadata"))
	{
		return HandleAddSectionMetadata(Params);
	}
	else if (CommandType == TEXT("remove_section_metadata"))
	{
		return HandleRemoveSectionMetadata(Params);
	}
	else if (CommandType == TEXT("reorder_montage_section"))
	{
		return HandleReorderMontageSection(Params);
	}
	else if (CommandType == TEXT("reorder_montage_segment"))
	{
		return HandleReorderMontageSegment(Params);
	}
	else if (CommandType == TEXT("reorder_montage_notify_track"))
	{
		return HandleReorderMontageNotifyTrack(Params);
	}
	else if (CommandType == TEXT("reorder_montage_slot"))
	{
		return HandleReorderMontageSlot(Params);
	}
	else if (CommandType == TEXT("get_anim_curve_keys"))
	{
		return HandleGetAnimCurveKeys(Params);
	}
	else if (CommandType == TEXT("add_anim_curve"))
	{
		return HandleAddAnimCurve(Params);
	}
	else if (CommandType == TEXT("set_anim_curve_keys"))
	{
		return HandleSetAnimCurveKeys(Params);
	}
	else if (CommandType == TEXT("remove_anim_curve"))
	{
		return HandleRemoveAnimCurve(Params);
	}
	else if (CommandType == TEXT("get_root_motion_data"))
	{
		return HandleGetRootMotionData(Params);
	}
	else if (CommandType == TEXT("batch_add_speed_curves"))
	{
		return HandleBatchAddSpeedCurves(Params);
	}
	else if (CommandType == TEXT("set_anim_sequence_root_motion"))
	{
		return HandleSetAnimSequenceRootMotion(Params);
	}
	else if (CommandType == TEXT("set_anim_sequence_properties"))
	{
		return HandleSetAnimSequenceProperties(Params);
	}
	else if (CommandType == TEXT("create_mirror_data_table"))
	{
		return HandleCreateMirrorDataTable(Params);
	}
	else if (CommandType == TEXT("analyze_mirror_data_table"))
	{
		return HandleAnalyzeMirrorDataTable(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown Animation command: %s"), *CommandType));
}

// ============================================================================
// Enum <-> string helpers
// ============================================================================

static const TCHAR* AlphaBlendOptionToString(EAlphaBlendOption Option)
{
	switch (Option)
	{
		case EAlphaBlendOption::Linear:          return TEXT("Linear");
		case EAlphaBlendOption::Cubic:           return TEXT("Cubic");
		case EAlphaBlendOption::HermiteCubic:    return TEXT("HermiteCubic");
		case EAlphaBlendOption::Sinusoidal:      return TEXT("Sinusoidal");
		case EAlphaBlendOption::QuadraticInOut:  return TEXT("QuadraticInOut");
		case EAlphaBlendOption::CubicInOut:      return TEXT("CubicInOut");
		case EAlphaBlendOption::QuarticInOut:    return TEXT("QuarticInOut");
		case EAlphaBlendOption::QuinticInOut:    return TEXT("QuinticInOut");
		case EAlphaBlendOption::CircularIn:      return TEXT("CircularIn");
		case EAlphaBlendOption::CircularOut:     return TEXT("CircularOut");
		case EAlphaBlendOption::CircularInOut:   return TEXT("CircularInOut");
		case EAlphaBlendOption::ExpIn:           return TEXT("ExpIn");
		case EAlphaBlendOption::ExpOut:          return TEXT("ExpOut");
		case EAlphaBlendOption::ExpInOut:        return TEXT("ExpInOut");
		case EAlphaBlendOption::Custom:          return TEXT("Custom");
		default:                                 return TEXT("Linear");
	}
}

static bool TryParseAlphaBlendOption(const FString& S, EAlphaBlendOption& Out)
{
	#define MATCH(x) if (S.Equals(TEXT(#x), ESearchCase::IgnoreCase)) { Out = EAlphaBlendOption::x; return true; }
	MATCH(Linear) MATCH(Cubic) MATCH(HermiteCubic) MATCH(Sinusoidal)
	MATCH(QuadraticInOut) MATCH(CubicInOut) MATCH(QuarticInOut) MATCH(QuinticInOut)
	MATCH(CircularIn) MATCH(CircularOut) MATCH(CircularInOut)
	MATCH(ExpIn) MATCH(ExpOut) MATCH(ExpInOut) MATCH(Custom)
	#undef MATCH
	return false;
}

static const TCHAR* RootMotionRootLockToString(ERootMotionRootLock::Type Lock)
{
	switch (Lock)
	{
		case ERootMotionRootLock::RefPose:        return TEXT("RefPose");
		case ERootMotionRootLock::AnimFirstFrame: return TEXT("AnimFirstFrame");
		case ERootMotionRootLock::Zero:           return TEXT("Zero");
		default:                                  return TEXT("RefPose");
	}
}

static bool TryParseRootMotionRootLock(const FString& S, ERootMotionRootLock::Type& Out)
{
	if (S.Equals(TEXT("RefPose"),        ESearchCase::IgnoreCase)) { Out = ERootMotionRootLock::RefPose;        return true; }
	if (S.Equals(TEXT("AnimFirstFrame"), ESearchCase::IgnoreCase)) { Out = ERootMotionRootLock::AnimFirstFrame; return true; }
	if (S.Equals(TEXT("Zero"),           ESearchCase::IgnoreCase)) { Out = ERootMotionRootLock::Zero;           return true; }
	return false;
}

static const TCHAR* AnimInterpolationTypeToString(EAnimInterpolationType T)
{
	switch (T)
	{
		case EAnimInterpolationType::Linear: return TEXT("Linear");
		case EAnimInterpolationType::Step:   return TEXT("Step");
		default:                             return TEXT("Linear");
	}
}

static bool TryParseAnimInterpolationType(const FString& S, EAnimInterpolationType& Out)
{
	if (S.Equals(TEXT("Linear"), ESearchCase::IgnoreCase)) { Out = EAnimInterpolationType::Linear; return true; }
	if (S.Equals(TEXT("Step"),   ESearchCase::IgnoreCase)) { Out = EAnimInterpolationType::Step;   return true; }
	return false;
}

static const TCHAR* AdditiveAnimTypeToString(EAdditiveAnimationType T)
{
	switch (T)
	{
		case AAT_None:                    return TEXT("None");
		case AAT_LocalSpaceBase:          return TEXT("LocalSpaceBase");
		case AAT_RotationOffsetMeshSpace: return TEXT("RotationOffsetMeshSpace");
		default:                          return TEXT("None");
	}
}

static bool TryParseAdditiveAnimType(const FString& S, EAdditiveAnimationType& Out)
{
	if (S.Equals(TEXT("None"),                    ESearchCase::IgnoreCase)) { Out = AAT_None;                    return true; }
	if (S.Equals(TEXT("LocalSpaceBase"),          ESearchCase::IgnoreCase)) { Out = AAT_LocalSpaceBase;          return true; }
	if (S.Equals(TEXT("RotationOffsetMeshSpace"), ESearchCase::IgnoreCase)) { Out = AAT_RotationOffsetMeshSpace; return true; }
	return false;
}

static const TCHAR* AdditiveBasePoseTypeToString(EAdditiveBasePoseType T)
{
	switch (T)
	{
		case ABPT_None:           return TEXT("None");
		case ABPT_RefPose:        return TEXT("RefPose");
		case ABPT_AnimScaled:     return TEXT("AnimScaled");
		case ABPT_AnimFrame:      return TEXT("AnimFrame");
		case ABPT_LocalAnimFrame: return TEXT("LocalAnimFrame");
		default:                  return TEXT("None");
	}
}

static bool TryParseAdditiveBasePoseType(const FString& S, EAdditiveBasePoseType& Out)
{
	if (S.Equals(TEXT("None"),           ESearchCase::IgnoreCase)) { Out = ABPT_None;           return true; }
	if (S.Equals(TEXT("RefPose"),        ESearchCase::IgnoreCase)) { Out = ABPT_RefPose;        return true; }
	if (S.Equals(TEXT("AnimScaled"),     ESearchCase::IgnoreCase)) { Out = ABPT_AnimScaled;     return true; }
	if (S.Equals(TEXT("AnimFrame"),      ESearchCase::IgnoreCase)) { Out = ABPT_AnimFrame;      return true; }
	if (S.Equals(TEXT("LocalAnimFrame"), ESearchCase::IgnoreCase)) { Out = ABPT_LocalAnimFrame; return true; }
	return false;
}

static const TCHAR* MontageBlendModeToString(EMontageBlendMode Mode)
{
	switch (Mode)
	{
		case EMontageBlendMode::Standard:        return TEXT("Standard");
		case EMontageBlendMode::Inertialization: return TEXT("Inertialization");
		default:                                 return TEXT("Standard");
	}
}

static bool TryParseMontageBlendMode(const FString& S, EMontageBlendMode& Out)
{
	if (S.Equals(TEXT("Standard"),        ESearchCase::IgnoreCase)) { Out = EMontageBlendMode::Standard;        return true; }
	if (S.Equals(TEXT("Inertialization"), ESearchCase::IgnoreCase)) { Out = EMontageBlendMode::Inertialization; return true; }
	return false;
}

// ============================================================================
// Helper: Build notify array from UAnimSequenceBase::Notifies
// ============================================================================

static TArray<TSharedPtr<FJsonValue>> BuildNotifiesArray(const UAnimSequenceBase* AnimBase)
{
	TArray<TSharedPtr<FJsonValue>> NotifiesArray;
	if (!AnimBase)
	{
		return NotifiesArray;
	}

	const TArray<FAnimNotifyTrack>& Tracks = AnimBase->AnimNotifyTracks;

	for (int32 NotifyIdx = 0; NotifyIdx < AnimBase->Notifies.Num(); ++NotifyIdx)
	{
		const FAnimNotifyEvent& Notify = AnimBase->Notifies[NotifyIdx];
		TSharedPtr<FJsonObject> NotifyObj = MakeShareable(new FJsonObject);

		NotifyObj->SetNumberField(TEXT("index"), NotifyIdx);

		// Notify name: prefer NotifyName, fall back to Notify object name
		FString NotifyName = Notify.NotifyName.ToString();
		if (NotifyName.IsEmpty() || NotifyName == TEXT("None"))
		{
			if (Notify.Notify)
			{
				NotifyName = Notify.Notify->GetNotifyName();
			}
			else if (Notify.NotifyStateClass)
			{
				NotifyName = Notify.NotifyStateClass->GetNotifyName();
			}
			else
			{
				NotifyName = TEXT("Unknown");
			}
		}
		NotifyObj->SetStringField(TEXT("notify_name"), NotifyName);

		const float TriggerTime = Notify.GetTriggerTime();
		const float Duration = Notify.GetDuration();
		NotifyObj->SetNumberField(TEXT("trigger_time"), TriggerTime);
		NotifyObj->SetNumberField(TEXT("duration"), Duration);
		NotifyObj->SetNumberField(TEXT("end_time"), TriggerTime + Duration);
		NotifyObj->SetBoolField(TEXT("is_state"), Notify.NotifyStateClass != nullptr);

		// Notify class (short name for back-compat, full path for round-trip)
		FString NotifyClassName = TEXT("None");
		FString NotifyClassPath = FString();
		UObject* NotifyInstance = nullptr;
		if (Notify.Notify)
		{
			NotifyClassName = Notify.Notify->GetClass()->GetName();
			NotifyClassPath = Notify.Notify->GetClass()->GetPathName();
			NotifyInstance = Notify.Notify;
		}
		else if (Notify.NotifyStateClass)
		{
			NotifyClassName = Notify.NotifyStateClass->GetClass()->GetName();
			NotifyClassPath = Notify.NotifyStateClass->GetClass()->GetPathName();
			NotifyInstance = Notify.NotifyStateClass;
		}
		NotifyObj->SetStringField(TEXT("notify_class"), NotifyClassName);
		NotifyObj->SetStringField(TEXT("notify_class_path"), NotifyClassPath);

		// Track index + resolved name
		NotifyObj->SetNumberField(TEXT("track_index"), Notify.TrackIndex);
		if (Tracks.IsValidIndex(Notify.TrackIndex))
		{
			NotifyObj->SetStringField(TEXT("track_name"), Tracks[Notify.TrackIndex].TrackName.ToString());
		}
		else
		{
			NotifyObj->SetStringField(TEXT("track_name"), FString::Printf(TEXT("<invalid:%d>"), Notify.TrackIndex));
		}

		// Tick type
		NotifyObj->SetStringField(TEXT("tick_type"),
			Notify.MontageTickType == EMontageNotifyTickType::BranchingPoint
				? TEXT("BranchingPoint")
				: TEXT("Queued"));

		// Time offsets & firing config
		NotifyObj->SetNumberField(TEXT("trigger_time_offset"), Notify.TriggerTimeOffset);
		NotifyObj->SetNumberField(TEXT("end_trigger_time_offset"), Notify.EndTriggerTimeOffset);
		NotifyObj->SetNumberField(TEXT("trigger_weight_threshold"), Notify.TriggerWeightThreshold);
		NotifyObj->SetNumberField(TEXT("notify_trigger_chance"), Notify.NotifyTriggerChance);
		NotifyObj->SetBoolField(TEXT("trigger_on_dedicated_server"), Notify.bTriggerOnDedicatedServer);
		NotifyObj->SetBoolField(TEXT("trigger_on_follower"), Notify.bTriggerOnFollower);

		// Reflection-based property payload of the notify instance
		if (NotifyInstance)
		{
			TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject);
			const bool bOk = FJsonObjectConverter::UStructToJsonObject(
				NotifyInstance->GetClass(), NotifyInstance, PropsObj.ToSharedRef(), CPF_Edit, 0);
			if (bOk)
			{
				NotifyObj->SetObjectField(TEXT("properties"), PropsObj);
			}
		}

		NotifiesArray.Add(MakeShareable(new FJsonValueObject(NotifyObj)));
	}

	return NotifiesArray;
}

// ============================================================================
// Helper: Build top-level notify_tracks array
// ============================================================================

static TSharedPtr<FJsonObject> LinearColorToJson(const FLinearColor& C)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetNumberField(TEXT("r"), C.R);
	Obj->SetNumberField(TEXT("g"), C.G);
	Obj->SetNumberField(TEXT("b"), C.B);
	Obj->SetNumberField(TEXT("a"), C.A);
	return Obj;
}

// Parse color from JSON: accepts object { r, g, b, a } with defaults (0,0,0,1).
static bool TryParseLinearColor(const TSharedPtr<FJsonObject>& Obj, FLinearColor& Out)
{
	if (!Obj.IsValid()) return false;
	Out = FLinearColor(
		static_cast<float>(Obj->HasField(TEXT("r")) ? Obj->GetNumberField(TEXT("r")) : 0.0),
		static_cast<float>(Obj->HasField(TEXT("g")) ? Obj->GetNumberField(TEXT("g")) : 0.0),
		static_cast<float>(Obj->HasField(TEXT("b")) ? Obj->GetNumberField(TEXT("b")) : 0.0),
		static_cast<float>(Obj->HasField(TEXT("a")) ? Obj->GetNumberField(TEXT("a")) : 1.0));
	return true;
}

static TArray<TSharedPtr<FJsonValue>> BuildNotifyTracksArray(const UAnimSequenceBase* AnimBase)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	if (!AnimBase)
	{
		return Result;
	}
	for (int32 i = 0; i < AnimBase->AnimNotifyTracks.Num(); ++i)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetNumberField(TEXT("index"), i);
		Obj->SetStringField(TEXT("name"), AnimBase->AnimNotifyTracks[i].TrackName.ToString());
		Obj->SetObjectField(TEXT("color"), LinearColorToJson(AnimBase->AnimNotifyTracks[i].TrackColor));
		Result.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	return Result;
}

// Build the per-section MetaData array as JSON.
static TArray<TSharedPtr<FJsonValue>> BuildSectionMetaDataArray(const FCompositeSection& Section)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	const TArray<UAnimMetaData*>& MetaList = Section.GetMetaData();
	for (int32 i = 0; i < MetaList.Num(); ++i)
	{
		UAnimMetaData* Meta = MetaList[i];
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetNumberField(TEXT("index"), i);
		if (Meta)
		{
			Obj->SetStringField(TEXT("class"), Meta->GetClass()->GetName());
			Obj->SetStringField(TEXT("class_path"), Meta->GetClass()->GetPathName());
			TSharedPtr<FJsonObject> Props = MakeShareable(new FJsonObject);
			if (FJsonObjectConverter::UStructToJsonObject(Meta->GetClass(), Meta, Props.ToSharedRef(), CPF_Edit, 0))
			{
				Obj->SetObjectField(TEXT("properties"), Props);
			}
		}
		else
		{
			Obj->SetStringField(TEXT("class"), TEXT("None"));
		}
		Result.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	return Result;
}

// ============================================================================
// Helper: Recompute composite length from all segments
// ============================================================================

static void RecomputeMontageCompositeLength(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return;
	}
	float MaxEndPos = 0.0f;
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Seg : Track.AnimTrack.AnimSegments)
		{
			// FAnimSegment::GetLength already folds in LoopingCount + play rate.
			const float SegEnd = Seg.StartPos + Seg.GetLength();
			MaxEndPos = FMath::Max(MaxEndPos, SegEnd);
		}
	}
	Montage->SetCompositeLength(MaxEndPos);
}

// Forward declarations for helpers used by HandleCreateAnimMontage but defined
// alongside the individual mutation handlers later in this file.
static UClass* ResolveNotifyClass(const TSharedPtr<FJsonObject>& Params, FString& OutError);
static int32   ResolveNotifyTrackIndex(const UAnimSequenceBase* AnimBase, const TSharedPtr<FJsonObject>& Params, FString& OutError);
static void    ApplyNotifyProperties(UObject* Instance, const TSharedPtr<FJsonObject>& Params);
static void    ApplyNotifyEventFields(FAnimNotifyEvent& Event, const TSharedPtr<FJsonObject>& Params);
static int32   ResolveOrCreateSlotIndex(UAnimMontage* Montage, const TSharedPtr<FJsonObject>& Params, bool bAllowCreate, FString& OutError);

// ============================================================================
// Derived analytics helpers for analyze_anim_montage / analyze_anim_sequence.
// These compute signals the caller would otherwise have to derive from
// primitives (overlaps, gaps, out-of-bounds, root-motion totals, curve
// samples, notify distribution, coverage) so AI-driven reasoning can read
// facts rather than compute them.
// ============================================================================

namespace AnalyzeWarnings
{
	static const TCHAR* SegmentOverlap            = TEXT("segment_overlap");
	static const TCHAR* SegmentGap                = TEXT("segment_gap");
	static const TCHAR* NotifyOutsideSegments     = TEXT("notify_outside_segments");
	static const TCHAR* NotifyPastMontageEnd      = TEXT("notify_past_montage_end");
	static const TCHAR* NotifyTrackIndexInvalid   = TEXT("notify_track_index_invalid");
	static const TCHAR* NotifySameTimeSameTrack   = TEXT("notify_same_time_same_track");
	static const TCHAR* SectionNextBroken         = TEXT("section_next_broken");
	static const TCHAR* SectionTimeOutOfRange     = TEXT("section_time_out_of_range");
	static const TCHAR* OrphanNotifyTrack         = TEXT("orphan_notify_track");
	static const TCHAR* EmptySlot                 = TEXT("empty_slot");
	static const TCHAR* SyncSlotInvalid           = TEXT("sync_slot_invalid");
	static const TCHAR* TimeStretchCurveMissing   = TEXT("time_stretch_curve_missing");
}

// Read optional warning_filter array from params into a set of strings to suppress.
static TSet<FString> BuildWarningFilterSet(const TSharedPtr<FJsonObject>& Params)
{
	TSet<FString> Out;
	if (!Params.IsValid() || !Params->HasField(TEXT("warning_filter")))
	{
		return Out;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Params->TryGetArrayField(TEXT("warning_filter"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString S;
			if (V->TryGetString(S))
			{
				Out.Add(S);
			}
		}
	}
	return Out;
}

static TSharedPtr<FJsonObject> MakeWarning(const TCHAR* Code, const TCHAR* Severity, const FString& Message)
{
	TSharedPtr<FJsonObject> W = MakeShareable(new FJsonObject);
	W->SetStringField(TEXT("code"), Code);
	W->SetStringField(TEXT("severity"), Severity);
	W->SetStringField(TEXT("message"), Message);
	return W;
}

static void AppendWarning(TArray<TSharedPtr<FJsonValue>>& Arr, const TSet<FString>& Filter, TSharedPtr<FJsonObject> W)
{
	if (!W.IsValid()) return;
	FString Code;
	W->TryGetStringField(TEXT("code"), Code);
	if (Filter.Contains(Code)) return;
	Arr.Add(MakeShareable(new FJsonValueObject(W)));
}

// Compute coverage fraction for a single slot: fraction of [0, PlayLength]
// covered by the union of its segment spans.
static double ComputeSlotCoverageFraction(const FSlotAnimationTrack& Slot, float PlayLength)
{
	if (PlayLength <= 0.0f) return 0.0;

	struct FSpan { float Start; float End; };
	TArray<FSpan> Spans;
	for (const FAnimSegment& Seg : Slot.AnimTrack.AnimSegments)
	{
		const float Start = FMath::Clamp(Seg.StartPos, 0.0f, PlayLength);
		const float End   = FMath::Clamp(Seg.StartPos + Seg.GetLength(), 0.0f, PlayLength);
		if (End > Start)
		{
			Spans.Add({ Start, End });
		}
	}
	Spans.Sort([](const FSpan& A, const FSpan& B) { return A.Start < B.Start; });

	float Covered = 0.0f;
	float CursorStart = 0.0f;
	float CursorEnd = 0.0f;
	bool bHas = false;
	for (const FSpan& S : Spans)
	{
		if (!bHas)
		{
			CursorStart = S.Start;
			CursorEnd   = S.End;
			bHas = true;
			continue;
		}
		if (S.Start > CursorEnd)
		{
			Covered += (CursorEnd - CursorStart);
			CursorStart = S.Start;
			CursorEnd   = S.End;
		}
		else
		{
			CursorEnd = FMath::Max(CursorEnd, S.End);
		}
	}
	if (bHas) Covered += (CursorEnd - CursorStart);
	return static_cast<double>(Covered) / static_cast<double>(PlayLength);
}

// Per-montage notify distribution summary.
static TSharedPtr<FJsonObject> ComputeNotifyDistribution(const UAnimMontage* Montage)
{
	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	if (!Montage)
	{
		Out->SetNumberField(TEXT("total"), 0);
		return Out;
	}

	int32 BranchingPoints = 0;
	int32 Queued = 0;
	int32 StateCount = 0;
	int32 InstantCount = 0;

	TSharedPtr<FJsonObject> ByTrack = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> ByClass = MakeShareable(new FJsonObject);

	const TArray<FAnimNotifyTrack>& Tracks = Montage->AnimNotifyTracks;

	for (const FAnimNotifyEvent& N : Montage->Notifies)
	{
		// Tick type
		if (N.MontageTickType == EMontageNotifyTickType::BranchingPoint) { ++BranchingPoints; }
		else                                                             { ++Queued; }

		// State vs instant
		if (N.NotifyStateClass != nullptr) { ++StateCount; }
		else                               { ++InstantCount; }

		// Track count
		const FString TrackName = Tracks.IsValidIndex(N.TrackIndex)
			? Tracks[N.TrackIndex].TrackName.ToString()
			: FString::Printf(TEXT("<invalid:%d>"), N.TrackIndex);
		double CurrentTrack = 0.0;
		ByTrack->TryGetNumberField(TrackName, CurrentTrack);
		ByTrack->SetNumberField(TrackName, CurrentTrack + 1.0);

		// Class count (full path)
		FString ClassPath;
		if (N.Notify)                  ClassPath = N.Notify->GetClass()->GetPathName();
		else if (N.NotifyStateClass)   ClassPath = N.NotifyStateClass->GetClass()->GetPathName();
		else                           ClassPath = TEXT("<builtin>");
		double CurrentClass = 0.0;
		ByClass->TryGetNumberField(ClassPath, CurrentClass);
		ByClass->SetNumberField(ClassPath, CurrentClass + 1.0);
	}

	Out->SetNumberField(TEXT("total"), Montage->Notifies.Num());
	Out->SetNumberField(TEXT("branching_points"), BranchingPoints);
	Out->SetNumberField(TEXT("queued"), Queued);
	Out->SetNumberField(TEXT("state_count"), StateCount);
	Out->SetNumberField(TEXT("instant_count"), InstantCount);
	Out->SetObjectField(TEXT("by_track"), ByTrack);
	Out->SetObjectField(TEXT("by_class"), ByClass);
	return Out;
}

// Serialize an FTransform delta as root-motion info.
static TSharedPtr<FJsonObject> RootMotionDeltaToJson(const FTransform& Delta)
{
	const FVector Trans = Delta.GetTranslation();
	const FRotator Rot  = Delta.GetRotation().Rotator();
	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);

	TSharedPtr<FJsonObject> T = MakeShareable(new FJsonObject);
	T->SetNumberField(TEXT("x"), Trans.X);
	T->SetNumberField(TEXT("y"), Trans.Y);
	T->SetNumberField(TEXT("z"), Trans.Z);
	Out->SetObjectField(TEXT("translation"), T);

	TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
	R->SetNumberField(TEXT("pitch"), Rot.Pitch);
	R->SetNumberField(TEXT("yaw"),   Rot.Yaw);
	R->SetNumberField(TEXT("roll"),  Rot.Roll);
	Out->SetObjectField(TEXT("rotation_euler"), R);

	Out->SetNumberField(TEXT("distance_xy"), FVector(Trans.X, Trans.Y, 0.0).Size());
	Out->SetNumberField(TEXT("distance_z"),  FMath::Abs(Trans.Z));
	return Out;
}

// Extract root motion over one playthrough of a segment (no looping applied).
static FTransform ExtractSegmentRootMotionOnce(const FAnimSegment& Seg)
{
	UAnimSequenceBase* RefBase = Seg.GetAnimReference();
	UAnimSequence* Seq = Cast<UAnimSequence>(RefBase);
	if (!Seq)
	{
		return FTransform::Identity;
	}
	const float Duration = FMath::Max(0.0f, Seg.AnimEndTime - Seg.AnimStartTime);
	if (Duration <= KINDA_SMALL_NUMBER)
	{
		return FTransform::Identity;
	}
	FAnimExtractContext RootMotionContext(Seg.AnimStartTime, /*bExtractRootMotion=*/true, FDeltaTimeRecord(Duration), /*bLooping=*/false);
	return Seq->ExtractRootMotion(RootMotionContext);
}

// Segment root motion including LoopingCount composition.
static FTransform ExtractSegmentRootMotionTotal(const FAnimSegment& Seg)
{
	FTransform Once = ExtractSegmentRootMotionOnce(Seg);
	const int32 Loops = FMath::Max(1, Seg.LoopingCount);
	if (Loops == 1)
	{
		return Once;
	}
	FTransform Total = FTransform::Identity;
	for (int32 i = 0; i < Loops; ++i)
	{
		Total = Once * Total;
	}
	return Total;
}

// Per-slot + per-segment root motion summary for a montage.
static TSharedPtr<FJsonObject> ComputeMontageRootMotionSummary(const UAnimMontage* Montage)
{
	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	if (!Montage)
	{
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> SlotsJson;
	FTransform MontageTotal = FTransform::Identity;

	for (int32 SlotIdx = 0; SlotIdx < Montage->SlotAnimTracks.Num(); ++SlotIdx)
	{
		const FSlotAnimationTrack& Slot = Montage->SlotAnimTracks[SlotIdx];
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetNumberField(TEXT("slot_index"), SlotIdx);
		SlotObj->SetStringField(TEXT("slot_name"), Slot.SlotName.ToString());

		TArray<TSharedPtr<FJsonValue>> Segments;
		FTransform SlotTotal = FTransform::Identity;

		// Compose in StartPos order so segment_total reflects play-order motion.
		TArray<int32> Order;
		Order.Reserve(Slot.AnimTrack.AnimSegments.Num());
		for (int32 i = 0; i < Slot.AnimTrack.AnimSegments.Num(); ++i) { Order.Add(i); }
		Order.Sort([&Slot](int32 A, int32 B)
		{
			return Slot.AnimTrack.AnimSegments[A].StartPos < Slot.AnimTrack.AnimSegments[B].StartPos;
		});

		for (int32 SegIdx : Order)
		{
			const FAnimSegment& Seg = Slot.AnimTrack.AnimSegments[SegIdx];
			const FTransform SegTotal = ExtractSegmentRootMotionTotal(Seg);
			TSharedPtr<FJsonObject> SegJson = RootMotionDeltaToJson(SegTotal);
			SegJson->SetNumberField(TEXT("segment_index"), SegIdx);
			Segments.Add(MakeShareable(new FJsonValueObject(SegJson)));
			SlotTotal = SegTotal * SlotTotal;
		}
		SlotObj->SetArrayField(TEXT("segments"), Segments);
		SlotObj->SetObjectField(TEXT("slot_total"), RootMotionDeltaToJson(SlotTotal));
		SlotsJson.Add(MakeShareable(new FJsonValueObject(SlotObj)));

		// Treat slot[0] as the driver for a rough montage total. More accurate
		// composition across slots would require knowing which slot wins for
		// root motion in the consumer AnimBP, which we can't know here.
		if (SlotIdx == 0)
		{
			MontageTotal = SlotTotal;
		}
	}

	Out->SetArrayField(TEXT("slots"), SlotsJson);
	Out->SetObjectField(TEXT("montage_total_from_slot0"), RootMotionDeltaToJson(MontageTotal));
	return Out;
}

// Evaluate a float curve at a specific time via FFloatCurve::FloatCurve::Eval.
static double EvalFloatCurveAtTime(const FFloatCurve& Curve, float Time)
{
	return Curve.FloatCurve.Eval(Time);
}

// Sample every float curve at a set of labeled times + uniform grid.
// Returns an array of { name, samples: [{time, value, label?}] }.
static TArray<TSharedPtr<FJsonValue>> SampleCurves(const UAnimSequenceBase* AnimBase, const TSharedPtr<FJsonObject>& Params)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!AnimBase) return Out;

	const IAnimationDataModel* Model = AnimBase->GetDataModel();
	if (!Model) return Out;

	const TArray<FFloatCurve>& Curves = Model->GetFloatCurves();
	if (Curves.Num() == 0) return Out;

	const float PlayLength = AnimBase->GetPlayLength();

	// Build labeled times: sections, notify trigger, notify end.
	struct FLabeledTime { float Time; FString Label; };
	TArray<FLabeledTime> Labeled;

	if (const UAnimMontage* Montage = Cast<const UAnimMontage>(AnimBase))
	{
		for (const FCompositeSection& Section : Montage->CompositeSections)
		{
			Labeled.Add({ Section.GetTime(), FString::Printf(TEXT("section:%s"), *Section.SectionName.ToString()) });
		}
	}
	for (int32 i = 0; i < AnimBase->Notifies.Num(); ++i)
	{
		const FAnimNotifyEvent& N = AnimBase->Notifies[i];
		const FString NotifyName = N.NotifyName.ToString();
		Labeled.Add({ N.GetTriggerTime(), FString::Printf(TEXT("notify[%d]:%s"), i, *NotifyName) });
		if (N.NotifyStateClass && N.GetDuration() > 0.0f)
		{
			Labeled.Add({
				N.GetTriggerTime() + N.GetDuration(),
				FString::Printf(TEXT("notify[%d]:%s:end"), i, *NotifyName)
			});
		}
	}

	// Uniform grid.
	int32 GridCount = 16;
	if (Params.IsValid() && Params->HasField(TEXT("curve_sample_count")))
	{
		GridCount = FMath::Max(0, static_cast<int32>(Params->GetNumberField(TEXT("curve_sample_count"))));
	}
	if (GridCount > 0 && PlayLength > 0.0f)
	{
		for (int32 i = 0; i < GridCount; ++i)
		{
			const float Frac = GridCount == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(GridCount - 1);
			Labeled.Add({ Frac * PlayLength, FString() });
		}
	}

	// Sort by time for deterministic output.
	Labeled.Sort([](const FLabeledTime& A, const FLabeledTime& B) { return A.Time < B.Time; });

	for (const FFloatCurve& Curve : Curves)
	{
		TSharedPtr<FJsonObject> CurveObj = MakeShareable(new FJsonObject);
		CurveObj->SetStringField(TEXT("name"), Curve.GetName().ToString());

		TArray<TSharedPtr<FJsonValue>> SampleArr;
		for (const FLabeledTime& LT : Labeled)
		{
			TSharedPtr<FJsonObject> S = MakeShareable(new FJsonObject);
			S->SetNumberField(TEXT("time"), LT.Time);
			S->SetNumberField(TEXT("value"), EvalFloatCurveAtTime(Curve, LT.Time));
			if (!LT.Label.IsEmpty())
			{
				S->SetStringField(TEXT("label"), LT.Label);
			}
			SampleArr.Add(MakeShareable(new FJsonValueObject(S)));
		}
		CurveObj->SetArrayField(TEXT("samples"), SampleArr);
		Out.Add(MakeShareable(new FJsonValueObject(CurveObj)));
	}
	return Out;
}

// Compute effective play length under current RateScale. TimeStretchCurve is
// an advanced re-mapping — for a summary value we only factor RateScale.
static double ComputeEffectivePlayLength(const UAnimMontage* Montage)
{
	if (!Montage) return 0.0;
	const float PlayLength = Montage->GetPlayLength();
	const float Rate = FMath::Abs(Montage->RateScale) > KINDA_SMALL_NUMBER ? FMath::Abs(Montage->RateScale) : 1.0f;
	return static_cast<double>(PlayLength) / static_cast<double>(Rate);
}

// Build the warnings array for a montage.
static TArray<TSharedPtr<FJsonValue>> BuildMontageWarnings(const UAnimMontage* Montage, const TSet<FString>& Filter)
{
	TArray<TSharedPtr<FJsonValue>> Warnings;
	if (!Montage) return Warnings;

	const float PlayLength = Montage->GetPlayLength();

	// Segment overlap + empty slot.
	for (int32 SlotIdx = 0; SlotIdx < Montage->SlotAnimTracks.Num(); ++SlotIdx)
	{
		const FSlotAnimationTrack& Slot = Montage->SlotAnimTracks[SlotIdx];
		if (Slot.AnimTrack.AnimSegments.Num() == 0)
		{
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::EmptySlot, TEXT("info"),
				FString::Printf(TEXT("Slot %d '%s' has no segments"), SlotIdx, *Slot.SlotName.ToString()));
			W->SetNumberField(TEXT("slot_index"), SlotIdx);
			W->SetStringField(TEXT("slot_name"), Slot.SlotName.ToString());
			AppendWarning(Warnings, Filter, W);
			continue;
		}

		for (int32 A = 0; A < Slot.AnimTrack.AnimSegments.Num(); ++A)
		{
			const FAnimSegment& SegA = Slot.AnimTrack.AnimSegments[A];
			const float AStart = SegA.StartPos;
			const float AEnd   = SegA.StartPos + SegA.GetLength();
			for (int32 B = A + 1; B < Slot.AnimTrack.AnimSegments.Num(); ++B)
			{
				const FAnimSegment& SegB = Slot.AnimTrack.AnimSegments[B];
				const float BStart = SegB.StartPos;
				const float BEnd   = SegB.StartPos + SegB.GetLength();
				const float OverlapStart = FMath::Max(AStart, BStart);
				const float OverlapEnd   = FMath::Min(AEnd, BEnd);
				if (OverlapEnd > OverlapStart + KINDA_SMALL_NUMBER)
				{
					TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::SegmentOverlap, TEXT("error"),
						FString::Printf(TEXT("Slot %d segments %d and %d overlap [%.4f, %.4f]"),
							SlotIdx, A, B, OverlapStart, OverlapEnd));
					W->SetNumberField(TEXT("slot_index"), SlotIdx);
					W->SetNumberField(TEXT("segment_index_a"), A);
					W->SetNumberField(TEXT("segment_index_b"), B);
					W->SetNumberField(TEXT("overlap_start"), OverlapStart);
					W->SetNumberField(TEXT("overlap_end"), OverlapEnd);
					AppendWarning(Warnings, Filter, W);
				}
			}
		}

		// Gaps: walk sorted union of spans and report holes.
		struct FSpan { float Start; float End; };
		TArray<FSpan> Spans;
		for (const FAnimSegment& Seg : Slot.AnimTrack.AnimSegments)
		{
			const float S = FMath::Clamp(Seg.StartPos, 0.0f, PlayLength);
			const float E = FMath::Clamp(Seg.StartPos + Seg.GetLength(), 0.0f, PlayLength);
			if (E > S) Spans.Add({ S, E });
		}
		Spans.Sort([](const FSpan& A, const FSpan& B) { return A.Start < B.Start; });
		float Cursor = 0.0f;
		auto EmitGap = [&](float Start, float End)
		{
			if (End - Start <= KINDA_SMALL_NUMBER) return;
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::SegmentGap, TEXT("warning"),
				FString::Printf(TEXT("Slot %d '%s' has a gap [%.4f, %.4f]"),
					SlotIdx, *Slot.SlotName.ToString(), Start, End));
			W->SetNumberField(TEXT("slot_index"), SlotIdx);
			W->SetStringField(TEXT("slot_name"), Slot.SlotName.ToString());
			W->SetNumberField(TEXT("gap_start"), Start);
			W->SetNumberField(TEXT("gap_end"), End);
			AppendWarning(Warnings, Filter, W);
		};
		for (const FSpan& S : Spans)
		{
			if (S.Start > Cursor) EmitGap(Cursor, S.Start);
			Cursor = FMath::Max(Cursor, S.End);
		}
		if (Cursor < PlayLength) EmitGap(Cursor, PlayLength);
	}

	// Notify checks.
	const TArray<FAnimNotifyTrack>& Tracks = Montage->AnimNotifyTracks;
	TArray<int32> NotifyCountByTrack;
	NotifyCountByTrack.SetNumZeroed(Tracks.Num());
	for (int32 i = 0; i < Montage->Notifies.Num(); ++i)
	{
		const FAnimNotifyEvent& N = Montage->Notifies[i];
		const float Trigger = N.GetTriggerTime();
		const float End     = Trigger + N.GetDuration();

		if (Tracks.IsValidIndex(N.TrackIndex))
		{
			NotifyCountByTrack[N.TrackIndex]++;
		}
		else
		{
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::NotifyTrackIndexInvalid, TEXT("error"),
				FString::Printf(TEXT("Notify %d '%s' has invalid track_index %d (max %d)"),
					i, *N.NotifyName.ToString(), N.TrackIndex, Tracks.Num() - 1));
			W->SetNumberField(TEXT("notify_index"), i);
			W->SetNumberField(TEXT("track_index"), N.TrackIndex);
			W->SetNumberField(TEXT("max_valid"), Tracks.Num() - 1);
			AppendWarning(Warnings, Filter, W);
		}

		if (Trigger > PlayLength + KINDA_SMALL_NUMBER || End > PlayLength + KINDA_SMALL_NUMBER)
		{
			const float Overshoot = FMath::Max(Trigger, End) - PlayLength;
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::NotifyPastMontageEnd, TEXT("error"),
				FString::Printf(TEXT("Notify %d '%s' ends past montage length (overshoot %.4f)"),
					i, *N.NotifyName.ToString(), Overshoot));
			W->SetNumberField(TEXT("notify_index"), i);
			W->SetNumberField(TEXT("overshoot_seconds"), Overshoot);
			AppendWarning(Warnings, Filter, W);
		}

		// Is the notify's time-range covered by at least one segment on any slot?
		auto HasCoverage = [&](float T) -> bool
		{
			for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
			{
				for (const FAnimSegment& Seg : Slot.AnimTrack.AnimSegments)
				{
					const float S = Seg.StartPos;
					const float E = Seg.StartPos + Seg.GetLength();
					if (T >= S - KINDA_SMALL_NUMBER && T <= E + KINDA_SMALL_NUMBER) return true;
				}
			}
			return false;
		};
		if (!HasCoverage(Trigger) || (N.NotifyStateClass && N.GetDuration() > 0.0f && !HasCoverage(End)))
		{
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::NotifyOutsideSegments, TEXT("warning"),
				FString::Printf(TEXT("Notify %d '%s' at t=%.4f falls where no segment is active"),
					i, *N.NotifyName.ToString(), Trigger));
			W->SetNumberField(TEXT("notify_index"), i);
			W->SetStringField(TEXT("notify_name"), N.NotifyName.ToString());
			W->SetNumberField(TEXT("trigger_time"), Trigger);
			AppendWarning(Warnings, Filter, W);
		}
	}

	// Same time + same track collision.
	const float SameTimeEpsilon = 1e-3f;
	for (int32 i = 0; i < Montage->Notifies.Num(); ++i)
	{
		const FAnimNotifyEvent& A = Montage->Notifies[i];
		for (int32 j = i + 1; j < Montage->Notifies.Num(); ++j)
		{
			const FAnimNotifyEvent& B = Montage->Notifies[j];
			if (A.TrackIndex != B.TrackIndex) continue;
			const float Delta = FMath::Abs(A.GetTriggerTime() - B.GetTriggerTime());
			if (Delta <= SameTimeEpsilon)
			{
				TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::NotifySameTimeSameTrack, TEXT("info"),
					FString::Printf(TEXT("Notifies %d and %d are within %.4fs on the same track (%d)"),
						i, j, Delta, A.TrackIndex));
				TArray<TSharedPtr<FJsonValue>> Indices;
				Indices.Add(MakeShareable(new FJsonValueNumber(i)));
				Indices.Add(MakeShareable(new FJsonValueNumber(j)));
				W->SetArrayField(TEXT("indices"), Indices);
				W->SetNumberField(TEXT("delta"), Delta);
				W->SetNumberField(TEXT("track_index"), A.TrackIndex);
				AppendWarning(Warnings, Filter, W);
			}
		}
	}

	// Orphan notify tracks.
	for (int32 i = 0; i < Tracks.Num(); ++i)
	{
		if (NotifyCountByTrack.IsValidIndex(i) && NotifyCountByTrack[i] == 0)
		{
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::OrphanNotifyTrack, TEXT("info"),
				FString::Printf(TEXT("Notify track %d '%s' has no notifies"), i, *Tracks[i].TrackName.ToString()));
			W->SetNumberField(TEXT("track_index"), i);
			W->SetStringField(TEXT("track_name"), Tracks[i].TrackName.ToString());
			AppendWarning(Warnings, Filter, W);
		}
	}

	// Section links and time-out-of-range.
	TSet<FName> SectionNames;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		SectionNames.Add(Section.SectionName);
	}
	for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
	{
		const FCompositeSection& Section = Montage->CompositeSections[i];
		if (Section.NextSectionName != NAME_None && !SectionNames.Contains(Section.NextSectionName))
		{
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::SectionNextBroken, TEXT("error"),
				FString::Printf(TEXT("Section %d '%s' links to non-existent next '%s'"),
					i, *Section.SectionName.ToString(), *Section.NextSectionName.ToString()));
			W->SetNumberField(TEXT("section_index"), i);
			W->SetStringField(TEXT("section_name"), Section.SectionName.ToString());
			W->SetStringField(TEXT("bad_next"), Section.NextSectionName.ToString());
			AppendWarning(Warnings, Filter, W);
		}
		if (Section.GetTime() >= PlayLength - KINDA_SMALL_NUMBER && PlayLength > 0.0f)
		{
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::SectionTimeOutOfRange, TEXT("warning"),
				FString::Printf(TEXT("Section %d '%s' at %.4fs is at or past play length %.4f"),
					i, *Section.SectionName.ToString(), Section.GetTime(), PlayLength));
			W->SetNumberField(TEXT("section_index"), i);
			W->SetNumberField(TEXT("time"), Section.GetTime());
			W->SetNumberField(TEXT("play_length"), PlayLength);
			AppendWarning(Warnings, Filter, W);
		}
	}

	// Sync slot valid.
	if (Montage->SlotAnimTracks.Num() > 0 && !Montage->SlotAnimTracks.IsValidIndex(Montage->SyncSlotIndex))
	{
		TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::SyncSlotInvalid, TEXT("error"),
			FString::Printf(TEXT("SyncSlotIndex %d is out of range [0, %d)"),
				Montage->SyncSlotIndex, Montage->SlotAnimTracks.Num()));
		W->SetNumberField(TEXT("sync_slot_index"), Montage->SyncSlotIndex);
		W->SetNumberField(TEXT("slot_count"), Montage->SlotAnimTracks.Num());
		AppendWarning(Warnings, Filter, W);
	}

	// TimeStretchCurveName refers to a curve that exists?
	if (Montage->TimeStretchCurveName != NAME_None)
	{
		bool bFound = false;
		if (const IAnimationDataModel* Model = Montage->GetDataModel())
		{
			for (const FFloatCurve& C : Model->GetFloatCurves())
			{
				if (C.GetName() == Montage->TimeStretchCurveName) { bFound = true; break; }
			}
		}
		if (!bFound)
		{
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::TimeStretchCurveMissing, TEXT("error"),
				FString::Printf(TEXT("TimeStretchCurveName '%s' does not exist on this montage"),
					*Montage->TimeStretchCurveName.ToString()));
			W->SetStringField(TEXT("curve_name"), Montage->TimeStretchCurveName.ToString());
			AppendWarning(Warnings, Filter, W);
		}
	}

	return Warnings;
}

// Build a smaller warnings set for UAnimSequence (no segments/sections/slots).
static TArray<TSharedPtr<FJsonValue>> BuildSequenceWarnings(const UAnimSequence* Seq, const TSet<FString>& Filter)
{
	TArray<TSharedPtr<FJsonValue>> Warnings;
	if (!Seq) return Warnings;

	const float PlayLength = Seq->GetPlayLength();
	const TArray<FAnimNotifyTrack>& Tracks = Seq->AnimNotifyTracks;

	for (int32 i = 0; i < Seq->Notifies.Num(); ++i)
	{
		const FAnimNotifyEvent& N = Seq->Notifies[i];
		const float Trigger = N.GetTriggerTime();
		const float End = Trigger + N.GetDuration();

		if (!Tracks.IsValidIndex(N.TrackIndex))
		{
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::NotifyTrackIndexInvalid, TEXT("error"),
				FString::Printf(TEXT("Notify %d '%s' has invalid track_index %d"),
					i, *N.NotifyName.ToString(), N.TrackIndex));
			W->SetNumberField(TEXT("notify_index"), i);
			W->SetNumberField(TEXT("track_index"), N.TrackIndex);
			AppendWarning(Warnings, Filter, W);
		}
		if (Trigger > PlayLength + KINDA_SMALL_NUMBER || End > PlayLength + KINDA_SMALL_NUMBER)
		{
			const float Overshoot = FMath::Max(Trigger, End) - PlayLength;
			TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::NotifyPastMontageEnd, TEXT("error"),
				FString::Printf(TEXT("Notify %d '%s' overshoots sequence end by %.4f"),
					i, *N.NotifyName.ToString(), Overshoot));
			W->SetNumberField(TEXT("notify_index"), i);
			W->SetNumberField(TEXT("overshoot_seconds"), Overshoot);
			AppendWarning(Warnings, Filter, W);
		}
	}

	const float SameTimeEpsilon = 1e-3f;
	for (int32 i = 0; i < Seq->Notifies.Num(); ++i)
	{
		const FAnimNotifyEvent& A = Seq->Notifies[i];
		for (int32 j = i + 1; j < Seq->Notifies.Num(); ++j)
		{
			const FAnimNotifyEvent& B = Seq->Notifies[j];
			if (A.TrackIndex != B.TrackIndex) continue;
			const float Delta = FMath::Abs(A.GetTriggerTime() - B.GetTriggerTime());
			if (Delta <= SameTimeEpsilon)
			{
				TSharedPtr<FJsonObject> W = MakeWarning(AnalyzeWarnings::NotifySameTimeSameTrack, TEXT("info"),
					FString::Printf(TEXT("Notifies %d and %d are within %.4fs on the same track"),
						i, j, Delta));
				TArray<TSharedPtr<FJsonValue>> Indices;
				Indices.Add(MakeShareable(new FJsonValueNumber(i)));
				Indices.Add(MakeShareable(new FJsonValueNumber(j)));
				W->SetArrayField(TEXT("indices"), Indices);
				W->SetNumberField(TEXT("delta"), Delta);
				AppendWarning(Warnings, Filter, W);
			}
		}
	}

	return Warnings;
}

// Root motion summary for a UAnimSequence: total over the full play range.
static TSharedPtr<FJsonObject> ComputeSequenceRootMotionSummary(const UAnimSequence* Seq)
{
	if (!Seq) return MakeShareable(new FJsonObject);
	const float PlayLength = Seq->GetPlayLength();
	const FAnimExtractContext TotalRootMotionContext(0.0, /*bExtractRootMotion=*/true, FDeltaTimeRecord(PlayLength), /*bLooping=*/false);
	const FTransform Total = Seq->ExtractRootMotion(TotalRootMotionContext);
	return RootMotionDeltaToJson(Total);
}

// ============================================================================
// analyze_anim_montage
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAnalyzeAnimMontage(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	UAnimMontage* Montage = Cast<UAnimMontage>(LoadedAsset);
	if (!Montage)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UAnimMontage: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), Montage->GetName());

	// Basic montage properties
	Result->SetNumberField(TEXT("play_length"), Montage->GetPlayLength());
	Result->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
	Result->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());
	Result->SetStringField(TEXT("blend_in_mode"), AlphaBlendOptionToString(Montage->BlendIn.GetBlendOption()));
	Result->SetStringField(TEXT("blend_out_mode"), AlphaBlendOptionToString(Montage->BlendOut.GetBlendOption()));
	Result->SetNumberField(TEXT("blend_out_trigger_time"), Montage->BlendOutTriggerTime);
	Result->SetBoolField(TEXT("enable_auto_blend_out"), Montage->bEnableAutoBlendOut);
	Result->SetStringField(TEXT("blend_profile_in"),
		Montage->BlendProfileIn ? Montage->BlendProfileIn->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("blend_profile_out"),
		Montage->BlendProfileOut ? Montage->BlendProfileOut->GetPathName() : TEXT("None"));

	// Root motion
	Result->SetStringField(TEXT("root_motion_root_lock"),
		RootMotionRootLockToString(Montage->RootMotionRootLock));
	Result->SetBoolField(TEXT("enable_root_motion_translation"), Montage->bEnableRootMotionTranslation);
	Result->SetBoolField(TEXT("enable_root_motion_rotation"), Montage->bEnableRootMotionRotation);

	// Sync group
	Result->SetStringField(TEXT("sync_group"), Montage->SyncGroup.ToString());
	Result->SetNumberField(TEXT("sync_slot_index"), Montage->SyncSlotIndex);

	// Playback defaults (inherited from UAnimSequenceBase)
	Result->SetNumberField(TEXT("rate_scale"), Montage->RateScale);
	Result->SetBoolField(TEXT("loop"), Montage->bLoop);

	// Blend modes (Standard vs Inertialization)
	Result->SetStringField(TEXT("blend_mode_in"), MontageBlendModeToString(Montage->BlendModeIn));
	Result->SetStringField(TEXT("blend_mode_out"), MontageBlendModeToString(Montage->BlendModeOut));

	// Time stretch curve — name + tuning fields via reflection
	Result->SetStringField(TEXT("time_stretch_curve_name"), Montage->TimeStretchCurveName.ToString());
	{
		TSharedPtr<FJsonObject> StretchObj = MakeShareable(new FJsonObject);
		if (FJsonObjectConverter::UStructToJsonObject(
			FTimeStretchCurve::StaticStruct(),
			&Montage->TimeStretchCurve,
			StretchObj.ToSharedRef(),
			CPF_Edit, 0))
		{
			Result->SetObjectField(TEXT("time_stretch_curve"), StretchObj);
		}
	}

#if WITH_EDITORONLY_DATA
	Result->SetStringField(TEXT("preview_base_pose"),
		Montage->PreviewBasePose ? Montage->PreviewBasePose->GetPathName() : TEXT("None"));
#endif

	// Composite sections
	TArray<TSharedPtr<FJsonValue>> SectionsArray;
	for (int32 SectionIdx = 0; SectionIdx < Montage->CompositeSections.Num(); ++SectionIdx)
	{
		const FCompositeSection& Section = Montage->CompositeSections[SectionIdx];
		TSharedPtr<FJsonObject> SectionObj = MakeShareable(new FJsonObject);
		SectionObj->SetNumberField(TEXT("index"), SectionIdx);
		SectionObj->SetStringField(TEXT("name"), Section.SectionName.ToString());
		SectionObj->SetNumberField(TEXT("start_time"), Section.GetTime());
		SectionObj->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
		SectionObj->SetNumberField(TEXT("link_method"), static_cast<int32>(Section.GetLinkMethod()));
		SectionObj->SetArrayField(TEXT("metadata"), BuildSectionMetaDataArray(Section));

		SectionsArray.Add(MakeShareable(new FJsonValueObject(SectionObj)));
	}
	Result->SetArrayField(TEXT("sections"), SectionsArray);

	// Slot tracks
	TArray<TSharedPtr<FJsonValue>> SlotTracksArray;
	TArray<TSharedPtr<FJsonValue>> SlotNamesArray;
	for (int32 SlotIdx = 0; SlotIdx < Montage->SlotAnimTracks.Num(); ++SlotIdx)
	{
		const FSlotAnimationTrack& Track = Montage->SlotAnimTracks[SlotIdx];
		TSharedPtr<FJsonObject> TrackObj = MakeShareable(new FJsonObject);
		TrackObj->SetNumberField(TEXT("slot_index"), SlotIdx);
		TrackObj->SetStringField(TEXT("slot_name"), Track.SlotName.ToString());
		TrackObj->SetNumberField(TEXT("coverage_fraction"),
			ComputeSlotCoverageFraction(Track, Montage->GetPlayLength()));

		SlotNamesArray.Add(MakeShareable(new FJsonValueString(Track.SlotName.ToString())));

		TArray<TSharedPtr<FJsonValue>> SegmentsArray;
		for (int32 SegIdx = 0; SegIdx < Track.AnimTrack.AnimSegments.Num(); ++SegIdx)
		{
			const FAnimSegment& Seg = Track.AnimTrack.AnimSegments[SegIdx];
			TSharedPtr<FJsonObject> SegObj = MakeShareable(new FJsonObject);
			SegObj->SetNumberField(TEXT("segment_index"), SegIdx);
			SegObj->SetNumberField(TEXT("start_pos"), Seg.StartPos);
			SegObj->SetNumberField(TEXT("anim_start_time"), Seg.AnimStartTime);
			SegObj->SetNumberField(TEXT("anim_end_time"), Seg.AnimEndTime);
			SegObj->SetNumberField(TEXT("anim_play_rate"), Seg.AnimPlayRate);
			SegObj->SetNumberField(TEXT("looping_count"), Seg.LoopingCount);
			SegObj->SetNumberField(TEXT("segment_end_pos"), Seg.StartPos + Seg.GetLength());

			UAnimSequenceBase* AnimRef = Seg.GetAnimReference();
			SegObj->SetStringField(TEXT("anim_reference"), AnimRef ? AnimRef->GetPathName() : TEXT("None"));

			SegmentsArray.Add(MakeShareable(new FJsonValueObject(SegObj)));
		}
		TrackObj->SetArrayField(TEXT("segments"), SegmentsArray);

		SlotTracksArray.Add(MakeShareable(new FJsonValueObject(TrackObj)));
	}
	Result->SetArrayField(TEXT("slot_tracks"), SlotTracksArray);
	Result->SetArrayField(TEXT("slot_names"), SlotNamesArray);

	// Notifies (inherited from UAnimSequenceBase)
	Result->SetArrayField(TEXT("notifies"), BuildNotifiesArray(Montage));
	Result->SetArrayField(TEXT("notify_tracks"), BuildNotifyTracksArray(Montage));

	// ----- Derived analytics -----
	const TSet<FString> WarningFilter = BuildWarningFilterSet(Params);
	Result->SetArrayField(TEXT("warnings"), BuildMontageWarnings(Montage, WarningFilter));
	Result->SetObjectField(TEXT("notify_summary"), ComputeNotifyDistribution(Montage));
	Result->SetObjectField(TEXT("root_motion_summary"), ComputeMontageRootMotionSummary(Montage));
	Result->SetArrayField(TEXT("curve_samples"), SampleCurves(Montage, Params));
	Result->SetNumberField(TEXT("effective_play_length"), ComputeEffectivePlayLength(Montage));

	return Result;
}

// ============================================================================
// analyze_anim_sequence
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAnalyzeAnimSequence(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	UAnimSequence* Sequence = Cast<UAnimSequence>(LoadedAsset);
	if (!Sequence)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a UAnimSequence: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), Sequence->GetName());

	// Basic sequence properties
	Result->SetNumberField(TEXT("play_length"), Sequence->GetPlayLength());
	Result->SetNumberField(TEXT("sampling_frame_rate"), Sequence->GetSamplingFrameRate().AsDecimal());
	Result->SetNumberField(TEXT("num_frames"), Sequence->GetNumberOfSampledKeys());

	// Root-motion flags (sequence-level; drive modern extraction regardless of montage flags)
	Result->SetBoolField(TEXT("enable_root_motion"), Sequence->bEnableRootMotion);
	Result->SetStringField(TEXT("root_motion_root_lock"), RootMotionRootLockToString(Sequence->RootMotionRootLock));
	Result->SetBoolField(TEXT("force_root_lock"), Sequence->bForceRootLock);
	Result->SetBoolField(TEXT("use_normalized_root_motion_scale"), Sequence->bUseNormalizedRootMotionScale);

	// Interpolation / additive / compression (drive pose evaluation — mis-set values can cause skating or pose drift)
	Result->SetStringField(TEXT("interpolation"), AnimInterpolationTypeToString(Sequence->Interpolation));
	Result->SetStringField(TEXT("additive_anim_type"), AdditiveAnimTypeToString(Sequence->AdditiveAnimType));
	Result->SetStringField(TEXT("ref_pose_type"), AdditiveBasePoseTypeToString(Sequence->RefPoseType));
	Result->SetNumberField(TEXT("ref_frame_index"), Sequence->RefFrameIndex);
	Result->SetStringField(TEXT("ref_pose_seq"),
		Sequence->RefPoseSeq ? Sequence->RefPoseSeq->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("bone_compression_settings"),
		Sequence->BoneCompressionSettings ? Sequence->BoneCompressionSettings->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("curve_compression_settings"),
		Sequence->CurveCompressionSettings ? Sequence->CurveCompressionSettings->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("variable_frame_stripping_settings"),
		Sequence->VariableFrameStrippingSettings ? Sequence->VariableFrameStrippingSettings->GetPathName() : TEXT("None"));

	// Bone track names via the data model (non-deprecated API)
	TArray<TSharedPtr<FJsonValue>> BoneTrackNamesArray;
	int32 NumBoneTracks = 0;
	if (const IAnimationDataModel* DataModel = Sequence->GetDataModel())
	{
		NumBoneTracks = DataModel->GetNumBoneTracks();
		TArray<FName> TrackNames;
		DataModel->GetBoneTrackNames(TrackNames);
		for (const FName& TrackName : TrackNames)
		{
			BoneTrackNamesArray.Add(MakeShareable(new FJsonValueString(TrackName.ToString())));
		}
	}
	Result->SetNumberField(TEXT("num_tracks"), NumBoneTracks);
	Result->SetArrayField(TEXT("bone_track_names"), BoneTrackNamesArray);

	// Notifies (inherited from UAnimSequenceBase)
	Result->SetArrayField(TEXT("notifies"), BuildNotifiesArray(Sequence));
	Result->SetArrayField(TEXT("notify_tracks"), BuildNotifyTracksArray(Sequence));

	// Derived analytics
	{
		const TSet<FString> WarningFilter = BuildWarningFilterSet(Params);
		Result->SetArrayField(TEXT("warnings"), BuildSequenceWarnings(Sequence, WarningFilter));
		Result->SetArrayField(TEXT("curve_samples"), SampleCurves(Sequence, Params));
		Result->SetObjectField(TEXT("root_motion_summary"), ComputeSequenceRootMotionSummary(Sequence));
	}

	// Curves via data model
	TArray<TSharedPtr<FJsonValue>> CurvesArray;
	if (const IAnimationDataModel* DataModel = Sequence->GetDataModel())
	{
		const TArray<FFloatCurve>& FloatCurves = DataModel->GetFloatCurves();
		for (const FFloatCurve& Curve : FloatCurves)
		{
			TSharedPtr<FJsonObject> CurveObj = MakeShareable(new FJsonObject);
			CurveObj->SetStringField(TEXT("name"), Curve.GetName().ToString());
			CurveObj->SetNumberField(TEXT("num_keys"), Curve.FloatCurve.GetNumKeys());
			CurvesArray.Add(MakeShareable(new FJsonValueObject(CurveObj)));
		}
	}
	Result->SetArrayField(TEXT("curves"), CurvesArray);

	return Result;
}

// ============================================================================
// list_animation_assets
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleListAnimationAssets(const TSharedPtr<FJsonObject>& Params)
{
	FString Directory = TEXT("/Game/");
	if (Params->HasField(TEXT("directory")))
	{
		Directory = Params->GetStringField(TEXT("directory"));
	}

	int32 Limit = 100;
	if (Params->HasField(TEXT("limit")))
	{
		Limit = static_cast<int32>(Params->GetNumberField(TEXT("limit")));
		if (Limit <= 0)
		{
			Limit = 100;
		}
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Directory));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UAnimMontage::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UBlendSpace::StaticClass()->GetClassPathName());

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	const int32 TotalFound = AssetDataList.Num();

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	const int32 Count = FMath::Min(TotalFound, Limit);
	for (int32 i = 0; i < Count; ++i)
	{
		const FAssetData& AssetData = AssetDataList[i];

		TSharedPtr<FJsonObject> AssetObj = MakeShareable(new FJsonObject);
		AssetObj->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("asset_path"), AssetData.PackageName.ToString());
		AssetObj->SetStringField(TEXT("asset_type"), AssetData.AssetClassPath.GetAssetName().ToString());

		AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetNumberField(TEXT("total_found"), TotalFound);
	Result->SetNumberField(TEXT("returned"), Count);
	Result->SetArrayField(TEXT("assets"), AssetsArray);

	return Result;
}

// ============================================================================
// Helpers: JSON <-> Vector/Rotator
// ============================================================================

static FVector JsonToVector(const TSharedPtr<FJsonObject>& Obj, const FVector& Default = FVector::ZeroVector)
{
	if (!Obj.IsValid()) return Default;
	return FVector(
		Obj->GetNumberField(TEXT("x")),
		Obj->GetNumberField(TEXT("y")),
		Obj->GetNumberField(TEXT("z"))
	);
}

static FRotator JsonToRotator(const TSharedPtr<FJsonObject>& Obj, const FRotator& Default = FRotator::ZeroRotator)
{
	if (!Obj.IsValid()) return Default;
	return FRotator(
		Obj->GetNumberField(TEXT("pitch")),
		Obj->GetNumberField(TEXT("yaw")),
		Obj->GetNumberField(TEXT("roll"))
	);
}

// ============================================================================
// Helper: Load USkeleton from asset_path param
// ============================================================================

static USkeleton* LoadSkeleton(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutError)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
		return nullptr;
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!LoadedAsset)
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
		return nullptr;
	}

	// Try direct cast to USkeleton
	USkeleton* Skeleton = Cast<USkeleton>(LoadedAsset);
	if (Skeleton)
	{
		return Skeleton;
	}

	// If it's a USkeletalMesh, get its skeleton
	USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(LoadedAsset);
	if (SkelMesh)
	{
		Skeleton = SkelMesh->GetSkeleton();
		if (Skeleton)
		{
			return Skeleton;
		}
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("SkeletalMesh '%s' has no skeleton"), *AssetPath));
		return nullptr;
	}

	OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a USkeleton or USkeletalMesh: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	return nullptr;
}

// ============================================================================
// Helper: Load UAnimMontage from "asset_path" param
// ============================================================================

static UAnimMontage* LoadMontage(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutError)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
		return nullptr;
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!LoadedAsset)
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
		return nullptr;
	}

	UAnimMontage* Montage = Cast<UAnimMontage>(LoadedAsset);
	if (!Montage)
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UAnimMontage: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
		return nullptr;
	}
	return Montage;
}

// ============================================================================
// get_skeletal_mesh_info
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleGetSkeletalMeshInfo(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const bool bIncludeBones = !Params->HasField(TEXT("include_bones")) || Params->GetBoolField(TEXT("include_bones"));

	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(LoadedAsset);
	if (!SkelMesh)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a USkeletalMesh: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), SkelMesh->GetName());

	USkeleton* Skeleton = SkelMesh->GetSkeleton();
	if (Skeleton)
	{
		Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	}

	const FReferenceSkeleton& RefSkeleton = SkelMesh->GetRefSkeleton();
	const int32 NumBones = RefSkeleton.GetNum();
	Result->SetNumberField(TEXT("num_bones"), NumBones);
	Result->SetNumberField(TEXT("num_lods"), SkelMesh->GetLODNum());

	const TArray<FSkeletalMaterial>& Materials = SkelMesh->GetMaterials();
	Result->SetNumberField(TEXT("num_materials"), Materials.Num());

	// Sockets (combined mesh + skeleton)
	TArray<USkeletalMeshSocket*> ActiveSockets = SkelMesh->GetActiveSocketList();
	Result->SetNumberField(TEXT("num_sockets"), ActiveSockets.Num());

	TArray<TSharedPtr<FJsonValue>> SocketsArray;
	for (const USkeletalMeshSocket* Socket : ActiveSockets)
	{
		if (!Socket) continue;

		TSharedPtr<FJsonObject> SocketObj = MakeShareable(new FJsonObject);
		SocketObj->SetStringField(TEXT("name"), Socket->SocketName.ToString());
		SocketObj->SetStringField(TEXT("bone"), Socket->BoneName.ToString());

		TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject);
		LocObj->SetNumberField(TEXT("x"), Socket->RelativeLocation.X);
		LocObj->SetNumberField(TEXT("y"), Socket->RelativeLocation.Y);
		LocObj->SetNumberField(TEXT("z"), Socket->RelativeLocation.Z);
		SocketObj->SetObjectField(TEXT("location"), LocObj);

		TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject);
		RotObj->SetNumberField(TEXT("pitch"), Socket->RelativeRotation.Pitch);
		RotObj->SetNumberField(TEXT("yaw"), Socket->RelativeRotation.Yaw);
		RotObj->SetNumberField(TEXT("roll"), Socket->RelativeRotation.Roll);
		SocketObj->SetObjectField(TEXT("rotation"), RotObj);

		TSharedPtr<FJsonObject> ScaleObj = MakeShareable(new FJsonObject);
		ScaleObj->SetNumberField(TEXT("x"), Socket->RelativeScale.X);
		ScaleObj->SetNumberField(TEXT("y"), Socket->RelativeScale.Y);
		ScaleObj->SetNumberField(TEXT("z"), Socket->RelativeScale.Z);
		SocketObj->SetObjectField(TEXT("scale"), ScaleObj);

		SocketsArray.Add(MakeShareable(new FJsonValueObject(SocketObj)));
	}
	Result->SetArrayField(TEXT("sockets"), SocketsArray);

	if (bIncludeBones)
	{
		TArray<TSharedPtr<FJsonValue>> BonesArray;
		for (int32 i = 0; i < NumBones; ++i)
		{
			TSharedPtr<FJsonObject> BoneObj = MakeShareable(new FJsonObject);
			BoneObj->SetNumberField(TEXT("index"), i);
			BoneObj->SetStringField(TEXT("name"), RefSkeleton.GetBoneName(i).ToString());
			BoneObj->SetNumberField(TEXT("parent_index"), RefSkeleton.GetParentIndex(i));
			BonesArray.Add(MakeShareable(new FJsonValueObject(BoneObj)));
		}
		Result->SetArrayField(TEXT("bones"), BonesArray);
	}

	return Result;
}

// ============================================================================
// add_socket_to_skeleton
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAddSocketToSkeleton(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	USkeleton* Skeleton = LoadSkeleton(Params, Error);
	if (!Skeleton) return Error;

	if (!Params->HasField(TEXT("socket_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: socket_name"));
	if (!Params->HasField(TEXT("bone_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: bone_name"));

	const FString SocketName = Params->GetStringField(TEXT("socket_name"));
	const FString BoneName = Params->GetStringField(TEXT("bone_name"));

	// Check if socket already exists
	if (Skeleton->FindSocket(FName(*SocketName)))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Socket '%s' already exists on skeleton"), *SocketName));
	}

	// Validate bone exists
	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	if (RefSkel.FindBoneIndex(FName(*BoneName)) == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Bone '%s' not found on skeleton"), *BoneName));
	}

	// Create socket
	USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(Skeleton);
	NewSocket->SocketName = FName(*SocketName);
	NewSocket->BoneName = FName(*BoneName);

	// Optional transform
	const TSharedPtr<FJsonObject>* LocPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocPtr) && LocPtr)
		NewSocket->RelativeLocation = JsonToVector(*LocPtr);

	const TSharedPtr<FJsonObject>* RotPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotPtr) && RotPtr)
		NewSocket->RelativeRotation = JsonToRotator(*RotPtr);

	const TSharedPtr<FJsonObject>* ScalePtr = nullptr;
	if (Params->TryGetObjectField(TEXT("scale"), ScalePtr) && ScalePtr)
		NewSocket->RelativeScale = JsonToVector(*ScalePtr, FVector::OneVector);

	Skeleton->Sockets.Add(NewSocket);
	Skeleton->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("socket_name"), SocketName);
	Result->SetStringField(TEXT("bone_name"), BoneName);
	Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());

	return Result;
}

// ============================================================================
// modify_socket
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleModifySocket(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	USkeleton* Skeleton = LoadSkeleton(Params, Error);
	if (!Skeleton) return Error;

	if (!Params->HasField(TEXT("socket_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: socket_name"));

	const FString SocketName = Params->GetStringField(TEXT("socket_name"));

	USkeletalMeshSocket* Socket = Skeleton->FindSocket(FName(*SocketName));
	if (!Socket)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Socket '%s' not found on skeleton"), *SocketName));
	}

	// Optional: change parent bone
	if (Params->HasField(TEXT("bone_name")))
	{
		const FString BoneName = Params->GetStringField(TEXT("bone_name"));
		const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
		if (RefSkel.FindBoneIndex(FName(*BoneName)) == INDEX_NONE)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Bone '%s' not found on skeleton"), *BoneName));
		}
		Socket->BoneName = FName(*BoneName);
	}

	const TSharedPtr<FJsonObject>* LocPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocPtr) && LocPtr)
		Socket->RelativeLocation = JsonToVector(*LocPtr);

	const TSharedPtr<FJsonObject>* RotPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotPtr) && RotPtr)
		Socket->RelativeRotation = JsonToRotator(*RotPtr);

	const TSharedPtr<FJsonObject>* ScalePtr = nullptr;
	if (Params->TryGetObjectField(TEXT("scale"), ScalePtr) && ScalePtr)
		Socket->RelativeScale = JsonToVector(*ScalePtr, FVector::OneVector);

	Skeleton->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("socket_name"), SocketName);
	Result->SetStringField(TEXT("bone_name"), Socket->BoneName.ToString());

	TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject);
	LocObj->SetNumberField(TEXT("x"), Socket->RelativeLocation.X);
	LocObj->SetNumberField(TEXT("y"), Socket->RelativeLocation.Y);
	LocObj->SetNumberField(TEXT("z"), Socket->RelativeLocation.Z);
	Result->SetObjectField(TEXT("location"), LocObj);

	TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject);
	RotObj->SetNumberField(TEXT("pitch"), Socket->RelativeRotation.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), Socket->RelativeRotation.Yaw);
	RotObj->SetNumberField(TEXT("roll"), Socket->RelativeRotation.Roll);
	Result->SetObjectField(TEXT("rotation"), RotObj);

	return Result;
}

// ============================================================================
// remove_socket
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleRemoveSocket(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	USkeleton* Skeleton = LoadSkeleton(Params, Error);
	if (!Skeleton) return Error;

	if (!Params->HasField(TEXT("socket_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: socket_name"));

	const FString SocketName = Params->GetStringField(TEXT("socket_name"));

	int32 FoundIndex = INDEX_NONE;
	Skeleton->FindSocketAndIndex(FName(*SocketName), FoundIndex);
	if (FoundIndex == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Socket '%s' not found on skeleton"), *SocketName));
	}

	Skeleton->Sockets.RemoveAt(FoundIndex);
	Skeleton->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_socket"), SocketName);
	Result->SetNumberField(TEXT("remaining_sockets"), Skeleton->Sockets.Num());

	return Result;
}

// ============================================================================
// preview_mesh_on_socket
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandlePreviewMeshOnSocket(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("actor_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: actor_name"));
	if (!Params->HasField(TEXT("socket_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: socket_name"));
	if (!Params->HasField(TEXT("mesh_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: mesh_path"));

	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	const FString SocketName = Params->GetStringField(TEXT("socket_name"));
	const FString MeshPath = Params->GetStringField(TEXT("mesh_path"));

	// Find actor in level
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world"));
	}

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found in level"), *ActorName));
	}

	// Find the component that owns the socket — skeletal first, then static.
	USceneComponent* AttachTargetComp = nullptr;
	if (USkeletalMeshComponent* SkelComp = TargetActor->FindComponentByClass<USkeletalMeshComponent>())
	{
		if (SkelComp->DoesSocketExist(FName(*SocketName)))
		{
			AttachTargetComp = SkelComp;
		}
	}
	if (!AttachTargetComp)
	{
		if (UStaticMeshComponent* SMComp = TargetActor->FindComponentByClass<UStaticMeshComponent>())
		{
			if (SMComp->DoesSocketExist(FName(*SocketName)))
			{
				AttachTargetComp = SMComp;
			}
		}
	}
	if (!AttachTargetComp)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Actor '%s' has no Skeletal/Static mesh component carrying socket '%s'"), *ActorName, *SocketName));
	}

	// Load the preview mesh
	UObject* MeshAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(MeshPath);
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshAsset);
	if (!StaticMesh)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load static mesh: %s"), *MeshPath));
	}

	// Spawn preview actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* PreviewActor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!PreviewActor)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to spawn preview actor"));
	}

	PreviewActor->SetActorLabel(FString::Printf(TEXT("MCP_Preview_%s_%s"), *ActorName, *SocketName));
	PreviewActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
	PreviewActor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Attach to socket
	PreviewActor->AttachToComponent(AttachTargetComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName(*SocketName));

	// Apply optional offsets
	const TSharedPtr<FJsonObject>* LocPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("location_offset"), LocPtr) && LocPtr)
		PreviewActor->SetActorRelativeLocation(JsonToVector(*LocPtr));

	const TSharedPtr<FJsonObject>* RotPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation_offset"), RotPtr) && RotPtr)
		PreviewActor->SetActorRelativeRotation(JsonToRotator(*RotPtr));

	const TSharedPtr<FJsonObject>* ScalePtr = nullptr;
	if (Params->TryGetObjectField(TEXT("scale"), ScalePtr) && ScalePtr)
		PreviewActor->SetActorRelativeScale3D(JsonToVector(*ScalePtr, FVector::OneVector));

	// Track for cleanup
	PreviewActors.FindOrAdd(ActorName).Add(PreviewActor);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("preview_actor"), PreviewActor->GetName());
	Result->SetStringField(TEXT("attached_to"), ActorName);
	Result->SetStringField(TEXT("socket"), SocketName);
	Result->SetStringField(TEXT("mesh"), MeshPath);

	return Result;
}

// ============================================================================
// clear_socket_preview
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleClearSocketPreview(const TSharedPtr<FJsonObject>& Params)
{
	int32 Cleared = 0;

	if (Params->HasField(TEXT("actor_name")))
	{
		const FString ActorName = Params->GetStringField(TEXT("actor_name"));
		TArray<TWeakObjectPtr<AActor>>* Actors = PreviewActors.Find(ActorName);
		if (Actors)
		{
			for (TWeakObjectPtr<AActor>& WeakActor : *Actors)
			{
				if (AActor* Actor = WeakActor.Get())
				{
					Actor->Destroy();
					Cleared++;
				}
			}
			PreviewActors.Remove(ActorName);
		}
	}
	else
	{
		// Clear all previews
		for (auto& KV : PreviewActors)
		{
			for (TWeakObjectPtr<AActor>& WeakActor : KV.Value)
			{
				if (AActor* Actor = WeakActor.Get())
				{
					Actor->Destroy();
					Cleared++;
				}
			}
		}
		PreviewActors.Empty();
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("cleared"), Cleared);

	return Result;
}

// ============================================================================
// capture_socket_preview
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleCaptureSocketPreview(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("skeleton_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: skeleton_path"));
	if (!Params->HasField(TEXT("socket_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: socket_name"));
	if (!Params->HasField(TEXT("mesh_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: mesh_path"));

	const FString SkeletonPath = Params->GetStringField(TEXT("skeleton_path"));
	const FString SocketName = Params->GetStringField(TEXT("socket_name"));
	const FString MeshPath = Params->GetStringField(TEXT("mesh_path"));
	const int32 Resolution = Params->HasField(TEXT("resolution"))
		? static_cast<int32>(Params->GetNumberField(TEXT("resolution")))
		: 512;

	// Load skeleton - create params with asset_path pointing to skeleton_path
	TSharedPtr<FJsonObject> SkelParams = MakeShareable(new FJsonObject);
	SkelParams->SetStringField(TEXT("asset_path"), SkeletonPath);
	TSharedPtr<FJsonObject> Error;
	USkeleton* Skeleton = LoadSkeleton(SkelParams, Error);
	if (!Skeleton) return Error;

	// Load the preview mesh
	UObject* MeshAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(MeshPath);
	if (!MeshAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load mesh: %s"), *MeshPath));
	}

	// Add the mesh as a preview attachment on the socket
	Skeleton->PreviewAttachedAssetContainer.AddAttachedObject(MeshAsset, FName(*SocketName));
	Skeleton->MarkPackageDirty();

	// Get the preview skeletal mesh for rendering
	USkeletalMesh* PreviewMesh = Skeleton->GetPreviewMesh(true);
	if (!PreviewMesh)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Skeleton has no preview mesh set"));
	}

	// Spawn temp scene for capture
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world"));
	}

	// Spawn skeletal mesh actor
	const FVector SpawnOrigin(0.0f, 0.0f, 50000.0f); // Far above to avoid overlap
	AActor* TempActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnOrigin, FRotator::ZeroRotator);
	TempActor->SetFlags(RF_Transient);

	USceneComponent* RootComp = NewObject<USceneComponent>(TempActor, TEXT("Root"));
	TempActor->SetRootComponent(RootComp);
	RootComp->RegisterComponent();

	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(TempActor, TEXT("Body"));
	SkelComp->SetupAttachment(RootComp);
	SkelComp->SetSkeletalMeshAsset(PreviewMesh);
	SkelComp->RegisterComponent();

	// Attach the preview mesh to the socket
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshAsset);
	USkeletalMesh* SkelMeshAsset = Cast<USkeletalMesh>(MeshAsset);

	USceneComponent* AttachedComp = nullptr;
	if (StaticMesh)
	{
		UStaticMeshComponent* SMComp = NewObject<UStaticMeshComponent>(TempActor, TEXT("PreviewMesh"));
		SMComp->SetupAttachment(SkelComp, FName(*SocketName));
		SMComp->SetStaticMesh(StaticMesh);
		SMComp->RegisterComponent();
		AttachedComp = SMComp;
	}
	else if (SkelMeshAsset)
	{
		USkeletalMeshComponent* SKComp = NewObject<USkeletalMeshComponent>(TempActor, TEXT("PreviewMesh"));
		SKComp->SetupAttachment(SkelComp, FName(*SocketName));
		SKComp->SetSkeletalMeshAsset(SkelMeshAsset);
		SKComp->RegisterComponent();
		AttachedComp = SKComp;
	}

	// Calculate bounds for camera positioning
	FBoxSphereBounds Bounds = SkelComp->CalcBounds(SkelComp->GetComponentTransform());

	// Focus on the socket area
	FVector SocketLocation = SpawnOrigin;
	if (SkelComp->DoesSocketExist(FName(*SocketName)))
	{
		SocketLocation = SkelComp->GetSocketLocation(FName(*SocketName));
	}

	// Create render target
	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RenderTarget->InitCustomFormat(Resolution, Resolution, PF_B8G8R8A8, false);
	RenderTarget->ClearColor = FLinearColor(0.15f, 0.15f, 0.15f, 1.0f);
	RenderTarget->UpdateResourceImmediate(true);

	// Create scene capture
	USceneCaptureComponent2D* CaptureComp = NewObject<USceneCaptureComponent2D>(TempActor, TEXT("Capture"));
	CaptureComp->SetupAttachment(RootComp);
	CaptureComp->TextureTarget = RenderTarget;
	CaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	CaptureComp->bCaptureEveryFrame = false;
	CaptureComp->bCaptureOnMovement = false;
	CaptureComp->RegisterComponent();

	// Position camera to look at the socket from behind-right at 45 degrees
	const float CamDist = Bounds.SphereRadius * 1.5f;
	FVector CamOffset = FVector(-CamDist * 0.7f, CamDist * 0.5f, CamDist * 0.3f);
	CaptureComp->SetWorldLocationAndRotation(
		SocketLocation + CamOffset,
		(SocketLocation - (SocketLocation + CamOffset)).Rotation()
	);

	// Capture
	CaptureComp->CaptureScene();
	FlushRenderingCommands();

	// Read pixels
	TArray<FColor> Pixels;
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (RTResource)
	{
		RTResource->ReadPixels(Pixels);
	}

	// Save to file
	FString OutputDir = FPaths::ProjectSavedDir() / TEXT("MCP_Previews");
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	FString FileName = FString::Printf(TEXT("socket_preview_%s_%s.png"), *SocketName, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	FString FullPath = OutputDir / FileName;

	if (Pixels.Num() > 0)
	{
		TArray64<uint8> PNGData;
		FImageUtils::PNGCompressImageArray(Resolution, Resolution, Pixels, PNGData);
		FFileHelper::SaveArrayToFile(PNGData, *FullPath);
	}

	// Cleanup temp actors
	TempActor->Destroy();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Pixels.Num() > 0);
	Result->SetStringField(TEXT("saved_path"), FullPath);
	Result->SetStringField(TEXT("socket_name"), SocketName);
	Result->SetStringField(TEXT("mesh_path"), MeshPath);
	Result->SetNumberField(TEXT("resolution"), Resolution);

	return Result;
}

// ============================================================================
// create_anim_montage
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleCreateAnimMontage(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: name"));
	if (!Params->HasField(TEXT("skeleton_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: skeleton_path"));

	const FString AssetName = Params->GetStringField(TEXT("name"));
	const FString SkeletonPath = Params->GetStringField(TEXT("skeleton_path"));

	FString PackagePath = Params->HasField(TEXT("package_path"))
		? Params->GetStringField(TEXT("package_path"))
		: TEXT("/Game/Animations/");
	if (!PackagePath.EndsWith(TEXT("/")))
		PackagePath += TEXT("/");

	const FString SlotName = Params->HasField(TEXT("slot_name"))
		? Params->GetStringField(TEXT("slot_name"))
		: TEXT("DefaultSlot");

	const float BlendInTime = Params->HasField(TEXT("blend_in_time"))
		? static_cast<float>(Params->GetNumberField(TEXT("blend_in_time")))
		: 0.25f;
	const float BlendOutTime = Params->HasField(TEXT("blend_out_time"))
		? static_cast<float>(Params->GetNumberField(TEXT("blend_out_time")))
		: 0.25f;

	// Check for existing asset
	const FString FullPath = PackagePath + AssetName;
	if (FEpicUnrealMCPCommonUtils::AssetExists(FullPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset already exists: %s"), *FullPath));
	}

	// Load skeleton (supports USkeleton or USkeletalMesh)
	UObject* SkelObj = FEpicUnrealMCPCommonUtils::ResolveAsset(SkeletonPath);
	if (!SkelObj)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load skeleton: %s"), *SkeletonPath));

	USkeleton* Skeleton = Cast<USkeleton>(SkelObj);
	if (!Skeleton)
	{
		USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(SkelObj);
		if (SkelMesh)
			Skeleton = SkelMesh->GetSkeleton();
	}
	if (!Skeleton)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Could not resolve skeleton from: %s"), *SkeletonPath));

	// Load optional source animation
	UAnimSequence* SourceAnim = nullptr;
	if (Params->HasField(TEXT("source_animation")))
	{
		const FString SourcePath = Params->GetStringField(TEXT("source_animation"));
		UObject* SourceObj = FEpicUnrealMCPCommonUtils::ResolveAsset(SourcePath);
		if (!SourceObj)
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to load source animation: %s"), *SourcePath));
		SourceAnim = Cast<UAnimSequence>(SourceObj);
		if (!SourceAnim)
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Asset is not a UAnimSequence: %s"), *SourcePath));
	}

	// Create package and montage
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to create package: %s"), *FullPath));

	UAnimMontage* Montage = NewObject<UAnimMontage>(Package, UAnimMontage::StaticClass(),
		*AssetName, RF_Public | RF_Standalone);
	if (!Montage)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create UAnimMontage"));

	Montage->SetSkeleton(Skeleton);

	// Set slot name on the default track
	if (Montage->SlotAnimTracks.Num() > 0)
	{
		Montage->SlotAnimTracks[0].SlotName = FName(*SlotName);
	}

	// If source animation provided, create a segment
	if (SourceAnim)
	{
		if (Montage->SlotAnimTracks.Num() == 0)
		{
			FSlotAnimationTrack NewTrack;
			NewTrack.SlotName = FName(*SlotName);
			Montage->SlotAnimTracks.Add(NewTrack);
		}
		FAnimSegment NewSegment;
		NewSegment.SetAnimReference(SourceAnim, true);
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Add(NewSegment);
		Montage->SetCompositeLength(SourceAnim->GetPlayLength());
	}

	// Blend times
	Montage->BlendIn.SetBlendTime(BlendInTime);
	Montage->BlendOut.SetBlendTime(BlendOutTime);

	// ----- Atomic initial configuration -----

	// Blend envelope (mode, trigger time, auto blend out, profiles)
	if (Params->HasField(TEXT("blend_in_mode")))
	{
		EAlphaBlendOption Opt;
		if (TryParseAlphaBlendOption(Params->GetStringField(TEXT("blend_in_mode")), Opt))
		{
			Montage->BlendIn.SetBlendOption(Opt);
		}
	}
	if (Params->HasField(TEXT("blend_out_mode")))
	{
		EAlphaBlendOption Opt;
		if (TryParseAlphaBlendOption(Params->GetStringField(TEXT("blend_out_mode")), Opt))
		{
			Montage->BlendOut.SetBlendOption(Opt);
		}
	}
	if (Params->HasField(TEXT("blend_out_trigger_time")))
	{
		Montage->BlendOutTriggerTime = static_cast<float>(Params->GetNumberField(TEXT("blend_out_trigger_time")));
	}
	if (Params->HasField(TEXT("enable_auto_blend_out")))
	{
		Montage->bEnableAutoBlendOut = Params->GetBoolField(TEXT("enable_auto_blend_out"));
	}
	if (Params->HasField(TEXT("blend_profile_in_path")))
	{
		const FString P = Params->GetStringField(TEXT("blend_profile_in_path"));
		if (!P.IsEmpty() && !P.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Montage->BlendProfileIn = LoadObject<UBlendProfile>(nullptr, *P);
		}
	}
	if (Params->HasField(TEXT("blend_profile_out_path")))
	{
		const FString P = Params->GetStringField(TEXT("blend_profile_out_path"));
		if (!P.IsEmpty() && !P.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Montage->BlendProfileOut = LoadObject<UBlendProfile>(nullptr, *P);
		}
	}

	// Root motion
	if (Params->HasField(TEXT("root_motion_root_lock")))
	{
		ERootMotionRootLock::Type Lock;
		if (TryParseRootMotionRootLock(Params->GetStringField(TEXT("root_motion_root_lock")), Lock))
		{
			Montage->RootMotionRootLock = Lock;
		}
	}
	if (Params->HasField(TEXT("enable_root_motion_translation")))
	{
		Montage->bEnableRootMotionTranslation = Params->GetBoolField(TEXT("enable_root_motion_translation"));
	}
	if (Params->HasField(TEXT("enable_root_motion_rotation")))
	{
		Montage->bEnableRootMotionRotation = Params->GetBoolField(TEXT("enable_root_motion_rotation"));
	}

	// Sync group
	if (Params->HasField(TEXT("sync_group")))
	{
		const FString SyncGroup = Params->GetStringField(TEXT("sync_group"));
		Montage->SyncGroup = (SyncGroup.IsEmpty() || SyncGroup.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			? FName(NAME_None)
			: FName(*SyncGroup);
	}
	if (Params->HasField(TEXT("sync_slot_index")))
	{
		Montage->SyncSlotIndex = static_cast<int32>(Params->GetNumberField(TEXT("sync_slot_index")));
	}

	// Playback defaults
	if (Params->HasField(TEXT("rate_scale")))
	{
		Montage->RateScale = static_cast<float>(Params->GetNumberField(TEXT("rate_scale")));
	}
	if (Params->HasField(TEXT("loop")))
	{
		Montage->bLoop = Params->GetBoolField(TEXT("loop"));
	}

	// Blend modes (Standard vs Inertialization)
	if (Params->HasField(TEXT("blend_mode_in")))
	{
		EMontageBlendMode M;
		if (TryParseMontageBlendMode(Params->GetStringField(TEXT("blend_mode_in")), M))
		{
			Montage->BlendModeIn = M;
		}
	}
	if (Params->HasField(TEXT("blend_mode_out")))
	{
		EMontageBlendMode M;
		if (TryParseMontageBlendMode(Params->GetStringField(TEXT("blend_mode_out")), M))
		{
			Montage->BlendModeOut = M;
		}
	}

	// Time stretch curve
	if (Params->HasField(TEXT("time_stretch_curve_name")))
	{
		const FString Val = Params->GetStringField(TEXT("time_stretch_curve_name"));
		Montage->TimeStretchCurveName = (Val.IsEmpty() || Val.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			? FName(NAME_None)
			: FName(*Val);
	}
	if (Params->HasField(TEXT("time_stretch_curve")))
	{
		const TSharedPtr<FJsonObject>* CurveObjPtr = nullptr;
		if (Params->TryGetObjectField(TEXT("time_stretch_curve"), CurveObjPtr) && CurveObjPtr && CurveObjPtr->IsValid())
		{
			FJsonObjectConverter::JsonObjectToUStruct(
				CurveObjPtr->ToSharedRef(),
				FTimeStretchCurve::StaticStruct(),
				&Montage->TimeStretchCurve,
				CPF_Edit, 0);
		}
	}

#if WITH_EDITORONLY_DATA
	// Preview base pose (editor-only)
	if (Params->HasField(TEXT("preview_base_pose_path")))
	{
		const FString Path = Params->GetStringField(TEXT("preview_base_pose_path"));
		if (!Path.IsEmpty() && !Path.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Montage->PreviewBasePose = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(Path));
		}
	}
#endif

	// Initial notify tracks: [{ name, color? }]
	if (Params->HasField(TEXT("notify_tracks")))
	{
		const TArray<TSharedPtr<FJsonValue>>* TracksJson = nullptr;
		if (Params->TryGetArrayField(TEXT("notify_tracks"), TracksJson))
		{
			for (const TSharedPtr<FJsonValue>& Val : *TracksJson)
			{
				const TSharedPtr<FJsonObject>* TrackObjPtr = nullptr;
				if (!Val->TryGetObject(TrackObjPtr) || !TrackObjPtr || !TrackObjPtr->IsValid()) continue;
				const TSharedPtr<FJsonObject>& TrackObj = *TrackObjPtr;
				if (!TrackObj->HasField(TEXT("name"))) continue;
				FAnimNotifyTrack NewTrack;
				NewTrack.TrackName = FName(*TrackObj->GetStringField(TEXT("name")));
				NewTrack.TrackColor = FLinearColor::White;
				if (TrackObj->HasField(TEXT("color")))
				{
					const TSharedPtr<FJsonObject>* ColObjPtr = nullptr;
					if (TrackObj->TryGetObjectField(TEXT("color"), ColObjPtr) && ColObjPtr && ColObjPtr->IsValid())
					{
						TryParseLinearColor(*ColObjPtr, NewTrack.TrackColor);
					}
				}
				Montage->AnimNotifyTracks.Add(NewTrack);
			}
		}
	}

	// Initial segments on the default slot: [{ animation_path, start_pos, anim_start_time, anim_end_time, play_rate, looping_count, slot_index?, slot_name? }]
	if (Params->HasField(TEXT("segments")))
	{
		const TArray<TSharedPtr<FJsonValue>>* SegmentsJson = nullptr;
		if (Params->TryGetArrayField(TEXT("segments"), SegmentsJson))
		{
			for (const TSharedPtr<FJsonValue>& Val : *SegmentsJson)
			{
				const TSharedPtr<FJsonObject>* SegObjPtr = nullptr;
				if (!Val->TryGetObject(SegObjPtr) || !SegObjPtr || !SegObjPtr->IsValid()) continue;
				const TSharedPtr<FJsonObject>& SegObj = *SegObjPtr;
				if (!SegObj->HasField(TEXT("animation_path"))) continue;

				const FString AnimPath = SegObj->GetStringField(TEXT("animation_path"));
				UAnimSequence* SegAnim = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimPath));
				if (!SegAnim) continue;

				FString DummyErr;
				int32 SegSlotIdx = ResolveOrCreateSlotIndex(Montage, SegObj, true, DummyErr);
				if (SegSlotIdx == INDEX_NONE) continue;

				FAnimSegment NewSeg;
				NewSeg.SetAnimReference(SegAnim, true);
				if (SegObj->HasField(TEXT("start_pos")))       NewSeg.StartPos       = static_cast<float>(SegObj->GetNumberField(TEXT("start_pos")));
				if (SegObj->HasField(TEXT("anim_start_time"))) NewSeg.AnimStartTime  = static_cast<float>(SegObj->GetNumberField(TEXT("anim_start_time")));
				if (SegObj->HasField(TEXT("anim_end_time")))   NewSeg.AnimEndTime    = static_cast<float>(SegObj->GetNumberField(TEXT("anim_end_time")));
				if (SegObj->HasField(TEXT("play_rate")))       NewSeg.AnimPlayRate   = static_cast<float>(SegObj->GetNumberField(TEXT("play_rate")));
				if (SegObj->HasField(TEXT("looping_count")))   NewSeg.LoopingCount   = FMath::Max(1, static_cast<int32>(SegObj->GetNumberField(TEXT("looping_count"))));

				Montage->SlotAnimTracks[SegSlotIdx].AnimTrack.AnimSegments.Add(NewSeg);
			}
			RecomputeMontageCompositeLength(Montage);
		}
	}

	// Initial sections: [{ name, start_time, next_section_name?, link_method? }]
	if (Params->HasField(TEXT("sections")))
	{
		const TArray<TSharedPtr<FJsonValue>>* SectionsJson = nullptr;
		if (Params->TryGetArrayField(TEXT("sections"), SectionsJson))
		{
			for (const TSharedPtr<FJsonValue>& Val : *SectionsJson)
			{
				const TSharedPtr<FJsonObject>* SectionObjPtr = nullptr;
				if (!Val->TryGetObject(SectionObjPtr) || !SectionObjPtr || !SectionObjPtr->IsValid()) continue;
				const TSharedPtr<FJsonObject>& SecObj = *SectionObjPtr;
				if (!SecObj->HasField(TEXT("name"))) continue;
				const FName SecName(*SecObj->GetStringField(TEXT("name")));
				const float SecStart = SecObj->HasField(TEXT("start_time"))
					? static_cast<float>(SecObj->GetNumberField(TEXT("start_time")))
					: 0.0f;
				const int32 SIdx = Montage->AddAnimCompositeSection(SecName, SecStart);
				if (SIdx == INDEX_NONE) continue;
				if (SecObj->HasField(TEXT("next_section_name")))
				{
					Montage->CompositeSections[SIdx].NextSectionName = FName(*SecObj->GetStringField(TEXT("next_section_name")));
				}
				if (SecObj->HasField(TEXT("link_method")))
				{
					const FString Method = SecObj->GetStringField(TEXT("link_method"));
					EAnimLinkMethod::Type LM = EAnimLinkMethod::Absolute;
					if (Method.Equals(TEXT("Relative"), ESearchCase::IgnoreCase))          LM = EAnimLinkMethod::Relative;
					else if (Method.Equals(TEXT("Proportional"), ESearchCase::IgnoreCase)) LM = EAnimLinkMethod::Proportional;
					Montage->CompositeSections[SIdx].ChangeLinkMethod(LM);
				}
			}
		}
	}

	// Ensure starting section at T=0 if none added
	if (Montage->CompositeSections.Num() == 0)
	{
		Montage->AddAnimCompositeSection(FName(TEXT("Default")), 0.0f);
	}

	// Initial notifies: [{ notify_name, trigger_time, duration?, notify_class_path? | notify_class?, track_index? | track_name?, tick_type?, end_trigger_time_offset?, trigger_weight_threshold?, notify_trigger_chance?, trigger_on_dedicated_server?, trigger_on_follower?, properties? }]
	if (Params->HasField(TEXT("notifies")))
	{
		const TArray<TSharedPtr<FJsonValue>>* NotifiesJson = nullptr;
		if (Params->TryGetArrayField(TEXT("notifies"), NotifiesJson))
		{
			for (const TSharedPtr<FJsonValue>& Val : *NotifiesJson)
			{
				const TSharedPtr<FJsonObject>* NObjPtr = nullptr;
				if (!Val->TryGetObject(NObjPtr) || !NObjPtr || !NObjPtr->IsValid()) continue;
				const TSharedPtr<FJsonObject>& NObj = *NObjPtr;
				if (!NObj->HasField(TEXT("notify_name")) || !NObj->HasField(TEXT("trigger_time"))) continue;

				const FString NotifyName = NObj->GetStringField(TEXT("notify_name"));
				const float TriggerTime = static_cast<float>(NObj->GetNumberField(TEXT("trigger_time")));
				const bool bHasDuration = NObj->HasField(TEXT("duration"));
				const float Duration = bHasDuration ? static_cast<float>(NObj->GetNumberField(TEXT("duration"))) : 0.0f;

				FString TrackErr;
				int32 TrackIndex = ResolveNotifyTrackIndex(Montage, NObj, TrackErr);
				if (TrackIndex == INDEX_NONE) TrackIndex = 0;

				FString ClassErr;
				UClass* NotifyClass = ResolveNotifyClass(NObj, ClassErr);

				FAnimNotifyEvent NewNotify;
				NewNotify.NotifyName = FName(*NotifyName);
				NewNotify.Link(Montage, TriggerTime);
				NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(TriggerTime));
				NewNotify.TrackIndex = TrackIndex;
				NewNotify.TriggerWeightThreshold = 0.0f;
				NewNotify.MontageTickType = EMontageNotifyTickType::Queued;
				NewNotify.NotifyTriggerChance = 1.0f;

				UObject* Instance = nullptr;
				if (NotifyClass)
				{
					if (NotifyClass->IsChildOf(UAnimNotifyState::StaticClass()))
					{
						UAnimNotifyState* StateInst = NewObject<UAnimNotifyState>(Montage, NotifyClass, NAME_None, RF_Transactional);
						NewNotify.NotifyStateClass = StateInst;
						NewNotify.SetDuration(Duration);
						NewNotify.EndLink.Link(Montage, NewNotify.EndLink.GetTime(), NewNotify.GetSlotIndex());
						NewNotify.EndTriggerTimeOffset = GetTriggerTimeOffsetForType(
							Montage->CalculateOffsetForNotify(NewNotify.EndLink.GetTime()));
						Instance = StateInst;
					}
					else if (NotifyClass->IsChildOf(UAnimNotify::StaticClass()))
					{
						UAnimNotify* NInst = NewObject<UAnimNotify>(Montage, NotifyClass, NAME_None, RF_Transactional);
						NewNotify.Notify = NInst;
						Instance = NInst;
					}
				}
				else if (bHasDuration)
				{
					NewNotify.Duration = Duration;
				}

				ApplyNotifyEventFields(NewNotify, NObj);
				ApplyNotifyProperties(Instance, NObj);

				Montage->Notifies.Add(NewNotify);
			}
			Montage->SortNotifies();
		}
	}

	FAssetRegistryModule::AssetCreated(Montage);
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Package->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), FullPath);
	Result->SetStringField(TEXT("asset_name"), AssetName);
	Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	Result->SetNumberField(TEXT("play_length"), Montage->GetPlayLength());
	Result->SetStringField(TEXT("slot_name"), SlotName);
	Result->SetNumberField(TEXT("total_sections"), Montage->CompositeSections.Num());
	Result->SetNumberField(TEXT("total_notifies"), Montage->Notifies.Num());
	Result->SetNumberField(TEXT("total_notify_tracks"), Montage->AnimNotifyTracks.Num());
	Result->SetNumberField(TEXT("total_slots"), Montage->SlotAnimTracks.Num());
	return Result;
}

// ============================================================================
// add_montage_section
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAddMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("section_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: section_name"));
	if (!Params->HasField(TEXT("start_time")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: start_time"));

	const FString SectionName = Params->GetStringField(TEXT("section_name"));
	const float StartTime = static_cast<float>(Params->GetNumberField(TEXT("start_time")));

	Montage->Modify();
	int32 Index = Montage->AddAnimCompositeSection(FName(*SectionName), StartTime);

	if (Index == INDEX_NONE)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to add section '%s' (duplicate name or invalid time?)"), *SectionName));

	if (Params->HasField(TEXT("next_section_name")))
	{
		const FString NextSection = Params->GetStringField(TEXT("next_section_name"));
		Montage->CompositeSections[Index].NextSectionName = FName(*NextSection);
	}

	if (Params->HasField(TEXT("link_method")))
	{
		const FString Method = Params->GetStringField(TEXT("link_method"));
		EAnimLinkMethod::Type LinkMethod = EAnimLinkMethod::Absolute;
		if (Method.Equals(TEXT("Relative"), ESearchCase::IgnoreCase))           LinkMethod = EAnimLinkMethod::Relative;
		else if (Method.Equals(TEXT("Proportional"), ESearchCase::IgnoreCase))  LinkMethod = EAnimLinkMethod::Proportional;
		Montage->CompositeSections[Index].ChangeLinkMethod(LinkMethod);
	}

	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("section_name"), SectionName);
	Result->SetNumberField(TEXT("index"), Index);
	Result->SetNumberField(TEXT("start_time"), StartTime);
	Result->SetNumberField(TEXT("total_sections"), Montage->CompositeSections.Num());
	return Result;
}

// ============================================================================
// remove_montage_section
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleRemoveMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("section_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: section_name"));

	const FString SectionName = Params->GetStringField(TEXT("section_name"));
	int32 SectionIndex = Montage->GetSectionIndex(FName(*SectionName));
	if (SectionIndex == INDEX_NONE)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Section '%s' not found"), *SectionName));

	Montage->Modify();
	Montage->CompositeSections.RemoveAt(SectionIndex);
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_section"), SectionName);
	Result->SetNumberField(TEXT("remaining_sections"), Montage->CompositeSections.Num());
	return Result;
}

// ============================================================================
// set_montage_section_link
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetMontageSectionLink(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("section_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: section_name"));
	if (!Params->HasField(TEXT("next_section_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: next_section_name"));

	const FString SectionName = Params->GetStringField(TEXT("section_name"));
	const FString NextSectionName = Params->GetStringField(TEXT("next_section_name"));

	int32 SectionIdx = Montage->GetSectionIndex(FName(*SectionName));
	if (SectionIdx == INDEX_NONE)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Section '%s' not found"), *SectionName));

	Montage->Modify();
	Montage->CompositeSections[SectionIdx].NextSectionName = FName(*NextSectionName);
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("section_name"), SectionName);
	Result->SetStringField(TEXT("next_section_name"), NextSectionName);
	return Result;
}

// ============================================================================
// Helper: Resolve a UClass from either "notify_class_path" (preferred) or
// "notify_class" (short name) on the params object. Returns nullptr and fills
// OutError on failure. Returns nullptr and empty OutError if neither field is
// provided (instant notify with no typed instance).
// ============================================================================

static UClass* ResolveNotifyClass(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	OutError.Reset();

	if (Params->HasField(TEXT("notify_class_path")))
	{
		const FString Path = Params->GetStringField(TEXT("notify_class_path"));
		if (UClass* Cls = FindObject<UClass>(nullptr, *Path))
		{
			return Cls;
		}
		// LoadObject walks UClass redirectors and on-disk classes too.
		if (UClass* Cls = LoadObject<UClass>(nullptr, *Path))
		{
			return Cls;
		}
		OutError = FString::Printf(TEXT("notify_class_path '%s' not resolved"), *Path);
		return nullptr;
	}

	if (Params->HasField(TEXT("notify_class")))
	{
		const FString ShortName = Params->GetStringField(TEXT("notify_class"));
		UClass* Cls = FindFirstObject<UClass>(*ShortName, EFindFirstObjectOptions::EnsureIfAmbiguous);
		if (!Cls && !ShortName.StartsWith(TEXT("U")))
		{
			Cls = FindFirstObject<UClass>(*(TEXT("U") + ShortName), EFindFirstObjectOptions::EnsureIfAmbiguous);
		}
		if (!Cls)
		{
			OutError = FString::Printf(TEXT("notify_class '%s' not found"), *ShortName);
		}
		return Cls;
	}

	return nullptr;
}

// ============================================================================
// Helper: Resolve a notify track index from either "track_name" (preferred)
// or "track_index". Returns INDEX_NONE on error (with OutError populated) or
// a default of 0 if neither is supplied.
// ============================================================================

static int32 ResolveNotifyTrackIndex(const UAnimSequenceBase* AnimBase, const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	OutError.Reset();

	if (Params->HasField(TEXT("track_name")))
	{
		const FString WantName = Params->GetStringField(TEXT("track_name"));
		for (int32 i = 0; i < AnimBase->AnimNotifyTracks.Num(); ++i)
		{
			if (AnimBase->AnimNotifyTracks[i].TrackName.ToString().Equals(WantName, ESearchCase::CaseSensitive))
			{
				return i;
			}
		}
		OutError = FString::Printf(TEXT("Notify track '%s' not found"), *WantName);
		return INDEX_NONE;
	}

	if (Params->HasField(TEXT("track_index")))
	{
		return static_cast<int32>(Params->GetNumberField(TEXT("track_index")));
	}

	return 0;
}

// ============================================================================
// Helper: Apply "properties" JSON object onto a UObject instance. No-op if
// properties field is absent or not an object.
// ============================================================================

static void ApplyNotifyProperties(UObject* Instance, const TSharedPtr<FJsonObject>& Params)
{
	if (!Instance || !Params->HasField(TEXT("properties")))
	{
		return;
	}
	const TSharedPtr<FJsonObject>* PropsObjPtr = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropsObjPtr) || !PropsObjPtr || !PropsObjPtr->IsValid())
	{
		return;
	}
	FJsonObjectConverter::JsonObjectToUStruct(
		PropsObjPtr->ToSharedRef(),
		Instance->GetClass(),
		Instance,
		CPF_Edit,
		0);
}

// ============================================================================
// Helper: Apply the common FAnimNotifyEvent fields (tick_type, offsets,
// chance, thresholds, server/follower flags) from params onto the event.
// ============================================================================

static void ApplyNotifyEventFields(FAnimNotifyEvent& Event, const TSharedPtr<FJsonObject>& Params)
{
	if (Params->HasField(TEXT("tick_type")))
	{
		const FString Tick = Params->GetStringField(TEXT("tick_type"));
		Event.MontageTickType = Tick.Equals(TEXT("BranchingPoint"), ESearchCase::IgnoreCase)
			? EMontageNotifyTickType::BranchingPoint
			: EMontageNotifyTickType::Queued;
	}
	if (Params->HasField(TEXT("end_trigger_time_offset")))
	{
		Event.EndTriggerTimeOffset = static_cast<float>(Params->GetNumberField(TEXT("end_trigger_time_offset")));
	}
	if (Params->HasField(TEXT("trigger_weight_threshold")))
	{
		Event.TriggerWeightThreshold = static_cast<float>(Params->GetNumberField(TEXT("trigger_weight_threshold")));
	}
	if (Params->HasField(TEXT("notify_trigger_chance")))
	{
		Event.NotifyTriggerChance = static_cast<float>(Params->GetNumberField(TEXT("notify_trigger_chance")));
	}
	if (Params->HasField(TEXT("trigger_on_dedicated_server")))
	{
		Event.bTriggerOnDedicatedServer = Params->GetBoolField(TEXT("trigger_on_dedicated_server"));
	}
	if (Params->HasField(TEXT("trigger_on_follower")))
	{
		Event.bTriggerOnFollower = Params->GetBoolField(TEXT("trigger_on_follower"));
	}
}

// ============================================================================
// add_montage_notify
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAddMontageNotify(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("notify_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: notify_name"));
	if (!Params->HasField(TEXT("trigger_time")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: trigger_time"));

	const FString NotifyName = Params->GetStringField(TEXT("notify_name"));
	const float TriggerTime = static_cast<float>(Params->GetNumberField(TEXT("trigger_time")));
	const bool bHasDuration = Params->HasField(TEXT("duration"));
	const float Duration = bHasDuration
		? static_cast<float>(Params->GetNumberField(TEXT("duration")))
		: 0.0f;

	FString ResolveErr;
	const int32 TrackIndex = ResolveNotifyTrackIndex(Montage, Params, ResolveErr);
	if (TrackIndex == INDEX_NONE && !ResolveErr.IsEmpty())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ResolveErr);
	}

	UClass* NotifyClass = ResolveNotifyClass(Params, ResolveErr);
	if (!NotifyClass && !ResolveErr.IsEmpty())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ResolveErr);
	}

	Montage->Modify();

	FAnimNotifyEvent NewNotify;
	NewNotify.NotifyName = FName(*NotifyName);
	NewNotify.Link(Montage, TriggerTime);
	NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(TriggerTime));
	NewNotify.TrackIndex = TrackIndex;
	NewNotify.TriggerWeightThreshold = 0.0f;
	NewNotify.MontageTickType = EMontageNotifyTickType::Queued;
	NewNotify.NotifyTriggerChance = 1.0f;

	UObject* Instance = nullptr;
	if (NotifyClass)
	{
		if (NotifyClass->IsChildOf(UAnimNotifyState::StaticClass()))
		{
			UAnimNotifyState* StateInstance = NewObject<UAnimNotifyState>(Montage, NotifyClass, NAME_None, RF_Transactional);
			NewNotify.NotifyStateClass = StateInstance;
			NewNotify.SetDuration(Duration);
			NewNotify.EndLink.Link(Montage, NewNotify.EndLink.GetTime(), NewNotify.GetSlotIndex());
			NewNotify.EndTriggerTimeOffset = GetTriggerTimeOffsetForType(
				Montage->CalculateOffsetForNotify(NewNotify.EndLink.GetTime()));
			Instance = StateInstance;
		}
		else if (NotifyClass->IsChildOf(UAnimNotify::StaticClass()))
		{
			UAnimNotify* NotifyInstance = NewObject<UAnimNotify>(Montage, NotifyClass, NAME_None, RF_Transactional);
			NewNotify.Notify = NotifyInstance;
			Instance = NotifyInstance;
		}
		else
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Class '%s' is not a UAnimNotify or UAnimNotifyState"),
					*NotifyClass->GetPathName()));
		}
	}
	else if (bHasDuration)
	{
		NewNotify.Duration = Duration;
	}

	ApplyNotifyEventFields(NewNotify, Params);
	ApplyNotifyProperties(Instance, Params);

	Montage->Notifies.Add(NewNotify);
	Montage->SortNotifies();
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	// Locate index after sort
	int32 NewIndex = INDEX_NONE;
	for (int32 i = 0; i < Montage->Notifies.Num(); ++i)
	{
		const FAnimNotifyEvent& Evt = Montage->Notifies[i];
		const bool bInstanceMatch = (Instance && (Evt.Notify == Instance || Evt.NotifyStateClass == Instance));
		const bool bNameTimeMatch =
			Evt.NotifyName == FName(*NotifyName) &&
			FMath::IsNearlyEqual(Evt.GetTriggerTime(), TriggerTime, 1e-4f);
		if (bInstanceMatch || (!Instance && bNameTimeMatch))
		{
			NewIndex = i;
			break;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("index"), NewIndex);
	Result->SetStringField(TEXT("notify_name"), NotifyName);
	Result->SetNumberField(TEXT("trigger_time"), TriggerTime);
	Result->SetNumberField(TEXT("track_index"), TrackIndex);
	Result->SetNumberField(TEXT("total_notifies"), Montage->Notifies.Num());
	if (bHasDuration)
		Result->SetNumberField(TEXT("duration"), Duration);
	if (NotifyClass)
		Result->SetStringField(TEXT("notify_class_path"), NotifyClass->GetPathName());
	return Result;
}

// ============================================================================
// remove_montage_notify
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleRemoveMontageNotify(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	const bool bHasName = Params->HasField(TEXT("notify_name"));
	const bool bHasIndex = Params->HasField(TEXT("notify_index"));

	if (!bHasName && !bHasIndex)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Must provide either notify_name or notify_index"));

	const bool bRemoveAll = Params->HasField(TEXT("remove_all"))
		? Params->GetBoolField(TEXT("remove_all"))
		: false;
	const bool bHasTriggerTime = Params->HasField(TEXT("trigger_time"));
	const float WantTriggerTime = bHasTriggerTime
		? static_cast<float>(Params->GetNumberField(TEXT("trigger_time")))
		: 0.0f;
	const float Tolerance = Params->HasField(TEXT("trigger_time_tolerance"))
		? static_cast<float>(Params->GetNumberField(TEXT("trigger_time_tolerance")))
		: 1e-4f;

	Montage->Modify();

	TArray<int32> RemoveIndices;
	TArray<FString> RemovedNames;

	if (bHasIndex)
	{
		const int32 RemoveIndex = static_cast<int32>(Params->GetNumberField(TEXT("notify_index")));
		if (RemoveIndex < 0 || RemoveIndex >= Montage->Notifies.Num())
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Notify index %d out of range [0, %d)"), RemoveIndex, Montage->Notifies.Num()));
		RemoveIndices.Add(RemoveIndex);
		RemovedNames.Add(Montage->Notifies[RemoveIndex].NotifyName.ToString());
	}
	else
	{
		const FString NotifyName = Params->GetStringField(TEXT("notify_name"));
		for (int32 i = 0; i < Montage->Notifies.Num(); ++i)
		{
			if (Montage->Notifies[i].NotifyName.ToString() != NotifyName)
			{
				continue;
			}
			if (bHasTriggerTime && !FMath::IsNearlyEqual(Montage->Notifies[i].GetTriggerTime(), WantTriggerTime, Tolerance))
			{
				continue;
			}
			RemoveIndices.Add(i);
			RemovedNames.Add(NotifyName);
			if (!bRemoveAll)
			{
				break;
			}
		}
		if (RemoveIndices.Num() == 0)
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Notify '%s' not found"), *NotifyName));
	}

	// Remove in descending order to keep earlier indices stable
	RemoveIndices.Sort([](int32 A, int32 B) { return A > B; });
	for (int32 Idx : RemoveIndices)
	{
		Montage->Notifies.RemoveAt(Idx);
	}

	Montage->SortNotifies();
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TArray<TSharedPtr<FJsonValue>> NamesJson;
	for (const FString& N : RemovedNames)
	{
		NamesJson.Add(MakeShareable(new FJsonValueString(N)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("removed_count"), RemoveIndices.Num());
	Result->SetArrayField(TEXT("removed_notifies"), NamesJson);
	Result->SetStringField(TEXT("removed_notify"), RemovedNames.Num() > 0 ? RemovedNames[0] : FString());
	Result->SetNumberField(TEXT("remaining_notifies"), Montage->Notifies.Num());
	return Result;
}

// ============================================================================
// Helper: Locate a notify by index or {name, time}. Returns INDEX_NONE on
// miss (and populates OutError), else a valid index into Montage->Notifies.
// ============================================================================

static int32 ResolveNotifyIndex(const UAnimSequenceBase* AnimBase, const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	OutError.Reset();

	if (Params->HasField(TEXT("notify_index")))
	{
		const int32 Idx = static_cast<int32>(Params->GetNumberField(TEXT("notify_index")));
		if (!AnimBase->Notifies.IsValidIndex(Idx))
		{
			OutError = FString::Printf(TEXT("notify_index %d out of range [0, %d)"), Idx, AnimBase->Notifies.Num());
			return INDEX_NONE;
		}
		return Idx;
	}

	if (!Params->HasField(TEXT("notify_name")))
	{
		OutError = TEXT("Must provide either notify_index or notify_name");
		return INDEX_NONE;
	}

	const FString WantName = Params->GetStringField(TEXT("notify_name"));
	const bool bHasTime = Params->HasField(TEXT("trigger_time"));
	const float WantTime = bHasTime
		? static_cast<float>(Params->GetNumberField(TEXT("trigger_time")))
		: 0.0f;
	const float Tolerance = Params->HasField(TEXT("trigger_time_tolerance"))
		? static_cast<float>(Params->GetNumberField(TEXT("trigger_time_tolerance")))
		: 1e-4f;

	int32 Found = INDEX_NONE;
	int32 MatchCount = 0;
	for (int32 i = 0; i < AnimBase->Notifies.Num(); ++i)
	{
		if (AnimBase->Notifies[i].NotifyName.ToString() != WantName)
		{
			continue;
		}
		if (bHasTime && !FMath::IsNearlyEqual(AnimBase->Notifies[i].GetTriggerTime(), WantTime, Tolerance))
		{
			continue;
		}
		if (Found == INDEX_NONE)
		{
			Found = i;
		}
		++MatchCount;
	}
	if (Found == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Notify '%s' not found"), *WantName);
		return INDEX_NONE;
	}
	if (MatchCount > 1 && !bHasTime)
	{
		OutError = FString::Printf(
			TEXT("Ambiguous: %d notifies named '%s'. Provide trigger_time to disambiguate."),
			MatchCount, *WantName);
		return INDEX_NONE;
	}
	return Found;
}

// ============================================================================
// update_montage_notify
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleUpdateMontageNotify(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	FString ResolveErr;
	int32 Idx = ResolveNotifyIndex(Montage, Params, ResolveErr);
	if (Idx == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ResolveErr);
	}

	Montage->Modify();

	FAnimNotifyEvent& Event = Montage->Notifies[Idx];

	// Track index / name
	if (Params->HasField(TEXT("track_name")) || Params->HasField(TEXT("track_index")))
	{
		FString TrackErr;
		const int32 NewTrack = ResolveNotifyTrackIndex(Montage, Params, TrackErr);
		if (NewTrack == INDEX_NONE && !TrackErr.IsEmpty())
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TrackErr);
		}
		Event.TrackIndex = NewTrack;
	}

	// Rename
	if (Params->HasField(TEXT("new_notify_name")))
	{
		Event.NotifyName = FName(*Params->GetStringField(TEXT("new_notify_name")));
	}

	// Time / duration — re-link to keep LinkValue consistent
	const bool bChangeTime = Params->HasField(TEXT("new_trigger_time"));
	const bool bChangeDuration = Params->HasField(TEXT("duration"));
	if (bChangeTime)
	{
		const float NewTime = static_cast<float>(Params->GetNumberField(TEXT("new_trigger_time")));
		Event.Link(Montage, NewTime);
		Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(NewTime));
	}
	if (bChangeDuration)
	{
		// SetDuration updates Duration AND re-anchors EndLink.LinkValue to GetTime()+Duration.
		Event.SetDuration(static_cast<float>(Params->GetNumberField(TEXT("duration"))));
	}
	else if (bChangeTime)
	{
		// Time moved but duration didn't — still need to slide EndLink.LinkValue to new start + Duration.
		Event.SetDuration(Event.Duration);
	}
	// For state notifies, walk the slot's AnimSegments so EndLink.SegmentIndex/SegmentBeginTime resolve.
	if (Event.NotifyStateClass && (bChangeTime || bChangeDuration))
	{
		Event.EndLink.Link(Montage, Event.EndLink.GetTime(), Event.GetSlotIndex());
		Event.EndTriggerTimeOffset = GetTriggerTimeOffsetForType(
			Montage->CalculateOffsetForNotify(Event.EndLink.GetTime()));
	}

	ApplyNotifyEventFields(Event, Params);

	// Apply properties onto existing instance (do NOT re-instantiate)
	UObject* Instance = Event.Notify ? static_cast<UObject*>(Event.Notify)
		: (Event.NotifyStateClass ? static_cast<UObject*>(Event.NotifyStateClass) : nullptr);
	ApplyNotifyProperties(Instance, Params);

	// Capture values before SortNotifies, which may reorder and invalidate Event.
	const FName FinalName = Event.NotifyName;
	const float FinalTriggerTime = Event.GetTriggerTime();
	const float FinalDuration = Event.GetDuration();

	Montage->SortNotifies();
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	// Index may have shifted after sort — relocate by instance or by name/time
	int32 FinalIdx = INDEX_NONE;
	for (int32 i = 0; i < Montage->Notifies.Num(); ++i)
	{
		const FAnimNotifyEvent& E = Montage->Notifies[i];
		if (Instance && (E.Notify == Instance || E.NotifyStateClass == Instance))
		{
			FinalIdx = i;
			break;
		}
		if (!Instance && E.NotifyName == FinalName && FMath::IsNearlyEqual(E.GetTriggerTime(), FinalTriggerTime, 1e-4f))
		{
			FinalIdx = i;
			break;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("index"), FinalIdx);
	Result->SetStringField(TEXT("notify_name"), FinalName.ToString());
	Result->SetNumberField(TEXT("trigger_time"), FinalTriggerTime);
	Result->SetNumberField(TEXT("duration"), FinalDuration);
	return Result;
}

// ============================================================================
// add_montage_notify_track
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAddMontageNotifyTrack(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("track_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: track_name"));

	const FString TrackName = Params->GetStringField(TEXT("track_name"));

	for (const FAnimNotifyTrack& Existing : Montage->AnimNotifyTracks)
	{
		if (Existing.TrackName.ToString().Equals(TrackName, ESearchCase::CaseSensitive))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Notify track '%s' already exists"), *TrackName));
		}
	}

	FLinearColor Color = FLinearColor::White;
	if (Params->HasField(TEXT("color")))
	{
		const TSharedPtr<FJsonObject>* ColorObjPtr = nullptr;
		if (Params->TryGetObjectField(TEXT("color"), ColorObjPtr) && ColorObjPtr && ColorObjPtr->IsValid())
		{
			TryParseLinearColor(*ColorObjPtr, Color);
		}
	}

	Montage->Modify();

	FAnimNotifyTrack NewTrack;
	NewTrack.TrackName = FName(*TrackName);
	NewTrack.TrackColor = Color;
	const int32 NewIndex = Montage->AnimNotifyTracks.Add(NewTrack);

	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("index"), NewIndex);
	Result->SetStringField(TEXT("track_name"), TrackName);
	Result->SetNumberField(TEXT("total_tracks"), Montage->AnimNotifyTracks.Num());
	return Result;
}

// ============================================================================
// rename_montage_notify_track
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleRenameMontageNotifyTrack(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("new_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: new_name"));

	FString ResolveErr;
	const int32 TrackIdx = ResolveNotifyTrackIndex(Montage, Params, ResolveErr);
	if (TrackIdx == INDEX_NONE || !Montage->AnimNotifyTracks.IsValidIndex(TrackIdx))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			ResolveErr.IsEmpty() ? TEXT("Invalid track reference") : ResolveErr);
	}

	const FString NewName = Params->GetStringField(TEXT("new_name"));

	for (int32 i = 0; i < Montage->AnimNotifyTracks.Num(); ++i)
	{
		if (i == TrackIdx) continue;
		if (Montage->AnimNotifyTracks[i].TrackName.ToString().Equals(NewName, ESearchCase::CaseSensitive))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Another track named '%s' already exists"), *NewName));
		}
	}

	Montage->Modify();

	const FString OldName = Montage->AnimNotifyTracks[TrackIdx].TrackName.ToString();
	Montage->AnimNotifyTracks[TrackIdx].TrackName = FName(*NewName);

	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("index"), TrackIdx);
	Result->SetStringField(TEXT("old_name"), OldName);
	Result->SetStringField(TEXT("new_name"), NewName);
	return Result;
}

// ============================================================================
// remove_montage_notify_track
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleRemoveMontageNotifyTrack(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	FString ResolveErr;
	const int32 TrackIdx = ResolveNotifyTrackIndex(Montage, Params, ResolveErr);
	if (TrackIdx == INDEX_NONE || !Montage->AnimNotifyTracks.IsValidIndex(TrackIdx))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			ResolveErr.IsEmpty() ? TEXT("Invalid track reference") : ResolveErr);
	}

	const int32 ReassignTo = Params->HasField(TEXT("reassign_to"))
		? static_cast<int32>(Params->GetNumberField(TEXT("reassign_to")))
		: INDEX_NONE;

	// Count / reassign orphans
	int32 OrphanCount = 0;
	for (const FAnimNotifyEvent& N : Montage->Notifies)
	{
		if (N.TrackIndex == TrackIdx)
		{
			++OrphanCount;
		}
	}
	if (OrphanCount > 0 && ReassignTo == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Track %d has %d notify(s). Pass reassign_to to relocate them first."),
				TrackIdx, OrphanCount));
	}
	if (ReassignTo != INDEX_NONE)
	{
		if (!Montage->AnimNotifyTracks.IsValidIndex(ReassignTo) || ReassignTo == TrackIdx)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("reassign_to %d is invalid"), ReassignTo));
		}
	}

	Montage->Modify();

	// Reassign orphans, then shift all higher indices down by one
	for (FAnimNotifyEvent& N : Montage->Notifies)
	{
		if (N.TrackIndex == TrackIdx)
		{
			// ReassignTo is the original index — adjust if it was above the removed one
			N.TrackIndex = (ReassignTo > TrackIdx) ? ReassignTo - 1 : ReassignTo;
		}
		else if (N.TrackIndex > TrackIdx)
		{
			N.TrackIndex -= 1;
		}
	}

	const FString RemovedName = Montage->AnimNotifyTracks[TrackIdx].TrackName.ToString();
	Montage->AnimNotifyTracks.RemoveAt(TrackIdx);

	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_track"), RemovedName);
	Result->SetNumberField(TEXT("reassigned_notifies"), ReassignTo == INDEX_NONE ? 0 : OrphanCount);
	Result->SetNumberField(TEXT("remaining_tracks"), Montage->AnimNotifyTracks.Num());
	return Result;
}

// ============================================================================
// Helper: Resolve a slot index from either "slot_name" or "slot_index" on
// params. Returns INDEX_NONE + populates OutError on error. If
// create_slot_if_missing is true and a name is given that doesn't exist, a
// new slot is appended and its index returned.
// ============================================================================

static int32 ResolveOrCreateSlotIndex(UAnimMontage* Montage, const TSharedPtr<FJsonObject>& Params, bool bAllowCreate, FString& OutError)
{
	OutError.Reset();

	if (Params->HasField(TEXT("slot_name")))
	{
		const FString WantName = Params->GetStringField(TEXT("slot_name"));
		for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
		{
			if (Montage->SlotAnimTracks[i].SlotName.ToString().Equals(WantName, ESearchCase::CaseSensitive))
			{
				return i;
			}
		}
		if (!bAllowCreate)
		{
			OutError = FString::Printf(TEXT("Slot '%s' not found"), *WantName);
			return INDEX_NONE;
		}
		FSlotAnimationTrack NewSlot;
		NewSlot.SlotName = FName(*WantName);
		return Montage->SlotAnimTracks.Add(NewSlot);
	}

	if (Params->HasField(TEXT("slot_index")))
	{
		const int32 Idx = static_cast<int32>(Params->GetNumberField(TEXT("slot_index")));
		if (!Montage->SlotAnimTracks.IsValidIndex(Idx))
		{
			OutError = FString::Printf(TEXT("slot_index %d out of range [0, %d)"), Idx, Montage->SlotAnimTracks.Num());
			return INDEX_NONE;
		}
		return Idx;
	}

	// Default to first slot
	if (Montage->SlotAnimTracks.Num() == 0)
	{
		OutError = TEXT("Montage has no slot tracks");
		return INDEX_NONE;
	}
	return 0;
}

// ============================================================================
// add_montage_segment
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAddMontageSegment(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("animation_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: animation_path"));

	const FString AnimPath = Params->GetStringField(TEXT("animation_path"));
	UObject* AnimObj = FEpicUnrealMCPCommonUtils::ResolveAsset(AnimPath);
	if (!AnimObj)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load animation: %s"), *AnimPath));

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimObj);
	if (!AnimSequence)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UAnimSequence: %s"), *AnimPath));

	const bool bAllowCreate = Params->HasField(TEXT("create_slot_if_missing"))
		? Params->GetBoolField(TEXT("create_slot_if_missing"))
		: true;

	FString ResolveErr;
	const int32 SlotIndex = ResolveOrCreateSlotIndex(Montage, Params, bAllowCreate, ResolveErr);
	if (SlotIndex == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ResolveErr);
	}

	const float StartPos = Params->HasField(TEXT("start_pos"))
		? static_cast<float>(Params->GetNumberField(TEXT("start_pos")))
		: 0.0f;
	const float AnimStartTime = Params->HasField(TEXT("anim_start_time"))
		? static_cast<float>(Params->GetNumberField(TEXT("anim_start_time")))
		: 0.0f;
	const float AnimEndTime = Params->HasField(TEXT("anim_end_time"))
		? static_cast<float>(Params->GetNumberField(TEXT("anim_end_time")))
		: AnimSequence->GetPlayLength();
	const float PlayRate = Params->HasField(TEXT("play_rate"))
		? static_cast<float>(Params->GetNumberField(TEXT("play_rate")))
		: 1.0f;
	const int32 LoopingCount = Params->HasField(TEXT("looping_count"))
		? FMath::Max(1, static_cast<int32>(Params->GetNumberField(TEXT("looping_count"))))
		: 1;

	Montage->Modify();

	FAnimSegment NewSegment;
	NewSegment.SetAnimReference(AnimSequence, true);
	NewSegment.StartPos = StartPos;
	NewSegment.AnimStartTime = AnimStartTime;
	NewSegment.AnimEndTime = AnimEndTime;
	NewSegment.AnimPlayRate = PlayRate;
	NewSegment.LoopingCount = LoopingCount;

	const int32 SegIdx = Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments.Add(NewSegment);

	RecomputeMontageCompositeLength(Montage);
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animation_path"), AnimPath);
	Result->SetNumberField(TEXT("slot_index"), SlotIndex);
	Result->SetStringField(TEXT("slot_name"), Montage->SlotAnimTracks[SlotIndex].SlotName.ToString());
	Result->SetNumberField(TEXT("segment_index"), SegIdx);
	Result->SetNumberField(TEXT("start_pos"), StartPos);
	Result->SetNumberField(TEXT("composite_length"), Montage->GetPlayLength());
	Result->SetNumberField(TEXT("total_segments"), Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments.Num());
	return Result;
}

// ============================================================================
// remove_montage_segment
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleRemoveMontageSegment(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	FString ResolveErr;
	const int32 SlotIndex = ResolveOrCreateSlotIndex(Montage, Params, false, ResolveErr);
	if (SlotIndex == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ResolveErr);
	}

	if (!Params->HasField(TEXT("segment_index")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: segment_index"));

	const int32 SegIdx = static_cast<int32>(Params->GetNumberField(TEXT("segment_index")));
	TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments;
	if (!Segments.IsValidIndex(SegIdx))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("segment_index %d out of range [0, %d)"), SegIdx, Segments.Num()));
	}

	Montage->Modify();
	Segments.RemoveAt(SegIdx);
	RecomputeMontageCompositeLength(Montage);
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("slot_index"), SlotIndex);
	Result->SetNumberField(TEXT("removed_segment_index"), SegIdx);
	Result->SetNumberField(TEXT("composite_length"), Montage->GetPlayLength());
	Result->SetNumberField(TEXT("remaining_segments"), Segments.Num());
	return Result;
}

// ============================================================================
// update_montage_segment
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleUpdateMontageSegment(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	FString ResolveErr;
	const int32 SlotIndex = ResolveOrCreateSlotIndex(Montage, Params, false, ResolveErr);
	if (SlotIndex == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ResolveErr);
	}

	if (!Params->HasField(TEXT("segment_index")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: segment_index"));

	const int32 SegIdx = static_cast<int32>(Params->GetNumberField(TEXT("segment_index")));
	TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments;
	if (!Segments.IsValidIndex(SegIdx))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("segment_index %d out of range [0, %d)"), SegIdx, Segments.Num()));
	}

	Montage->Modify();

	FAnimSegment& Segment = Segments[SegIdx];

	if (Params->HasField(TEXT("animation_path")))
	{
		const FString AnimPath = Params->GetStringField(TEXT("animation_path"));
		UObject* AnimObj = FEpicUnrealMCPCommonUtils::ResolveAsset(AnimPath);
		UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimObj);
		if (!AnimSequence)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to load AnimSequence: %s"), *AnimPath));
		}
		Segment.SetAnimReference(AnimSequence, false);
	}
	if (Params->HasField(TEXT("start_pos")))
	{
		Segment.StartPos = static_cast<float>(Params->GetNumberField(TEXT("start_pos")));
	}
	if (Params->HasField(TEXT("anim_start_time")))
	{
		Segment.AnimStartTime = static_cast<float>(Params->GetNumberField(TEXT("anim_start_time")));
	}
	if (Params->HasField(TEXT("anim_end_time")))
	{
		Segment.AnimEndTime = static_cast<float>(Params->GetNumberField(TEXT("anim_end_time")));
	}
	if (Params->HasField(TEXT("play_rate")))
	{
		Segment.AnimPlayRate = static_cast<float>(Params->GetNumberField(TEXT("play_rate")));
	}
	if (Params->HasField(TEXT("looping_count")))
	{
		Segment.LoopingCount = FMath::Max(1, static_cast<int32>(Params->GetNumberField(TEXT("looping_count"))));
	}

	RecomputeMontageCompositeLength(Montage);
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("slot_index"), SlotIndex);
	Result->SetNumberField(TEXT("segment_index"), SegIdx);
	Result->SetNumberField(TEXT("start_pos"), Segment.StartPos);
	Result->SetNumberField(TEXT("anim_start_time"), Segment.AnimStartTime);
	Result->SetNumberField(TEXT("anim_end_time"), Segment.AnimEndTime);
	Result->SetNumberField(TEXT("anim_play_rate"), Segment.AnimPlayRate);
	Result->SetNumberField(TEXT("looping_count"), Segment.LoopingCount);
	Result->SetNumberField(TEXT("composite_length"), Montage->GetPlayLength());
	return Result;
}

// ============================================================================
// update_montage_section
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleUpdateMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("section_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: section_name"));

	const FString SectionName = Params->GetStringField(TEXT("section_name"));
	const int32 SectionIdx = Montage->GetSectionIndex(FName(*SectionName));
	if (SectionIdx == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Section '%s' not found"), *SectionName));
	}

	Montage->Modify();

	FCompositeSection& Section = Montage->CompositeSections[SectionIdx];

	if (Params->HasField(TEXT("new_name")))
	{
		const FString NewName = Params->GetStringField(TEXT("new_name"));
		// Reject duplicates
		for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
		{
			if (i == SectionIdx) continue;
			if (Montage->CompositeSections[i].SectionName.ToString().Equals(NewName, ESearchCase::CaseSensitive))
			{
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Another section named '%s' already exists"), *NewName));
			}
		}

		const FName OldFName = Section.SectionName;
		const FName NewFName(*NewName);
		Section.SectionName = NewFName;

		// Rewire any section whose NextSectionName pointed at the old name
		for (FCompositeSection& Other : Montage->CompositeSections)
		{
			if (Other.NextSectionName == OldFName)
			{
				Other.NextSectionName = NewFName;
			}
		}
	}

	if (Params->HasField(TEXT("start_time")))
	{
		Section.SetTime(static_cast<float>(Params->GetNumberField(TEXT("start_time"))));
	}

	if (Params->HasField(TEXT("next_section_name")))
	{
		Section.NextSectionName = FName(*Params->GetStringField(TEXT("next_section_name")));
	}

	if (Params->HasField(TEXT("link_method")))
	{
		const FString Method = Params->GetStringField(TEXT("link_method"));
		EAnimLinkMethod::Type LinkMethod = EAnimLinkMethod::Absolute;
		if (Method.Equals(TEXT("Relative"), ESearchCase::IgnoreCase)) LinkMethod = EAnimLinkMethod::Relative;
		else if (Method.Equals(TEXT("Proportional"), ESearchCase::IgnoreCase)) LinkMethod = EAnimLinkMethod::Proportional;
		Section.ChangeLinkMethod(LinkMethod);
	}

	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("index"), SectionIdx);
	Result->SetStringField(TEXT("name"), Section.SectionName.ToString());
	Result->SetNumberField(TEXT("start_time"), Section.GetTime());
	Result->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
	Result->SetNumberField(TEXT("link_method"), static_cast<int32>(Section.GetLinkMethod()));
	return Result;
}

// ============================================================================
// set_montage_blend_times
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetMontageBlendTimes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	const bool bTouchesAnything =
		Params->HasField(TEXT("blend_in_time"))        || Params->HasField(TEXT("blend_out_time")) ||
		Params->HasField(TEXT("blend_in_mode"))        || Params->HasField(TEXT("blend_out_mode")) ||
		Params->HasField(TEXT("blend_mode_in"))        || Params->HasField(TEXT("blend_mode_out")) ||
		Params->HasField(TEXT("blend_out_trigger_time")) ||
		Params->HasField(TEXT("enable_auto_blend_out")) ||
		Params->HasField(TEXT("blend_profile_in_path")) ||
		Params->HasField(TEXT("blend_profile_out_path"));
	if (!bTouchesAnything)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Must provide at least one blend field to change"));

	Montage->Modify();

	if (Params->HasField(TEXT("blend_in_time")))
	{
		Montage->BlendIn.SetBlendTime(static_cast<float>(Params->GetNumberField(TEXT("blend_in_time"))));
	}
	if (Params->HasField(TEXT("blend_out_time")))
	{
		Montage->BlendOut.SetBlendTime(static_cast<float>(Params->GetNumberField(TEXT("blend_out_time"))));
	}
	if (Params->HasField(TEXT("blend_in_mode")))
	{
		EAlphaBlendOption NewOpt;
		const FString ModeStr = Params->GetStringField(TEXT("blend_in_mode"));
		if (!TryParseAlphaBlendOption(ModeStr, NewOpt))
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unknown blend_in_mode '%s'"), *ModeStr));
		Montage->BlendIn.SetBlendOption(NewOpt);
	}
	if (Params->HasField(TEXT("blend_out_mode")))
	{
		EAlphaBlendOption NewOpt;
		const FString ModeStr = Params->GetStringField(TEXT("blend_out_mode"));
		if (!TryParseAlphaBlendOption(ModeStr, NewOpt))
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unknown blend_out_mode '%s'"), *ModeStr));
		Montage->BlendOut.SetBlendOption(NewOpt);
	}
	if (Params->HasField(TEXT("blend_out_trigger_time")))
	{
		Montage->BlendOutTriggerTime = static_cast<float>(Params->GetNumberField(TEXT("blend_out_trigger_time")));
	}
	if (Params->HasField(TEXT("enable_auto_blend_out")))
	{
		Montage->bEnableAutoBlendOut = Params->GetBoolField(TEXT("enable_auto_blend_out"));
	}
	if (Params->HasField(TEXT("blend_profile_in_path")))
	{
		const FString P = Params->GetStringField(TEXT("blend_profile_in_path"));
		if (P.IsEmpty() || P.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Montage->BlendProfileIn = nullptr;
		}
		else
		{
			UBlendProfile* Profile = LoadObject<UBlendProfile>(nullptr, *P);
			if (!Profile)
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Failed to load blend_profile_in_path: %s"), *P));
			Montage->BlendProfileIn = Profile;
		}
	}
	if (Params->HasField(TEXT("blend_profile_out_path")))
	{
		const FString P = Params->GetStringField(TEXT("blend_profile_out_path"));
		if (P.IsEmpty() || P.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Montage->BlendProfileOut = nullptr;
		}
		else
		{
			UBlendProfile* Profile = LoadObject<UBlendProfile>(nullptr, *P);
			if (!Profile)
				return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Failed to load blend_profile_out_path: %s"), *P));
			Montage->BlendProfileOut = Profile;
		}
	}
	if (Params->HasField(TEXT("blend_mode_in")))
	{
		EMontageBlendMode M;
		const FString V = Params->GetStringField(TEXT("blend_mode_in"));
		if (!TryParseMontageBlendMode(V, M))
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unknown blend_mode_in '%s' (expected Standard|Inertialization)"), *V));
		Montage->BlendModeIn = M;
	}
	if (Params->HasField(TEXT("blend_mode_out")))
	{
		EMontageBlendMode M;
		const FString V = Params->GetStringField(TEXT("blend_mode_out"));
		if (!TryParseMontageBlendMode(V, M))
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unknown blend_mode_out '%s' (expected Standard|Inertialization)"), *V));
		Montage->BlendModeOut = M;
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
	Result->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
	Result->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());
	Result->SetStringField(TEXT("blend_in_mode"), AlphaBlendOptionToString(Montage->BlendIn.GetBlendOption()));
	Result->SetStringField(TEXT("blend_out_mode"), AlphaBlendOptionToString(Montage->BlendOut.GetBlendOption()));
	Result->SetStringField(TEXT("blend_mode_in"), MontageBlendModeToString(Montage->BlendModeIn));
	Result->SetStringField(TEXT("blend_mode_out"), MontageBlendModeToString(Montage->BlendModeOut));
	Result->SetNumberField(TEXT("blend_out_trigger_time"), Montage->BlendOutTriggerTime);
	Result->SetBoolField(TEXT("enable_auto_blend_out"), Montage->bEnableAutoBlendOut);
	Result->SetStringField(TEXT("blend_profile_in"),
		Montage->BlendProfileIn ? Montage->BlendProfileIn->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("blend_profile_out"),
		Montage->BlendProfileOut ? Montage->BlendProfileOut->GetPathName() : TEXT("None"));
	return Result;
}

// ============================================================================
// set_montage_root_motion
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetMontageRootMotion(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	const bool bAny =
		Params->HasField(TEXT("root_motion_root_lock")) ||
		Params->HasField(TEXT("enable_root_motion_translation")) ||
		Params->HasField(TEXT("enable_root_motion_rotation"));
	if (!bAny)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Must provide at least one root-motion field"));

	Montage->Modify();

	if (Params->HasField(TEXT("root_motion_root_lock")))
	{
		ERootMotionRootLock::Type Lock;
		const FString Val = Params->GetStringField(TEXT("root_motion_root_lock"));
		if (!TryParseRootMotionRootLock(Val, Lock))
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unknown root_motion_root_lock '%s' (expected RefPose|AnimFirstFrame|Zero)"), *Val));
		Montage->RootMotionRootLock = Lock;
	}
	if (Params->HasField(TEXT("enable_root_motion_translation")))
	{
		Montage->bEnableRootMotionTranslation = Params->GetBoolField(TEXT("enable_root_motion_translation"));
	}
	if (Params->HasField(TEXT("enable_root_motion_rotation")))
	{
		Montage->bEnableRootMotionRotation = Params->GetBoolField(TEXT("enable_root_motion_rotation"));
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("root_motion_root_lock"), RootMotionRootLockToString(Montage->RootMotionRootLock));
	Result->SetBoolField(TEXT("enable_root_motion_translation"), Montage->bEnableRootMotionTranslation);
	Result->SetBoolField(TEXT("enable_root_motion_rotation"), Montage->bEnableRootMotionRotation);
	return Result;
}

// ============================================================================
// add_montage_slot
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAddMontageSlot(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("slot_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: slot_name"));

	const FString SlotName = Params->GetStringField(TEXT("slot_name"));

	for (const FSlotAnimationTrack& Existing : Montage->SlotAnimTracks)
	{
		if (Existing.SlotName.ToString().Equals(SlotName, ESearchCase::CaseSensitive))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Slot '%s' already exists"), *SlotName));
		}
	}

	Montage->Modify();

	FSlotAnimationTrack NewSlot;
	NewSlot.SlotName = FName(*SlotName);
	const int32 NewIndex = Montage->SlotAnimTracks.Add(NewSlot);

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("slot_index"), NewIndex);
	Result->SetStringField(TEXT("slot_name"), SlotName);
	Result->SetNumberField(TEXT("total_slots"), Montage->SlotAnimTracks.Num());
	return Result;
}

// ============================================================================
// rename_montage_slot
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleRenameMontageSlot(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("new_slot_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: new_slot_name"));

	FString ResolveErr;
	const int32 SlotIndex = ResolveOrCreateSlotIndex(Montage, Params, false, ResolveErr);
	if (SlotIndex == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ResolveErr);
	}

	const FString NewName = Params->GetStringField(TEXT("new_slot_name"));
	for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
	{
		if (i == SlotIndex) continue;
		if (Montage->SlotAnimTracks[i].SlotName.ToString().Equals(NewName, ESearchCase::CaseSensitive))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Another slot named '%s' already exists"), *NewName));
		}
	}

	Montage->Modify();

	const FString OldName = Montage->SlotAnimTracks[SlotIndex].SlotName.ToString();
	Montage->SlotAnimTracks[SlotIndex].SlotName = FName(*NewName);

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("slot_index"), SlotIndex);
	Result->SetStringField(TEXT("old_name"), OldName);
	Result->SetStringField(TEXT("new_name"), NewName);
	return Result;
}

// ============================================================================
// remove_montage_slot
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleRemoveMontageSlot(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	FString ResolveErr;
	const int32 SlotIndex = ResolveOrCreateSlotIndex(Montage, Params, false, ResolveErr);
	if (SlotIndex == INDEX_NONE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ResolveErr);
	}

	const int32 SegCount = Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments.Num();
	const bool bForce = Params->HasField(TEXT("force"))
		? Params->GetBoolField(TEXT("force"))
		: false;

	if (SegCount > 0 && !bForce)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Slot %d has %d segment(s). Pass force=true to delete the slot and its segments."),
				SlotIndex, SegCount));
	}

	if (Montage->SlotAnimTracks.Num() <= 1)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Cannot remove the last remaining slot track on a montage"));
	}

	Montage->Modify();

	const FString RemovedName = Montage->SlotAnimTracks[SlotIndex].SlotName.ToString();
	Montage->SlotAnimTracks.RemoveAt(SlotIndex);

	RecomputeMontageCompositeLength(Montage);
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_slot"), RemovedName);
	Result->SetNumberField(TEXT("removed_segments"), SegCount);
	Result->SetNumberField(TEXT("remaining_slots"), Montage->SlotAnimTracks.Num());
	Result->SetNumberField(TEXT("composite_length"), Montage->GetPlayLength());
	return Result;
}

// ============================================================================
// set_montage_sync_group
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetMontageSyncGroup(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	const bool bAny =
		Params->HasField(TEXT("sync_group")) ||
		Params->HasField(TEXT("sync_slot_index"));
	if (!bAny)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Must provide sync_group or sync_slot_index"));

	Montage->Modify();

	if (Params->HasField(TEXT("sync_group")))
	{
		const FString SyncGroup = Params->GetStringField(TEXT("sync_group"));
		Montage->SyncGroup = (SyncGroup.IsEmpty() || SyncGroup.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			? FName(NAME_None)
			: FName(*SyncGroup);
	}
	if (Params->HasField(TEXT("sync_slot_index")))
	{
		const int32 Idx = static_cast<int32>(Params->GetNumberField(TEXT("sync_slot_index")));
		if (Idx < 0 || !Montage->SlotAnimTracks.IsValidIndex(Idx))
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("sync_slot_index %d out of range [0, %d)"), Idx, Montage->SlotAnimTracks.Num()));
		Montage->SyncSlotIndex = Idx;
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sync_group"), Montage->SyncGroup.ToString());
	Result->SetNumberField(TEXT("sync_slot_index"), Montage->SyncSlotIndex);
	return Result;
}

// ============================================================================
// set_montage_playback
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetMontagePlayback(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	const bool bAny =
		Params->HasField(TEXT("rate_scale")) ||
		Params->HasField(TEXT("loop"));
	if (!bAny)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Must provide rate_scale or loop"));

	Montage->Modify();

	if (Params->HasField(TEXT("rate_scale")))
	{
		Montage->RateScale = static_cast<float>(Params->GetNumberField(TEXT("rate_scale")));
	}
	if (Params->HasField(TEXT("loop")))
	{
		Montage->bLoop = Params->GetBoolField(TEXT("loop"));
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("rate_scale"), Montage->RateScale);
	Result->SetBoolField(TEXT("loop"), Montage->bLoop);
	return Result;
}

// ============================================================================
// set_montage_time_stretch
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetMontageTimeStretch(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	const bool bAny =
		Params->HasField(TEXT("time_stretch_curve_name")) ||
		Params->HasField(TEXT("time_stretch_curve"));
	if (!bAny)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Must provide time_stretch_curve_name or time_stretch_curve"));

	Montage->Modify();

	if (Params->HasField(TEXT("time_stretch_curve_name")))
	{
		const FString Val = Params->GetStringField(TEXT("time_stretch_curve_name"));
		Montage->TimeStretchCurveName = (Val.IsEmpty() || Val.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			? FName(NAME_None)
			: FName(*Val);
	}
	if (Params->HasField(TEXT("time_stretch_curve")))
	{
		const TSharedPtr<FJsonObject>* CurveObjPtr = nullptr;
		if (Params->TryGetObjectField(TEXT("time_stretch_curve"), CurveObjPtr) && CurveObjPtr && CurveObjPtr->IsValid())
		{
			// Reflection-based write through UPROPERTYs on FTimeStretchCurve.
			FJsonObjectConverter::JsonObjectToUStruct(
				CurveObjPtr->ToSharedRef(),
				FTimeStretchCurve::StaticStruct(),
				&Montage->TimeStretchCurve,
				CPF_Edit, 0);
		}
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("time_stretch_curve_name"), Montage->TimeStretchCurveName.ToString());
	return Result;
}

// ============================================================================
// set_montage_preview_base_pose
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetMontagePreviewBasePose(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

#if WITH_EDITORONLY_DATA
	if (!Params->HasField(TEXT("preview_base_pose_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: preview_base_pose_path"));

	const FString Path = Params->GetStringField(TEXT("preview_base_pose_path"));

	Montage->Modify();
	if (Path.IsEmpty() || Path.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		Montage->PreviewBasePose = nullptr;
	}
	else
	{
		UAnimSequence* Seq = Cast<UAnimSequence>(FEpicUnrealMCPCommonUtils::ResolveAsset(Path));
		if (!Seq)
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to load preview_base_pose_path: %s"), *Path));
		Montage->PreviewBasePose = Seq;
	}
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("preview_base_pose"),
		Montage->PreviewBasePose ? Montage->PreviewBasePose->GetPathName() : TEXT("None"));
	return Result;
#else
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("preview_base_pose is editor-only data"));
#endif
}

// ============================================================================
// set_montage_notify_track_color
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetMontageNotifyTrackColor(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	FString ResolveErr;
	const int32 TrackIdx = ResolveNotifyTrackIndex(Montage, Params, ResolveErr);
	if (TrackIdx == INDEX_NONE || !Montage->AnimNotifyTracks.IsValidIndex(TrackIdx))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			ResolveErr.IsEmpty() ? TEXT("Invalid track reference") : ResolveErr);

	const TSharedPtr<FJsonObject>* ColorObjPtr = nullptr;
	if (!Params->TryGetObjectField(TEXT("color"), ColorObjPtr) || !ColorObjPtr || !ColorObjPtr->IsValid())
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required object field: color { r, g, b, a }"));

	FLinearColor NewColor;
	if (!TryParseLinearColor(*ColorObjPtr, NewColor))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid color object"));

	Montage->Modify();
	Montage->AnimNotifyTracks[TrackIdx].TrackColor = NewColor;

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("index"), TrackIdx);
	Result->SetStringField(TEXT("name"), Montage->AnimNotifyTracks[TrackIdx].TrackName.ToString());
	Result->SetObjectField(TEXT("color"), LinearColorToJson(NewColor));
	return Result;
}

// ============================================================================
// Helper: apply section.MetaData add in place. Returns the new index or -1.
// ============================================================================

static int32 AddSectionMetadataInPlace(UAnimMontage* Montage, FCompositeSection& Section, const FString& ClassPath, const TSharedPtr<FJsonObject>& PropsObj, FString& OutError)
{
	OutError.Reset();

	UClass* MetaClass = LoadObject<UClass>(nullptr, *ClassPath);
	if (!MetaClass)
	{
		MetaClass = FindObject<UClass>(nullptr, *ClassPath);
	}
	if (!MetaClass)
	{
		OutError = FString::Printf(TEXT("Failed to resolve metadata class: %s"), *ClassPath);
		return INDEX_NONE;
	}
	if (!MetaClass->IsChildOf(UAnimMetaData::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Class '%s' is not a UAnimMetaData subclass"), *ClassPath);
		return INDEX_NONE;
	}

	UAnimMetaData* NewMeta = NewObject<UAnimMetaData>(Montage, MetaClass, NAME_None, RF_Transactional);
	if (PropsObj.IsValid())
	{
		FJsonObjectConverter::JsonObjectToUStruct(PropsObj.ToSharedRef(), MetaClass, NewMeta, CPF_Edit, 0);
	}

	// Use non-const cast: GetMetaData returns const ref; the underlying field is accessible only via the struct.
	// Use the Modify path by accessing MetaData directly through the section — it's a UPROPERTY.
	// FCompositeSection has MetaData as a private UPROPERTY but no setter, so reach through by appending
	// to the underlying array via a const_cast of the returned reference.
	const_cast<TArray<UAnimMetaData*>&>(Section.GetMetaData()).Add(NewMeta);
	return Section.GetMetaData().Num() - 1;
}

// ============================================================================
// add_section_metadata
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAddSectionMetadata(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("section_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: section_name"));
	if (!Params->HasField(TEXT("metadata_class_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: metadata_class_path"));

	const FString SectionName = Params->GetStringField(TEXT("section_name"));
	const int32 SectionIdx = Montage->GetSectionIndex(FName(*SectionName));
	if (SectionIdx == INDEX_NONE)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Section '%s' not found"), *SectionName));

	const FString ClassPath = Params->GetStringField(TEXT("metadata_class_path"));
	TSharedPtr<FJsonObject> PropsObj;
	if (Params->HasField(TEXT("properties")))
	{
		const TSharedPtr<FJsonObject>* P = nullptr;
		if (Params->TryGetObjectField(TEXT("properties"), P) && P && P->IsValid())
		{
			PropsObj = *P;
		}
	}

	Montage->Modify();

	FString AddErr;
	const int32 NewIdx = AddSectionMetadataInPlace(Montage, Montage->CompositeSections[SectionIdx], ClassPath, PropsObj, AddErr);
	if (NewIdx == INDEX_NONE)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(AddErr);

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("section_name"), SectionName);
	Result->SetNumberField(TEXT("metadata_index"), NewIdx);
	Result->SetStringField(TEXT("metadata_class_path"), ClassPath);
	Result->SetNumberField(TEXT("total_metadata"), Montage->CompositeSections[SectionIdx].GetMetaData().Num());
	return Result;
}

// ============================================================================
// remove_section_metadata
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleRemoveSectionMetadata(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("section_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: section_name"));
	if (!Params->HasField(TEXT("metadata_index")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: metadata_index"));

	const FString SectionName = Params->GetStringField(TEXT("section_name"));
	const int32 SectionIdx = Montage->GetSectionIndex(FName(*SectionName));
	if (SectionIdx == INDEX_NONE)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Section '%s' not found"), *SectionName));

	const int32 MetaIdx = static_cast<int32>(Params->GetNumberField(TEXT("metadata_index")));
	TArray<UAnimMetaData*>& MetaArray = const_cast<TArray<UAnimMetaData*>&>(Montage->CompositeSections[SectionIdx].GetMetaData());
	if (!MetaArray.IsValidIndex(MetaIdx))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("metadata_index %d out of range [0, %d)"), MetaIdx, MetaArray.Num()));

	Montage->Modify();
	MetaArray.RemoveAt(MetaIdx);
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("section_name"), SectionName);
	Result->SetNumberField(TEXT("removed_metadata_index"), MetaIdx);
	Result->SetNumberField(TEXT("remaining_metadata"), MetaArray.Num());
	return Result;
}

// ============================================================================
// Helper: move array element from OldIdx to NewIdx. Clamps NewIdx to valid range.
// Returns the resulting new index.
// ============================================================================

template<typename T>
static int32 MoveArrayElement(TArray<T>& Arr, int32 OldIdx, int32 NewIdx)
{
	if (!Arr.IsValidIndex(OldIdx))
	{
		return INDEX_NONE;
	}
	NewIdx = FMath::Clamp(NewIdx, 0, Arr.Num() - 1);
	if (NewIdx == OldIdx)
	{
		return OldIdx;
	}
	T Tmp = MoveTemp(Arr[OldIdx]);
	Arr.RemoveAt(OldIdx, 1, EAllowShrinking::No);
	Arr.Insert(MoveTemp(Tmp), NewIdx);
	return NewIdx;
}

// ============================================================================
// reorder_montage_section
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleReorderMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("from_index")) || !Params->HasField(TEXT("to_index")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing from_index/to_index"));

	const int32 From = static_cast<int32>(Params->GetNumberField(TEXT("from_index")));
	const int32 To = static_cast<int32>(Params->GetNumberField(TEXT("to_index")));

	if (!Montage->CompositeSections.IsValidIndex(From))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("from_index %d out of range [0, %d)"), From, Montage->CompositeSections.Num()));

	Montage->Modify();
	const int32 ResultIdx = MoveArrayElement(Montage->CompositeSections, From, To);
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("from_index"), From);
	Result->SetNumberField(TEXT("to_index"), ResultIdx);
	return Result;
}

// ============================================================================
// reorder_montage_segment
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleReorderMontageSegment(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	FString ResolveErr;
	const int32 SlotIndex = ResolveOrCreateSlotIndex(Montage, Params, false, ResolveErr);
	if (SlotIndex == INDEX_NONE)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ResolveErr);

	if (!Params->HasField(TEXT("from_index")) || !Params->HasField(TEXT("to_index")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing from_index/to_index"));

	const int32 From = static_cast<int32>(Params->GetNumberField(TEXT("from_index")));
	const int32 To = static_cast<int32>(Params->GetNumberField(TEXT("to_index")));

	TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments;
	if (!Segments.IsValidIndex(From))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("from_index %d out of range [0, %d)"), From, Segments.Num()));

	Montage->Modify();
	const int32 ResultIdx = MoveArrayElement(Segments, From, To);
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("slot_index"), SlotIndex);
	Result->SetNumberField(TEXT("from_index"), From);
	Result->SetNumberField(TEXT("to_index"), ResultIdx);
	return Result;
}

// ============================================================================
// reorder_montage_notify_track
// Must remap notify TrackIndex references to track the move.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleReorderMontageNotifyTrack(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("from_index")) || !Params->HasField(TEXT("to_index")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing from_index/to_index"));

	const int32 From = static_cast<int32>(Params->GetNumberField(TEXT("from_index")));
	const int32 ToRaw = static_cast<int32>(Params->GetNumberField(TEXT("to_index")));

	if (!Montage->AnimNotifyTracks.IsValidIndex(From))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("from_index %d out of range [0, %d)"), From, Montage->AnimNotifyTracks.Num()));

	const int32 To = FMath::Clamp(ToRaw, 0, Montage->AnimNotifyTracks.Num() - 1);
	if (From == To)
	{
		TSharedPtr<FJsonObject> Noop = MakeShareable(new FJsonObject);
		Noop->SetBoolField(TEXT("success"), true);
		Noop->SetNumberField(TEXT("from_index"), From);
		Noop->SetNumberField(TEXT("to_index"), To);
		return Noop;
	}

	Montage->Modify();
	MoveArrayElement(Montage->AnimNotifyTracks, From, To);

	// Remap every notify's TrackIndex. An index i (old) maps to:
	//   - To if i == From
	//   - i - 1 if From < i <= To (moving down shifts tracks in (From, To] up by 1)
	//   - i + 1 if To <= i < From (moving up shifts tracks in [To, From) down by 1)
	//   - unchanged otherwise
	auto RemapIndex = [&](int32 i) -> int32
	{
		if (i == From) return To;
		if (From < To)
		{
			return (i > From && i <= To) ? (i - 1) : i;
		}
		else // From > To
		{
			return (i >= To && i < From) ? (i + 1) : i;
		}
	};

	for (FAnimNotifyEvent& Evt : Montage->Notifies)
	{
		Evt.TrackIndex = RemapIndex(Evt.TrackIndex);
	}

	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("from_index"), From);
	Result->SetNumberField(TEXT("to_index"), To);
	return Result;
}

// ============================================================================
// reorder_montage_slot
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleReorderMontageSlot(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UAnimMontage* Montage = LoadMontage(Params, Error);
	if (!Montage) return Error;

	if (!Params->HasField(TEXT("from_index")) || !Params->HasField(TEXT("to_index")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing from_index/to_index"));

	const int32 From = static_cast<int32>(Params->GetNumberField(TEXT("from_index")));
	const int32 ToRaw = static_cast<int32>(Params->GetNumberField(TEXT("to_index")));

	if (!Montage->SlotAnimTracks.IsValidIndex(From))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("from_index %d out of range [0, %d)"), From, Montage->SlotAnimTracks.Num()));

	const int32 To = FMath::Clamp(ToRaw, 0, Montage->SlotAnimTracks.Num() - 1);
	if (From == To)
	{
		TSharedPtr<FJsonObject> Noop = MakeShareable(new FJsonObject);
		Noop->SetBoolField(TEXT("success"), true);
		Noop->SetNumberField(TEXT("from_index"), From);
		Noop->SetNumberField(TEXT("to_index"), To);
		return Noop;
	}

	Montage->Modify();
	MoveArrayElement(Montage->SlotAnimTracks, From, To);

	// SyncSlotIndex refers to slot index; remap it too.
	auto RemapIndex = [&](int32 i) -> int32
	{
		if (i == From) return To;
		if (From < To)
		{
			return (i > From && i <= To) ? (i - 1) : i;
		}
		else
		{
			return (i >= To && i < From) ? (i + 1) : i;
		}
	};
	Montage->SyncSlotIndex = RemapIndex(Montage->SyncSlotIndex);

	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("from_index"), From);
	Result->SetNumberField(TEXT("to_index"), To);
	Result->SetNumberField(TEXT("sync_slot_index"), Montage->SyncSlotIndex);
	return Result;
}

// ============================================================================
// Helper: Load UAnimSequence from "asset_path" param
// ============================================================================

static UAnimSequence* LoadSequence(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutError)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
		return nullptr;
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!LoadedAsset)
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
		return nullptr;
	}

	UAnimSequence* Sequence = Cast<UAnimSequence>(LoadedAsset);
	if (!Sequence)
	{
		OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UAnimSequence: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
		return nullptr;
	}
	return Sequence;
}

// ============================================================================
// Helper: Serialize FRichCurveKey to JSON
// ============================================================================

static TSharedPtr<FJsonObject> RichCurveKeyToJson(const FRichCurveKey& Key)
{
	TSharedPtr<FJsonObject> KeyObj = MakeShareable(new FJsonObject);
	KeyObj->SetNumberField(TEXT("time"), Key.Time);
	KeyObj->SetNumberField(TEXT("value"), Key.Value);

	switch (Key.InterpMode)
	{
		case RCIM_Constant: KeyObj->SetStringField(TEXT("interp_mode"), TEXT("Constant")); break;
		case RCIM_Linear:   KeyObj->SetStringField(TEXT("interp_mode"), TEXT("Linear")); break;
		case RCIM_Cubic:    KeyObj->SetStringField(TEXT("interp_mode"), TEXT("Cubic")); break;
		default:            KeyObj->SetStringField(TEXT("interp_mode"), TEXT("Linear")); break;
	}

	KeyObj->SetNumberField(TEXT("arrive_tangent"), Key.ArriveTangent);
	KeyObj->SetNumberField(TEXT("leave_tangent"), Key.LeaveTangent);
	return KeyObj;
}

// ============================================================================
// Helper: Parse interpolation mode from string
// ============================================================================

static ERichCurveInterpMode ParseInterpMode(const FString& Mode)
{
	if (Mode.Equals(TEXT("Constant"), ESearchCase::IgnoreCase)) return RCIM_Constant;
	if (Mode.Equals(TEXT("Linear"), ESearchCase::IgnoreCase))   return RCIM_Linear;
	if (Mode.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase))    return RCIM_Cubic;
	return RCIM_Linear;
}

// ============================================================================
// Helper: Parse JSON keys array into TArray<FRichCurveKey>
// ============================================================================

static bool ParseCurveKeys(const TArray<TSharedPtr<FJsonValue>>& KeysJson, TArray<FRichCurveKey>& OutKeys, FString& OutError)
{
	if (KeysJson.Num() == 0)
	{
		OutError = TEXT("Keys array must not be empty");
		return false;
	}

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

		if (KeyObj->HasField(TEXT("interp_mode")))
		{
			NewKey.InterpMode = ParseInterpMode(KeyObj->GetStringField(TEXT("interp_mode")));
		}
		else
		{
			NewKey.InterpMode = RCIM_Linear;
		}

		OutKeys.Add(NewKey);
	}

	return true;
}

// ============================================================================
// Helper: Find a named float curve on a sequence (returns nullptr if not found)
// ============================================================================

static const FFloatCurve* FindFloatCurve(const UAnimSequence* Sequence, const FString& CurveName)
{
	const IAnimationDataModel* DataModel = Sequence->GetDataModel();
	if (!DataModel)
	{
		return nullptr;
	}

	const TArray<FFloatCurve>& FloatCurves = DataModel->GetFloatCurves();
	for (const FFloatCurve& Curve : FloatCurves)
	{
		if (Curve.GetName().ToString() == CurveName)
		{
			return &Curve;
		}
	}
	return nullptr;
}

// ============================================================================
// get_anim_curve_keys
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleGetAnimCurveKeys(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> LoadError;
	UAnimSequence* Sequence = LoadSequence(Params, LoadError);
	if (!Sequence) return LoadError;

	if (!Params->HasField(TEXT("curve_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: curve_name"));
	}
	const FString CurveName = Params->GetStringField(TEXT("curve_name"));

	const FFloatCurve* FoundCurve = FindFloatCurve(Sequence, CurveName);
	if (!FoundCurve)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Curve '%s' not found on asset '%s'"), *CurveName, *Params->GetStringField(TEXT("asset_path"))));
	}

	const FRichCurve& RichCurve = FoundCurve->FloatCurve;
	TArray<TSharedPtr<FJsonValue>> KeysArray;
	for (const FRichCurveKey& Key : RichCurve.GetConstRefOfKeys())
	{
		KeysArray.Add(MakeShareable(new FJsonValueObject(RichCurveKeyToJson(Key))));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
	Result->SetStringField(TEXT("curve_name"), CurveName);
	Result->SetNumberField(TEXT("num_keys"), RichCurve.GetNumKeys());
	Result->SetArrayField(TEXT("keys"), KeysArray);
	return Result;
}

// ============================================================================
// get_root_motion_data
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleGetRootMotionData(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> LoadError;
	UAnimSequence* Sequence = LoadSequence(Params, LoadError);
	if (!Sequence) return LoadError;

	const double PlayLength = Sequence->GetPlayLength();
	if (PlayLength <= 0.0)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
		Result->SetNumberField(TEXT("play_length"), 0.0);
		Result->SetBoolField(TEXT("root_motion_enabled"), Sequence->bEnableRootMotion);
		Result->SetNumberField(TEXT("total_distance_3d"), 0.0);
		Result->SetNumberField(TEXT("horizontal_distance"), 0.0);
		Result->SetNumberField(TEXT("average_speed"), 0.0);
		return Result;
	}

	int32 NumSamples = 10;
	if (Params->HasField(TEXT("num_samples")))
	{
		NumSamples = FMath::Clamp(static_cast<int32>(Params->GetNumberField(TEXT("num_samples"))), 1, 100);
	}

	// Extract total root motion using UE5.7 non-deprecated API
	FAnimExtractContext ExtractionContext;
	ExtractionContext.bExtractRootMotion = true;
	const FTransform TotalRootMotion = Sequence->ExtractRootMotionFromRange(0.0, PlayLength, ExtractionContext);
	const FVector TotalTranslation = TotalRootMotion.GetTranslation();

	const double TotalDistance3D = TotalTranslation.Size();
	const double HorizontalDistance = FVector(TotalTranslation.X, TotalTranslation.Y, 0.0).Size();
	const double AverageSpeed = HorizontalDistance / PlayLength;

	// Build translation JSON
	TSharedPtr<FJsonObject> TranslationObj = MakeShareable(new FJsonObject);
	TranslationObj->SetNumberField(TEXT("x"), TotalTranslation.X);
	TranslationObj->SetNumberField(TEXT("y"), TotalTranslation.Y);
	TranslationObj->SetNumberField(TEXT("z"), TotalTranslation.Z);

	// Per-sample distance data
	TArray<TSharedPtr<FJsonValue>> SamplesArray;
	const double SampleStep = PlayLength / static_cast<double>(NumSamples);
	for (int32 i = 0; i <= NumSamples; ++i)
	{
		const double SampleTime = FMath::Min(SampleStep * i, PlayLength);
		FAnimExtractContext SampleContext;
		SampleContext.bExtractRootMotion = true;
		const FTransform SampleMotion = Sequence->ExtractRootMotionFromRange(0.0, SampleTime, SampleContext);
		const FVector SamplePos = SampleMotion.GetTranslation();

		TSharedPtr<FJsonObject> SampleObj = MakeShareable(new FJsonObject);
		SampleObj->SetNumberField(TEXT("time"), SampleTime);
		SampleObj->SetNumberField(TEXT("cumulative_distance"), FVector(SamplePos.X, SamplePos.Y, 0.0).Size());

		TSharedPtr<FJsonObject> PosObj = MakeShareable(new FJsonObject);
		PosObj->SetNumberField(TEXT("x"), SamplePos.X);
		PosObj->SetNumberField(TEXT("y"), SamplePos.Y);
		PosObj->SetNumberField(TEXT("z"), SamplePos.Z);
		SampleObj->SetObjectField(TEXT("position"), PosObj);

		SamplesArray.Add(MakeShareable(new FJsonValueObject(SampleObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
	Result->SetNumberField(TEXT("play_length"), PlayLength);
	Result->SetBoolField(TEXT("root_motion_enabled"), Sequence->bEnableRootMotion);
	Result->SetObjectField(TEXT("total_translation"), TranslationObj);
	Result->SetNumberField(TEXT("total_distance_3d"), TotalDistance3D);
	Result->SetNumberField(TEXT("horizontal_distance"), HorizontalDistance);
	Result->SetNumberField(TEXT("average_speed"), AverageSpeed);
	Result->SetArrayField(TEXT("samples"), SamplesArray);
	return Result;
}

// ============================================================================
// set_anim_sequence_root_motion
// ============================================================================
// Writes the AnimSequence-level root-motion flags. Distinct from set_montage_root_motion,
// which writes UAnimMontage flags — modern root-motion extraction is driven primarily by
// the source UAnimSequence, so flipping these is typically what you want for AAA closing
// distance / pushback behavior.

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetAnimSequenceRootMotion(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> LoadError;
	UAnimSequence* Sequence = LoadSequence(Params, LoadError);
	if (!Sequence) return LoadError;

	const bool bAny =
		Params->HasField(TEXT("enable_root_motion")) ||
		Params->HasField(TEXT("root_motion_root_lock")) ||
		Params->HasField(TEXT("force_root_lock")) ||
		Params->HasField(TEXT("use_normalized_root_motion_scale"));
	if (!bAny)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Must provide at least one of: enable_root_motion | root_motion_root_lock | force_root_lock | use_normalized_root_motion_scale"));
	}

	Sequence->Modify();

	if (Params->HasField(TEXT("enable_root_motion")))
	{
		Sequence->bEnableRootMotion = Params->GetBoolField(TEXT("enable_root_motion"));
	}
	if (Params->HasField(TEXT("root_motion_root_lock")))
	{
		ERootMotionRootLock::Type Lock;
		const FString Val = Params->GetStringField(TEXT("root_motion_root_lock"));
		if (!TryParseRootMotionRootLock(Val, Lock))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unknown root_motion_root_lock '%s' (expected RefPose|AnimFirstFrame|Zero)"), *Val));
		}
		Sequence->RootMotionRootLock = Lock;
	}
	if (Params->HasField(TEXT("force_root_lock")))
	{
		Sequence->bForceRootLock = Params->GetBoolField(TEXT("force_root_lock"));
	}
	if (Params->HasField(TEXT("use_normalized_root_motion_scale")))
	{
		Sequence->bUseNormalizedRootMotionScale = Params->GetBoolField(TEXT("use_normalized_root_motion_scale"));
	}

	Sequence->PostEditChange();
	Sequence->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
	Result->SetBoolField(TEXT("enable_root_motion"), Sequence->bEnableRootMotion);
	Result->SetStringField(TEXT("root_motion_root_lock"), RootMotionRootLockToString(Sequence->RootMotionRootLock));
	Result->SetBoolField(TEXT("force_root_lock"), Sequence->bForceRootLock);
	Result->SetBoolField(TEXT("use_normalized_root_motion_scale"), Sequence->bUseNormalizedRootMotionScale);
	return Result;
}

// ============================================================================
// set_anim_sequence_properties
// ============================================================================
// Writes interpolation / additive / compression-reference fields that affect how a
// UAnimSequence evaluates — orthogonal to the root-motion flags in
// set_anim_sequence_root_motion. Any unspecified field is left unchanged.
// For the three *_compression_settings and ref_pose_seq fields, pass an empty string
// or "None" to clear the reference; pass an asset path to look up and assign it.

template <typename T>
static bool ResolveAssetRef(const FString& Path, T*& OutAsset, FString& OutError)
{
	if (Path.IsEmpty() || Path.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		OutAsset = nullptr;
		return true;
	}
	UObject* Loaded = FEpicUnrealMCPCommonUtils::ResolveAsset(Path);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load asset: %s"), *Path);
		return false;
	}
	T* Typed = Cast<T>(Loaded);
	if (!Typed)
	{
		OutError = FString::Printf(TEXT("Asset '%s' is not a %s (is %s)"),
			*Path, *T::StaticClass()->GetName(), *Loaded->GetClass()->GetName());
		return false;
	}
	OutAsset = Typed;
	return true;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetAnimSequenceProperties(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> LoadError;
	UAnimSequence* Sequence = LoadSequence(Params, LoadError);
	if (!Sequence) return LoadError;

	const bool bAny =
		Params->HasField(TEXT("interpolation")) ||
		Params->HasField(TEXT("additive_anim_type")) ||
		Params->HasField(TEXT("ref_pose_type")) ||
		Params->HasField(TEXT("ref_pose_seq")) ||
		Params->HasField(TEXT("ref_frame_index")) ||
		Params->HasField(TEXT("bone_compression_settings")) ||
		Params->HasField(TEXT("curve_compression_settings")) ||
		Params->HasField(TEXT("variable_frame_stripping_settings"));
	if (!bAny)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Must provide at least one of: interpolation | additive_anim_type | ref_pose_type | ref_pose_seq | ref_frame_index | bone_compression_settings | curve_compression_settings | variable_frame_stripping_settings"));
	}

	Sequence->Modify();

	if (Params->HasField(TEXT("interpolation")))
	{
		EAnimInterpolationType Interp;
		const FString Val = Params->GetStringField(TEXT("interpolation"));
		if (!TryParseAnimInterpolationType(Val, Interp))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unknown interpolation '%s' (expected Linear|Step)"), *Val));
		}
		Sequence->Interpolation = Interp;
	}
	if (Params->HasField(TEXT("additive_anim_type")))
	{
		EAdditiveAnimationType AddType;
		const FString Val = Params->GetStringField(TEXT("additive_anim_type"));
		if (!TryParseAdditiveAnimType(Val, AddType))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unknown additive_anim_type '%s' (expected None|LocalSpaceBase|RotationOffsetMeshSpace)"), *Val));
		}
		Sequence->AdditiveAnimType = AddType;
	}
	if (Params->HasField(TEXT("ref_pose_type")))
	{
		EAdditiveBasePoseType BaseType;
		const FString Val = Params->GetStringField(TEXT("ref_pose_type"));
		if (!TryParseAdditiveBasePoseType(Val, BaseType))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unknown ref_pose_type '%s' (expected None|RefPose|AnimScaled|AnimFrame|LocalAnimFrame)"), *Val));
		}
		Sequence->RefPoseType = BaseType;
	}
	if (Params->HasField(TEXT("ref_frame_index")))
	{
		Sequence->RefFrameIndex = static_cast<int32>(Params->GetNumberField(TEXT("ref_frame_index")));
	}
	if (Params->HasField(TEXT("ref_pose_seq")))
	{
		UAnimSequence* RefSeq = nullptr;
		FString Err;
		if (!ResolveAssetRef<UAnimSequence>(Params->GetStringField(TEXT("ref_pose_seq")), RefSeq, Err))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);
		}
		Sequence->RefPoseSeq = RefSeq;
	}
	if (Params->HasField(TEXT("bone_compression_settings")))
	{
		UAnimBoneCompressionSettings* S = nullptr;
		FString Err;
		if (!ResolveAssetRef<UAnimBoneCompressionSettings>(Params->GetStringField(TEXT("bone_compression_settings")), S, Err))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);
		}
		Sequence->BoneCompressionSettings = S;
	}
	if (Params->HasField(TEXT("curve_compression_settings")))
	{
		UAnimCurveCompressionSettings* S = nullptr;
		FString Err;
		if (!ResolveAssetRef<UAnimCurveCompressionSettings>(Params->GetStringField(TEXT("curve_compression_settings")), S, Err))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);
		}
		Sequence->CurveCompressionSettings = S;
	}
	if (Params->HasField(TEXT("variable_frame_stripping_settings")))
	{
		UVariableFrameStrippingSettings* S = nullptr;
		FString Err;
		if (!ResolveAssetRef<UVariableFrameStrippingSettings>(Params->GetStringField(TEXT("variable_frame_stripping_settings")), S, Err))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);
		}
		Sequence->VariableFrameStrippingSettings = S;
	}

	Sequence->PostEditChange();
	Sequence->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
	Result->SetStringField(TEXT("interpolation"), AnimInterpolationTypeToString(Sequence->Interpolation));
	Result->SetStringField(TEXT("additive_anim_type"), AdditiveAnimTypeToString(Sequence->AdditiveAnimType));
	Result->SetStringField(TEXT("ref_pose_type"), AdditiveBasePoseTypeToString(Sequence->RefPoseType));
	Result->SetNumberField(TEXT("ref_frame_index"), Sequence->RefFrameIndex);
	Result->SetStringField(TEXT("ref_pose_seq"),
		Sequence->RefPoseSeq ? Sequence->RefPoseSeq->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("bone_compression_settings"),
		Sequence->BoneCompressionSettings ? Sequence->BoneCompressionSettings->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("curve_compression_settings"),
		Sequence->CurveCompressionSettings ? Sequence->CurveCompressionSettings->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("variable_frame_stripping_settings"),
		Sequence->VariableFrameStrippingSettings ? Sequence->VariableFrameStrippingSettings->GetPathName() : TEXT("None"));
	return Result;
}

// ============================================================================
// add_anim_curve
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAddAnimCurve(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> LoadError;
	UAnimSequence* Sequence = LoadSequence(Params, LoadError);
	if (!Sequence) return LoadError;

	if (!Params->HasField(TEXT("curve_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: curve_name"));
	}
	if (!Params->HasField(TEXT("keys")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: keys"));
	}

	const FString CurveName = Params->GetStringField(TEXT("curve_name"));

	// Check if curve already exists
	if (FindFloatCurve(Sequence, CurveName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Curve '%s' already exists. Use set_anim_curve_keys to modify it."), *CurveName));
	}

	// Parse keys
	TArray<FRichCurveKey> Keys;
	FString ParseError;
	if (!ParseCurveKeys(Params->GetArrayField(TEXT("keys")), Keys, ParseError))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ParseError);
	}

	// Create curve via controller
	const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
	IAnimationDataController& Controller = Sequence->GetController();

	Sequence->Modify();
	Controller.OpenBracket(FText::FromString(TEXT("Add Animation Curve")));
	const bool bCurveAdded = Controller.AddCurve(CurveId);
	if (!bCurveAdded)
	{
		Controller.CloseBracket();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to add curve '%s' via controller"), *CurveName));
	}
	Controller.SetCurveKeys(CurveId, Keys);
	Controller.CloseBracket();

	Sequence->PostEditChange();
	Sequence->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
	Result->SetStringField(TEXT("curve_name"), CurveName);
	Result->SetNumberField(TEXT("num_keys"), Keys.Num());
	return Result;
}

// ============================================================================
// set_anim_curve_keys
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleSetAnimCurveKeys(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> LoadError;
	UAnimSequence* Sequence = LoadSequence(Params, LoadError);
	if (!Sequence) return LoadError;

	if (!Params->HasField(TEXT("curve_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: curve_name"));
	}
	if (!Params->HasField(TEXT("keys")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: keys"));
	}

	const FString CurveName = Params->GetStringField(TEXT("curve_name"));

	// Verify curve exists
	if (!FindFloatCurve(Sequence, CurveName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Curve '%s' not found. Use add_anim_curve to create it first."), *CurveName));
	}

	// Parse keys
	TArray<FRichCurveKey> Keys;
	FString ParseError;
	if (!ParseCurveKeys(Params->GetArrayField(TEXT("keys")), Keys, ParseError))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ParseError);
	}

	// Set keys via controller
	const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
	IAnimationDataController& Controller = Sequence->GetController();

	Sequence->Modify();
	Controller.OpenBracket(FText::FromString(TEXT("Set Animation Curve Keys")));
	Controller.SetCurveKeys(CurveId, Keys);
	Controller.CloseBracket();

	Sequence->PostEditChange();
	Sequence->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
	Result->SetStringField(TEXT("curve_name"), CurveName);
	Result->SetNumberField(TEXT("num_keys"), Keys.Num());
	return Result;
}

// ============================================================================
// remove_anim_curve
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleRemoveAnimCurve(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> LoadError;
	UAnimSequence* Sequence = LoadSequence(Params, LoadError);
	if (!Sequence) return LoadError;

	if (!Params->HasField(TEXT("curve_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: curve_name"));
	}

	const FString CurveName = Params->GetStringField(TEXT("curve_name"));

	// Verify curve exists
	if (!FindFloatCurve(Sequence, CurveName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Curve '%s' not found on asset"), *CurveName));
	}

	// Remove via controller
	const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
	IAnimationDataController& Controller = Sequence->GetController();

	Sequence->Modify();
	Controller.OpenBracket(FText::FromString(TEXT("Remove Animation Curve")));
	const bool bRemoved = Controller.RemoveCurve(CurveId);
	Controller.CloseBracket();

	if (!bRemoved)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to remove curve '%s' via controller"), *CurveName));
	}

	Sequence->PostEditChange();
	Sequence->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
	Result->SetStringField(TEXT("removed_curve_name"), CurveName);
	return Result;
}

// ============================================================================
// batch_add_speed_curves
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleBatchAddSpeedCurves(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("directory")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: directory"));
	}

	const FString Directory = Params->GetStringField(TEXT("directory"));
	FString CurveName = TEXT("Speed");
	if (Params->HasField(TEXT("curve_name")))
	{
		CurveName = Params->GetStringField(TEXT("curve_name"));
	}
	bool bOverwrite = true;
	if (Params->HasField(TEXT("overwrite")))
	{
		bOverwrite = Params->GetBoolField(TEXT("overwrite"));
	}
	bool bDryRun = false;
	if (Params->HasField(TEXT("dry_run")))
	{
		bDryRun = Params->GetBoolField(TEXT("dry_run"));
	}

	// Sampling mode: "auto" (per-frame + collapse to flat on uniform clips), "per_frame", "constant"
	FString Mode = TEXT("auto");
	if (Params->HasField(TEXT("mode")))
	{
		Mode = Params->GetStringField(TEXT("mode")).ToLower();
	}
	if (Mode != TEXT("auto") && Mode != TEXT("per_frame") && Mode != TEXT("constant"))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid mode (expected 'auto', 'per_frame', or 'constant')"));
	}

	double RequestedSampleRateFps = 30.0;
	if (Params->HasField(TEXT("sample_rate_fps")))
	{
		RequestedSampleRateFps = Params->GetNumberField(TEXT("sample_rate_fps"));
		if (RequestedSampleRateFps <= 0.0) RequestedSampleRateFps = 30.0;
	}

	// Flat-collapse tolerance: absolute cm/s OR fractional (whichever is larger)
	double FlatAbsToleranceCmS = 1.5;
	if (Params->HasField(TEXT("flat_tolerance_cm_s")))
	{
		FlatAbsToleranceCmS = FMath::Max(0.0, Params->GetNumberField(TEXT("flat_tolerance_cm_s")));
	}
	double FlatFracTolerance = 0.03;
	if (Params->HasField(TEXT("flat_tolerance_fraction")))
	{
		FlatFracTolerance = FMath::Max(0.0, Params->GetNumberField(TEXT("flat_tolerance_fraction")));
	}

	// Find all UAnimSequence assets in directory
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Directory));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 ModifiedCount = 0;
	int32 SkippedCount = 0;

	for (const FAssetData& AssetData : AssetList)
	{
		TSharedPtr<FJsonObject> EntryObj = MakeShareable(new FJsonObject);
		const FString AssetPath = AssetData.GetObjectPathString();
		EntryObj->SetStringField(TEXT("asset_path"), AssetPath);
		EntryObj->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());

		// Load the sequence via the same editor path single-asset tools use, so compressed
		// data is fully built before we extract. AssetData.GetAsset() returns the UObject
		// before the editor finishes building compressed animation data on cold access,
		// which causes ExtractRootMotionFromRange to read empty data and return identity.
		UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
		UAnimSequence* Sequence = Cast<UAnimSequence>(LoadedAsset);
		if (!Sequence)
		{
			EntryObj->SetStringField(TEXT("action"), TEXT("error"));
			EntryObj->SetStringField(TEXT("error"), TEXT("Failed to load"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
			continue;
		}

		// Force synchronous compressed-data build and wait for any in-flight async compression
		// to complete. Without this, ExtractRootMotionFromRange can read empty compressed data
		// and return identity on cold-loaded assets even though raw animation data is populated.
		Sequence->CacheDerivedDataForCurrentPlatform();
		Sequence->WaitOnExistingCompression(/*bWantResults*/ true);

		const double PlayLength = Sequence->GetPlayLength();
		EntryObj->SetNumberField(TEXT("play_length"), PlayLength);

		if (PlayLength <= 0.0)
		{
			EntryObj->SetStringField(TEXT("action"), TEXT("skipped"));
			EntryObj->SetStringField(TEXT("reason"), TEXT("zero_length"));
			EntryObj->SetNumberField(TEXT("speed_value"), 0.0);
			SkippedCount++;
			ResultsArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
			continue;
		}

		// Full-clip average (always computed, used for reporting and flat collapse)
		FAnimExtractContext ExtractionContext;
		ExtractionContext.bExtractRootMotion = true;
		const FTransform RootMotion = Sequence->ExtractRootMotionFromRange(0.0, PlayLength, ExtractionContext);
		const FVector Translation = RootMotion.GetTranslation();
		const double HorizontalDistance = FVector(Translation.X, Translation.Y, 0.0).Size();
		const double AverageSpeed = HorizontalDistance / PlayLength;

		EntryObj->SetNumberField(TEXT("root_motion_distance"), HorizontalDistance);
		EntryObj->SetNumberField(TEXT("speed_value"), AverageSpeed);

		// Check if curve already exists
		const bool bCurveExists = (FindFloatCurve(Sequence, CurveName) != nullptr);

		if (bCurveExists && !bOverwrite)
		{
			EntryObj->SetStringField(TEXT("action"), TEXT("skipped"));
			EntryObj->SetStringField(TEXT("reason"), TEXT("curve_exists"));
			SkippedCount++;
			ResultsArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
			continue;
		}

		if (bDryRun)
		{
			EntryObj->SetStringField(TEXT("action"), bCurveExists ? TEXT("would_update") : TEXT("would_create"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
			continue;
		}

		// Resolve effective sample rate (max of requested and sequence rate, capped at 120)
		double EffectiveSampleRateFps = RequestedSampleRateFps;
		const FFrameRate SeqFrameRate = Sequence->GetSamplingFrameRate();
		if (SeqFrameRate.IsValid() && SeqFrameRate.AsDecimal() > EffectiveSampleRateFps)
		{
			EffectiveSampleRateFps = SeqFrameRate.AsDecimal();
		}
		EffectiveSampleRateFps = FMath::Clamp(EffectiveSampleRateFps, 10.0, 120.0);
		const double FrameDelta = 1.0 / EffectiveSampleRateFps;

		TArray<FRichCurveKey> Keys;
		FString UsedMode;
		double MinSampled = TNumericLimits<double>::Max();
		double MaxSampled = -TNumericLimits<double>::Max();

		if (Mode == TEXT("constant"))
		{
			UsedMode = TEXT("constant");
			FRichCurveKey S; S.Time = 0.0f; S.Value = static_cast<float>(AverageSpeed); S.InterpMode = RCIM_Constant; Keys.Add(S);
			FRichCurveKey E; E.Time = static_cast<float>(PlayLength); E.Value = static_cast<float>(AverageSpeed); E.InterpMode = RCIM_Constant; Keys.Add(E);
			MinSampled = MaxSampled = AverageSpeed;
		}
		else
		{
			// Per-frame sampling: centered window around each sample time
			const int32 NumSamples = FMath::Max(2, FMath::RoundToInt(PlayLength * EffectiveSampleRateFps) + 1);
			TArray<FRichCurveKey> SampledKeys;
			SampledKeys.Reserve(NumSamples);

			for (int32 i = 0; i < NumSamples; i++)
			{
				double T = (double)i * FrameDelta;
				if (T > PlayLength) T = PlayLength;

				double WStart = FMath::Max(0.0, T - FrameDelta * 0.5);
				double WEnd = FMath::Min((double)PlayLength, T + FrameDelta * 0.5);
				if (i == 0)
				{
					WStart = 0.0;
					WEnd = FMath::Min((double)PlayLength, FrameDelta);
				}
				else if (i == NumSamples - 1)
				{
					WEnd = PlayLength;
					WStart = FMath::Max(0.0, PlayLength - FrameDelta);
				}

				const double WinDur = WEnd - WStart;
				if (WinDur <= KINDA_SMALL_NUMBER)
				{
					continue;
				}

				FAnimExtractContext WinCtx;
				WinCtx.bExtractRootMotion = true;
				const FTransform WinMotion = Sequence->ExtractRootMotionFromRange(WStart, WEnd, WinCtx);
				const FVector WinTrans = WinMotion.GetTranslation();
				const double WinDist = FVector(WinTrans.X, WinTrans.Y, 0.0).Size();
				const double InstSpeed = WinDist / WinDur;

				MinSampled = FMath::Min(MinSampled, InstSpeed);
				MaxSampled = FMath::Max(MaxSampled, InstSpeed);

				FRichCurveKey K;
				K.Time = static_cast<float>(T);
				K.Value = static_cast<float>(InstSpeed);
				K.InterpMode = RCIM_Linear;
				SampledKeys.Add(K);
			}

			// Decide whether to collapse to flat (auto mode only)
			bool bFlatCollapse = false;
			if (Mode == TEXT("auto") && SampledKeys.Num() >= 2)
			{
				const double Spread = MaxSampled - MinSampled;
				const double MeanRef = FMath::Max(1.0, AverageSpeed);
				const double TolAbs = FlatAbsToleranceCmS;
				const double TolFrac = FlatFracTolerance * MeanRef;
				const double EffectiveTol = FMath::Max(TolAbs, TolFrac);
				if (Spread <= EffectiveTol)
				{
					bFlatCollapse = true;
				}
			}

			if (bFlatCollapse)
			{
				UsedMode = TEXT("flat");
				FRichCurveKey S; S.Time = 0.0f; S.Value = static_cast<float>(AverageSpeed); S.InterpMode = RCIM_Constant; Keys.Add(S);
				FRichCurveKey E; E.Time = static_cast<float>(PlayLength); E.Value = static_cast<float>(AverageSpeed); E.InterpMode = RCIM_Constant; Keys.Add(E);
			}
			else
			{
				UsedMode = TEXT("per_frame");
				Keys = MoveTemp(SampledKeys);
			}
		}

		EntryObj->SetStringField(TEXT("curve_mode"), UsedMode);
		EntryObj->SetNumberField(TEXT("sample_rate_fps"), EffectiveSampleRateFps);
		EntryObj->SetNumberField(TEXT("min_sampled_speed"), MinSampled == TNumericLimits<double>::Max() ? 0.0 : MinSampled);
		EntryObj->SetNumberField(TEXT("max_sampled_speed"), MaxSampled == -TNumericLimits<double>::Max() ? 0.0 : MaxSampled);
		EntryObj->SetNumberField(TEXT("num_keys"), Keys.Num());

		const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
		IAnimationDataController& Controller = Sequence->GetController();

		Sequence->Modify();
		Controller.OpenBracket(FText::FromString(TEXT("Batch Add Speed Curve")));

		if (bCurveExists)
		{
			Controller.SetCurveKeys(CurveId, Keys);
			EntryObj->SetStringField(TEXT("action"), TEXT("updated"));
		}
		else
		{
			if (Controller.AddCurve(CurveId))
			{
				Controller.SetCurveKeys(CurveId, Keys);
				EntryObj->SetStringField(TEXT("action"), TEXT("created"));
			}
			else
			{
				Controller.CloseBracket();
				EntryObj->SetStringField(TEXT("action"), TEXT("error"));
				EntryObj->SetStringField(TEXT("error"), TEXT("Failed to add curve"));
				ResultsArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
				continue;
			}
		}

		Controller.CloseBracket();
		Sequence->PostEditChange();
		Sequence->MarkPackageDirty();
		ModifiedCount++;

		ResultsArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetStringField(TEXT("curve_name"), CurveName);
	Result->SetBoolField(TEXT("dry_run"), bDryRun);
	Result->SetNumberField(TEXT("total_found"), AssetList.Num());
	Result->SetNumberField(TEXT("modified_count"), ModifiedCount);
	Result->SetNumberField(TEXT("skipped_count"), SkippedCount);
	Result->SetArrayField(TEXT("results"), ResultsArray);
	return Result;
}

// ============================================================================
// create_mirror_data_table
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleCreateMirrorDataTable(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path (e.g., /Game/PRK/Animation/MirrorTables/MDT_PRKSkeleton)"));
	}
	if (!Params->HasField(TEXT("skeleton_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: skeleton_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString SkeletonPath = Params->GetStringField(TEXT("skeleton_path"));

	// Load skeleton
	UObject* SkeletonObj = FEpicUnrealMCPCommonUtils::ResolveAsset(SkeletonPath);
	USkeleton* Skeleton = nullptr;
	if (SkeletonObj)
	{
		Skeleton = Cast<USkeleton>(SkeletonObj);
		if (!Skeleton)
		{
			// Maybe it's a skeletal mesh - get its skeleton
			if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(SkeletonObj))
			{
				Skeleton = SkelMesh->GetSkeleton();
			}
		}
	}
	if (!Skeleton)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load skeleton from: %s"), *SkeletonPath));
	}

	// Check if asset already exists
	if (FEpicUnrealMCPCommonUtils::AssetExists(AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset already exists at: %s"), *AssetPath));
	}

	// Parse asset name from path
	FString PackagePath, AssetName;
	const int32 LastSlash = AssetPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (LastSlash != INDEX_NONE)
	{
		PackagePath = AssetPath;
		AssetName = AssetPath.Mid(LastSlash + 1);
	}
	else
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid asset_path format"));
	}

	// Create the package and mirror data table
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to create package: %s"), *PackagePath));
	}

	UMirrorDataTable* MDT = NewObject<UMirrorDataTable>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!MDT)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create UMirrorDataTable object"));
	}

	// Set row struct and skeleton
	MDT->RowStruct = const_cast<UScriptStruct*>(FMirrorTableRow::StaticStruct());
	MDT->Skeleton = Skeleton;

	// Mirror axis (default X for left/right)
	if (Params->HasField(TEXT("mirror_axis")))
	{
		const FString AxisStr = Params->GetStringField(TEXT("mirror_axis"));
		if (AxisStr.Equals(TEXT("X"), ESearchCase::IgnoreCase)) MDT->MirrorAxis = EAxis::X;
		else if (AxisStr.Equals(TEXT("Y"), ESearchCase::IgnoreCase)) MDT->MirrorAxis = EAxis::Y;
		else if (AxisStr.Equals(TEXT("Z"), ESearchCase::IgnoreCase)) MDT->MirrorAxis = EAxis::Z;
	}
	else
	{
		MDT->MirrorAxis = EAxis::X;
	}

	// Mirror root motion flag
	if (Params->HasField(TEXT("mirror_root_motion")))
	{
		MDT->bMirrorRootMotion = Params->GetBoolField(TEXT("mirror_root_motion"));
	}

	// Parse find/replace expressions
	if (Params->HasField(TEXT("expressions")))
	{
		const TArray<TSharedPtr<FJsonValue>>& ExpressionsArray = Params->GetArrayField(TEXT("expressions"));
		for (const TSharedPtr<FJsonValue>& ExprVal : ExpressionsArray)
		{
			const TSharedPtr<FJsonObject>* ExprObjPtr = nullptr;
			if (!ExprVal->TryGetObject(ExprObjPtr) || !ExprObjPtr || !ExprObjPtr->IsValid())
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& ExprObj = *ExprObjPtr;

			FMirrorFindReplaceExpression Expr;
			if (ExprObj->HasField(TEXT("find")))
			{
				Expr.FindExpression = FName(*ExprObj->GetStringField(TEXT("find")));
			}
			if (ExprObj->HasField(TEXT("replace")))
			{
				Expr.ReplaceExpression = FName(*ExprObj->GetStringField(TEXT("replace")));
			}

			// Method: Prefix (default), Suffix, RegularExpression
			if (ExprObj->HasField(TEXT("method")))
			{
				const FString MethodStr = ExprObj->GetStringField(TEXT("method"));
				if (MethodStr.Equals(TEXT("Suffix"), ESearchCase::IgnoreCase))
					Expr.FindReplaceMethod = EMirrorFindReplaceMethod::Suffix;
				else if (MethodStr.Equals(TEXT("RegularExpression"), ESearchCase::IgnoreCase) || MethodStr.Equals(TEXT("Regex"), ESearchCase::IgnoreCase))
					Expr.FindReplaceMethod = EMirrorFindReplaceMethod::RegularExpression;
				else
					Expr.FindReplaceMethod = EMirrorFindReplaceMethod::Prefix;
			}
			else
			{
				Expr.FindReplaceMethod = EMirrorFindReplaceMethod::Suffix;
			}

			MDT->MirrorFindReplaceExpressions.Add(Expr);
		}
	}
	else
	{
		// Default: standard _l/_r suffix swap (most common UE5 convention)
		MDT->MirrorFindReplaceExpressions.Add(FMirrorFindReplaceExpression(
			FName(TEXT("_l")), FName(TEXT("_r")), EMirrorFindReplaceMethod::Suffix));
		MDT->MirrorFindReplaceExpressions.Add(FMirrorFindReplaceExpression(
			FName(TEXT("_r")), FName(TEXT("_l")), EMirrorFindReplaceMethod::Suffix));
	}

	// Auto-populate bone mappings from skeleton + find/replace expressions
	MDT->FindReplaceMirroredNames();

	// Register and save
	FAssetRegistryModule::AssetCreated(MDT);
	MDT->PostEditChange();
	Package->MarkPackageDirty();

	// Count rows
	int32 RowCount = 0;
	const TMap<FName, uint8*>& RowMap = MDT->GetRowMap();
	RowCount = RowMap.Num();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	Result->SetStringField(TEXT("mirror_axis"), MDT->MirrorAxis == EAxis::X ? TEXT("X") : MDT->MirrorAxis == EAxis::Y ? TEXT("Y") : TEXT("Z"));
	Result->SetBoolField(TEXT("mirror_root_motion"), MDT->bMirrorRootMotion);
	Result->SetNumberField(TEXT("row_count"), RowCount);
	Result->SetNumberField(TEXT("expression_count"), MDT->MirrorFindReplaceExpressions.Num());
	return Result;
}

// ============================================================================
// analyze_mirror_data_table
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAnimationCommands::HandleAnalyzeMirrorDataTable(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UObject* LoadedAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	UMirrorDataTable* MDT = Cast<UMirrorDataTable>(LoadedAsset);
	if (!MDT)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UMirrorDataTable: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), MDT->GetName());
	Result->SetStringField(TEXT("skeleton"), MDT->Skeleton ? MDT->Skeleton->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("mirror_axis"), MDT->MirrorAxis == EAxis::X ? TEXT("X") : MDT->MirrorAxis == EAxis::Y ? TEXT("Y") : TEXT("Z"));
	Result->SetBoolField(TEXT("mirror_root_motion"), MDT->bMirrorRootMotion);

	// Find/Replace expressions
	TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
	for (const FMirrorFindReplaceExpression& Expr : MDT->MirrorFindReplaceExpressions)
	{
		TSharedPtr<FJsonObject> ExprObj = MakeShareable(new FJsonObject);
		ExprObj->SetStringField(TEXT("find"), Expr.FindExpression.ToString());
		ExprObj->SetStringField(TEXT("replace"), Expr.ReplaceExpression.ToString());
		FString MethodStr;
		switch (Expr.FindReplaceMethod)
		{
			case EMirrorFindReplaceMethod::Prefix: MethodStr = TEXT("Prefix"); break;
			case EMirrorFindReplaceMethod::Suffix: MethodStr = TEXT("Suffix"); break;
			case EMirrorFindReplaceMethod::RegularExpression: MethodStr = TEXT("RegularExpression"); break;
		}
		ExprObj->SetStringField(TEXT("method"), MethodStr);
		ExpressionsArray.Add(MakeShareable(new FJsonValueObject(ExprObj)));
	}
	Result->SetArrayField(TEXT("expressions"), ExpressionsArray);

	// Rows - iterate the data table row map
	TArray<TSharedPtr<FJsonValue>> RowsArray;
	const TMap<FName, uint8*>& RowMap = MDT->GetRowMap();
	for (const auto& Pair : RowMap)
	{
		const FMirrorTableRow* Row = reinterpret_cast<const FMirrorTableRow*>(Pair.Value);
		if (!Row) continue;

		TSharedPtr<FJsonObject> RowObj = MakeShareable(new FJsonObject);
		RowObj->SetStringField(TEXT("row_name"), Pair.Key.ToString());
		RowObj->SetStringField(TEXT("name"), Row->Name.ToString());
		RowObj->SetStringField(TEXT("mirrored_name"), Row->MirroredName.ToString());

		FString TypeStr;
		switch (Row->MirrorEntryType)
		{
			case EMirrorRowType::Bone: TypeStr = TEXT("Bone"); break;
			case EMirrorRowType::AnimationNotify: TypeStr = TEXT("AnimationNotify"); break;
			case EMirrorRowType::Curve: TypeStr = TEXT("Curve"); break;
			case EMirrorRowType::SyncMarker: TypeStr = TEXT("SyncMarker"); break;
			case EMirrorRowType::Custom: TypeStr = TEXT("Custom"); break;
		}
		RowObj->SetStringField(TEXT("type"), TypeStr);

		RowsArray.Add(MakeShareable(new FJsonValueObject(RowObj)));
	}
	Result->SetArrayField(TEXT("rows"), RowsArray);
	Result->SetNumberField(TEXT("row_count"), RowMap.Num());

	return Result;
}
