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
