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
#include "Animation/Skeleton.h"

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

	// Skeleton reference
	USkeleton* Skeleton = SkelMesh->GetSkeleton();
	if (Skeleton)
	{
		Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	}

	// Reference skeleton for bone data
	const FReferenceSkeleton& RefSkeleton = SkelMesh->GetRefSkeleton();
	const int32 NumBones = RefSkeleton.GetNum();
	Result->SetNumberField(TEXT("num_bones"), NumBones);

	// LODs
	Result->SetNumberField(TEXT("num_lods"), SkelMesh->GetLODNum());

	// Materials
	const TArray<FSkeletalMaterial>& Materials = SkelMesh->GetMaterials();
	Result->SetNumberField(TEXT("num_materials"), Materials.Num());

	TArray<TSharedPtr<FJsonValue>> MaterialsArray;
	for (const FSkeletalMaterial& Mat : Materials)
	{
		TSharedPtr<FJsonObject> MatObj = MakeShareable(new FJsonObject);
		MatObj->SetStringField(TEXT("slot_name"), Mat.MaterialSlotName.ToString());
		MatObj->SetStringField(TEXT("material"), Mat.MaterialInterface ? Mat.MaterialInterface->GetPathName() : TEXT("None"));
		MaterialsArray.Add(MakeShareable(new FJsonValueObject(MatObj)));
	}
	Result->SetArrayField(TEXT("materials"), MaterialsArray);

	// Sockets (combined mesh + skeleton sockets, no duplicates)
	TArray<USkeletalMeshSocket*> ActiveSockets = SkelMesh->GetActiveSocketList();
	Result->SetNumberField(TEXT("num_sockets"), ActiveSockets.Num());

	TArray<TSharedPtr<FJsonValue>> SocketsArray;
	for (const USkeletalMeshSocket* Socket : ActiveSockets)
	{
		if (!Socket)
		{
			continue;
		}

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

	// Bones
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
