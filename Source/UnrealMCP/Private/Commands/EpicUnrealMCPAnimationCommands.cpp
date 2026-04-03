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
	else if (CommandType == TEXT("add_montage_notify"))
	{
		return HandleAddMontageNotify(Params);
	}
	else if (CommandType == TEXT("remove_montage_notify"))
	{
		return HandleRemoveMontageNotify(Params);
	}
	else if (CommandType == TEXT("add_montage_segment"))
	{
		return HandleAddMontageSegment(Params);
	}
	else if (CommandType == TEXT("set_montage_blend_times"))
	{
		return HandleSetMontageBlendTimes(Params);
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
// Helper: Build notify array from UAnimSequenceBase::Notifies
// ============================================================================

static TArray<TSharedPtr<FJsonValue>> BuildNotifiesArray(const UAnimSequenceBase* AnimBase)
{
	TArray<TSharedPtr<FJsonValue>> NotifiesArray;
	if (!AnimBase)
	{
		return NotifiesArray;
	}

	for (const FAnimNotifyEvent& Notify : AnimBase->Notifies)
	{
		TSharedPtr<FJsonObject> NotifyObj = MakeShareable(new FJsonObject);

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

		NotifyObj->SetNumberField(TEXT("trigger_time"), Notify.GetTriggerTime());
		NotifyObj->SetNumberField(TEXT("duration"), Notify.GetDuration());
		NotifyObj->SetBoolField(TEXT("is_state"), Notify.NotifyStateClass != nullptr);

		// Notify class name
		FString NotifyClassName = TEXT("None");
		if (Notify.Notify)
		{
			NotifyClassName = Notify.Notify->GetClass()->GetName();
		}
		else if (Notify.NotifyStateClass)
		{
			NotifyClassName = Notify.NotifyStateClass->GetClass()->GetName();
		}
		NotifyObj->SetStringField(TEXT("notify_class"), NotifyClassName);

		NotifyObj->SetNumberField(TEXT("track_index"), Notify.TrackIndex);

		NotifiesArray.Add(MakeShareable(new FJsonValueObject(NotifyObj)));
	}

	return NotifiesArray;
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
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
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

	// Composite sections
	TArray<TSharedPtr<FJsonValue>> SectionsArray;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		TSharedPtr<FJsonObject> SectionObj = MakeShareable(new FJsonObject);
		SectionObj->SetStringField(TEXT("name"), Section.SectionName.ToString());
		SectionObj->SetNumberField(TEXT("start_time"), Section.GetTime());
		SectionObj->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
		SectionObj->SetNumberField(TEXT("link_method"), static_cast<int32>(Section.GetLinkMethod()));

		SectionsArray.Add(MakeShareable(new FJsonValueObject(SectionObj)));
	}
	Result->SetArrayField(TEXT("sections"), SectionsArray);

	// Slot tracks
	TArray<TSharedPtr<FJsonValue>> SlotTracksArray;
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		TSharedPtr<FJsonObject> TrackObj = MakeShareable(new FJsonObject);
		TrackObj->SetStringField(TEXT("slot_name"), Track.SlotName.ToString());

		TArray<TSharedPtr<FJsonValue>> SegmentsArray;
		for (const FAnimSegment& Seg : Track.AnimTrack.AnimSegments)
		{
			TSharedPtr<FJsonObject> SegObj = MakeShareable(new FJsonObject);
			SegObj->SetNumberField(TEXT("start_pos"), Seg.StartPos);
			SegObj->SetNumberField(TEXT("anim_start_time"), Seg.AnimStartTime);
			SegObj->SetNumberField(TEXT("anim_end_time"), Seg.AnimEndTime);
			SegObj->SetNumberField(TEXT("anim_play_rate"), Seg.AnimPlayRate);

			UAnimSequenceBase* AnimRef = Seg.GetAnimReference();
			SegObj->SetStringField(TEXT("anim_reference"), AnimRef ? AnimRef->GetPathName() : TEXT("None"));

			SegmentsArray.Add(MakeShareable(new FJsonValueObject(SegObj)));
		}
		TrackObj->SetArrayField(TEXT("segments"), SegmentsArray);

		SlotTracksArray.Add(MakeShareable(new FJsonValueObject(TrackObj)));
	}
	Result->SetArrayField(TEXT("slot_tracks"), SlotTracksArray);

	// Notifies (inherited from UAnimSequenceBase)
	Result->SetArrayField(TEXT("notifies"), BuildNotifiesArray(Montage));

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
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
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
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
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
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
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

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
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

	// Get skeletal mesh component
	USkeletalMeshComponent* SkelComp = TargetActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!SkelComp)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' has no SkeletalMeshComponent"), *ActorName));
	}

	// Verify socket exists
	if (!SkelComp->DoesSocketExist(FName(*SocketName)))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Socket '%s' not found on actor's mesh"), *SocketName));
	}

	// Load the preview mesh
	UObject* MeshAsset = UEditorAssetLibrary::LoadAsset(MeshPath);
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
	PreviewActor->AttachToComponent(SkelComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName(*SocketName));

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
	UObject* MeshAsset = UEditorAssetLibrary::LoadAsset(MeshPath);
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
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset already exists: %s"), *FullPath));
	}

	// Load skeleton (supports USkeleton or USkeletalMesh)
	UObject* SkelObj = UEditorAssetLibrary::LoadAsset(SkeletonPath);
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
		UObject* SourceObj = UEditorAssetLibrary::LoadAsset(SourcePath);
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

	// Ensure starting section at T=0
	if (Montage->CompositeSections.Num() == 0)
	{
		Montage->AddAnimCompositeSection(FName(TEXT("Default")), 0.0f);
	}

	FAssetRegistryModule::AssetCreated(Montage);
	Montage->PostEditChange();
	Package->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), FullPath);
	Result->SetStringField(TEXT("asset_name"), AssetName);
	Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	Result->SetNumberField(TEXT("play_length"), Montage->GetPlayLength());
	Result->SetStringField(TEXT("slot_name"), SlotName);
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
	const int32 TrackIndex = Params->HasField(TEXT("track_index"))
		? static_cast<int32>(Params->GetNumberField(TEXT("track_index")))
		: 0;
	const bool bHasDuration = Params->HasField(TEXT("duration"));
	const float Duration = bHasDuration
		? static_cast<float>(Params->GetNumberField(TEXT("duration")))
		: 0.0f;

	Montage->Modify();

	FAnimNotifyEvent NewNotify;
	NewNotify.NotifyName = FName(*NotifyName);
	NewNotify.Link(Montage, TriggerTime);
	NewNotify.TrackIndex = TrackIndex;
	NewNotify.TriggerWeightThreshold = 0.0f;
	NewNotify.MontageTickType = EMontageNotifyTickType::Queued;
	NewNotify.NotifyTriggerChance = 1.0f;

	if (Params->HasField(TEXT("notify_class")))
	{
		const FString NotifyClassName = Params->GetStringField(TEXT("notify_class"));
		FString ClassName = NotifyClassName;
		UClass* NotifyClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
		if (!NotifyClass && !ClassName.StartsWith(TEXT("U")))
		{
			NotifyClass = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::EnsureIfAmbiguous);
		}

		if (NotifyClass)
		{
			if (NotifyClass->IsChildOf(UAnimNotifyState::StaticClass()))
			{
				NewNotify.NotifyStateClass = NewObject<UAnimNotifyState>(Montage, NotifyClass);
				NewNotify.Duration = Duration;
			}
			else if (NotifyClass->IsChildOf(UAnimNotify::StaticClass()))
			{
				NewNotify.Notify = NewObject<UAnimNotify>(Montage, NotifyClass);
			}
		}
	}
	else if (bHasDuration)
	{
		NewNotify.Duration = Duration;
	}

	Montage->Notifies.Add(NewNotify);
	Montage->SortNotifies();
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("notify_name"), NotifyName);
	Result->SetNumberField(TEXT("trigger_time"), TriggerTime);
	Result->SetNumberField(TEXT("track_index"), TrackIndex);
	Result->SetNumberField(TEXT("total_notifies"), Montage->Notifies.Num());
	if (bHasDuration)
		Result->SetNumberField(TEXT("duration"), Duration);
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

	Montage->Modify();

	int32 RemoveIndex = INDEX_NONE;
	FString RemovedName;

	if (bHasIndex)
	{
		RemoveIndex = static_cast<int32>(Params->GetNumberField(TEXT("notify_index")));
		if (RemoveIndex < 0 || RemoveIndex >= Montage->Notifies.Num())
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Notify index %d out of range [0, %d)"), RemoveIndex, Montage->Notifies.Num()));
		RemovedName = Montage->Notifies[RemoveIndex].NotifyName.ToString();
	}
	else
	{
		const FString NotifyName = Params->GetStringField(TEXT("notify_name"));
		for (int32 i = 0; i < Montage->Notifies.Num(); ++i)
		{
			if (Montage->Notifies[i].NotifyName.ToString() == NotifyName)
			{
				RemoveIndex = i;
				RemovedName = NotifyName;
				break;
			}
		}
		if (RemoveIndex == INDEX_NONE)
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Notify '%s' not found"), *NotifyName));
	}

	Montage->Notifies.RemoveAt(RemoveIndex);
	Montage->SortNotifies();
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_notify"), RemovedName);
	Result->SetNumberField(TEXT("remaining_notifies"), Montage->Notifies.Num());
	return Result;
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
	UObject* AnimObj = UEditorAssetLibrary::LoadAsset(AnimPath);
	if (!AnimObj)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load animation: %s"), *AnimPath));

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimObj);
	if (!AnimSequence)
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UAnimSequence: %s"), *AnimPath));

	const int32 SlotIndex = Params->HasField(TEXT("slot_index"))
		? static_cast<int32>(Params->GetNumberField(TEXT("slot_index")))
		: 0;

	if (SlotIndex < 0 || SlotIndex >= Montage->SlotAnimTracks.Num())
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Slot index %d out of range [0, %d)"), SlotIndex, Montage->SlotAnimTracks.Num()));

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

	Montage->Modify();

	FAnimSegment NewSegment;
	NewSegment.SetAnimReference(AnimSequence, true);
	NewSegment.StartPos = StartPos;
	NewSegment.AnimStartTime = AnimStartTime;
	NewSegment.AnimEndTime = AnimEndTime;
	NewSegment.AnimPlayRate = PlayRate;

	Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments.Add(NewSegment);

	// Recalculate composite length from all segments
	float MaxEndPos = 0.0f;
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Seg : Track.AnimTrack.AnimSegments)
		{
			float SegEnd = Seg.StartPos + (Seg.AnimEndTime - Seg.AnimStartTime) / FMath::Max(Seg.AnimPlayRate, 0.01f);
			MaxEndPos = FMath::Max(MaxEndPos, SegEnd);
		}
	}
	Montage->SetCompositeLength(MaxEndPos);

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animation_path"), AnimPath);
	Result->SetNumberField(TEXT("slot_index"), SlotIndex);
	Result->SetNumberField(TEXT("start_pos"), StartPos);
	Result->SetNumberField(TEXT("composite_length"), Montage->GetPlayLength());
	Result->SetNumberField(TEXT("total_segments"), Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments.Num());
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

	if (!Params->HasField(TEXT("blend_in_time")) && !Params->HasField(TEXT("blend_out_time")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Must provide at least one of blend_in_time or blend_out_time"));

	Montage->Modify();

	if (Params->HasField(TEXT("blend_in_time")))
	{
		const float BlendIn = static_cast<float>(Params->GetNumberField(TEXT("blend_in_time")));
		Montage->BlendIn.SetBlendTime(BlendIn);
	}
	if (Params->HasField(TEXT("blend_out_time")))
	{
		const float BlendOut = static_cast<float>(Params->GetNumberField(TEXT("blend_out_time")));
		Montage->BlendOut.SetBlendTime(BlendOut);
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Params->GetStringField(TEXT("asset_path")));
	Result->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
	Result->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());
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
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
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

		// Load the sequence
		UAnimSequence* Sequence = Cast<UAnimSequence>(AssetData.GetAsset());
		if (!Sequence)
		{
			EntryObj->SetStringField(TEXT("action"), TEXT("error"));
			EntryObj->SetStringField(TEXT("error"), TEXT("Failed to load"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
			continue;
		}

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

		// Extract root motion
		FAnimExtractContext ExtractionContext;
		ExtractionContext.bExtractRootMotion = true;
		const FTransform RootMotion = Sequence->ExtractRootMotionFromRange(0.0, PlayLength, ExtractionContext);
		const FVector Translation = RootMotion.GetTranslation();
		const double HorizontalDistance = FVector(Translation.X, Translation.Y, 0.0).Size();
		const double SpeedValue = HorizontalDistance / PlayLength;

		EntryObj->SetNumberField(TEXT("root_motion_distance"), HorizontalDistance);
		EntryObj->SetNumberField(TEXT("speed_value"), SpeedValue);

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

		// Build constant curve keys
		TArray<FRichCurveKey> Keys;
		FRichCurveKey StartKey;
		StartKey.Time = 0.0f;
		StartKey.Value = static_cast<float>(SpeedValue);
		StartKey.InterpMode = RCIM_Constant;
		Keys.Add(StartKey);

		FRichCurveKey EndKey;
		EndKey.Time = static_cast<float>(PlayLength);
		EndKey.Value = static_cast<float>(SpeedValue);
		EndKey.InterpMode = RCIM_Constant;
		Keys.Add(EndKey);

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
	UObject* SkeletonObj = UEditorAssetLibrary::LoadAsset(SkeletonPath);
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
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
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
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
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
