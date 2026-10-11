#include "Commands/EpicUnrealMCPStaticMeshCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/ColorVertexBuffer.h"
#include "StaticMeshEditorSubsystem.h"
#include "PhysicsEngine/BodySetup.h"
#include "AI/Navigation/NavCollisionBase.h"
#include "Engine/StaticMeshSocket.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "Misc/ScopeExit.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Editor.h"

// ============================================================================
// Local JSON helpers (self-contained — mirror the animation-command helpers)
// ============================================================================

namespace
{
	FVector JsonToVector(const TSharedPtr<FJsonObject>& Obj, const FVector& Default = FVector::ZeroVector)
	{
		if (!Obj.IsValid()) return Default;
		return FVector(
			Obj->GetNumberField(TEXT("x")),
			Obj->GetNumberField(TEXT("y")),
			Obj->GetNumberField(TEXT("z"))
		);
	}

	FRotator JsonToRotator(const TSharedPtr<FJsonObject>& Obj, const FRotator& Default = FRotator::ZeroRotator)
	{
		if (!Obj.IsValid()) return Default;
		return FRotator(
			Obj->GetNumberField(TEXT("pitch")),
			Obj->GetNumberField(TEXT("yaw")),
			Obj->GetNumberField(TEXT("roll"))
		);
	}

	TSharedPtr<FJsonObject> VectorToJson(const FVector& V)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetNumberField(TEXT("x"), V.X);
		Obj->SetNumberField(TEXT("y"), V.Y);
		Obj->SetNumberField(TEXT("z"), V.Z);
		return Obj;
	}

	TSharedPtr<FJsonObject> RotatorToJson(const FRotator& R)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetNumberField(TEXT("pitch"), R.Pitch);
		Obj->SetNumberField(TEXT("yaw"), R.Yaw);
		Obj->SetNumberField(TEXT("roll"), R.Roll);
		return Obj;
	}

	UStaticMesh* LoadStaticMesh(const TSharedPtr<FJsonObject>& Params, const FString& FieldName, TSharedPtr<FJsonObject>& OutError)
	{
		if (!Params->HasField(FieldName))
		{
			OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Missing required field: %s"), *FieldName));
			return nullptr;
		}

		const FString AssetPath = Params->GetStringField(FieldName);
		UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
		if (!LoadedAsset)
		{
			OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
			return nullptr;
		}

		UStaticMesh* StaticMesh = Cast<UStaticMesh>(LoadedAsset);
		if (!StaticMesh)
		{
			OutError = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Asset is not a UStaticMesh: %s (is %s)"), *AssetPath, *LoadedAsset->GetClass()->GetName()));
			return nullptr;
		}
		return StaticMesh;
	}

	TSharedPtr<FJsonObject> SerializeStaticMeshSocket(const UStaticMeshSocket* Socket)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		if (!Socket) return Obj;

		Obj->SetStringField(TEXT("name"), Socket->SocketName.ToString());
		Obj->SetStringField(TEXT("tag"), Socket->Tag);
		Obj->SetObjectField(TEXT("location"), VectorToJson(Socket->RelativeLocation));
		Obj->SetObjectField(TEXT("rotation"), RotatorToJson(Socket->RelativeRotation));
		Obj->SetObjectField(TEXT("scale"), VectorToJson(Socket->RelativeScale));

		if (Socket->PreviewStaticMesh)
		{
			Obj->SetStringField(TEXT("preview_static_mesh"), Socket->PreviewStaticMesh->GetPathName());
		}
		return Obj;
	}
}

FEpicUnrealMCPStaticMeshCommands::FEpicUnrealMCPStaticMeshCommands()
{
}

FEpicUnrealMCPStaticMeshCommands::~FEpicUnrealMCPStaticMeshCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_static_mesh_info"))
	{
		return HandleGetStaticMeshInfo(Params);
	}
	else if (CommandType == TEXT("get_mesh_footprint"))
	{
		return HandleGetMeshFootprint(Params);
	}
	else if (CommandType == TEXT("get_mesh_collision_info"))
	{
		return HandleGetMeshCollisionInfo(Params);
	}
	else if (CommandType == TEXT("generate_convex_collision"))
	{
		return HandleGenerateConvexCollision(Params);
	}
	else if (CommandType == TEXT("get_mesh_vertex_colors"))
	{
		return HandleGetMeshVertexColors(Params);
	}
	else if (CommandType == TEXT("delete_mesh_geometry_in_box"))
	{
		return HandleDeleteMeshGeometryInBox(Params);
	}
	else if (CommandType == TEXT("add_socket_to_static_mesh"))
	{
		return HandleAddSocketToStaticMesh(Params);
	}
	else if (CommandType == TEXT("modify_static_mesh_socket"))
	{
		return HandleModifyStaticMeshSocket(Params);
	}
	else if (CommandType == TEXT("remove_static_mesh_socket"))
	{
		return HandleRemoveStaticMeshSocket(Params);
	}
	else if (CommandType == TEXT("capture_static_mesh_socket_preview"))
	{
		return HandleCaptureStaticMeshSocketPreview(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown static-mesh command: %s"), *CommandType));
}

// ============================================================================
// get_static_mesh_info
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleGetStaticMeshInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UStaticMesh* StaticMesh = LoadStaticMesh(Params, TEXT("asset_path"), Error);
	if (!StaticMesh) return Error;

	const bool bIncludeMaterials = !Params->HasField(TEXT("include_materials")) || Params->GetBoolField(TEXT("include_materials"));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), StaticMesh->GetPathName());
	Result->SetStringField(TEXT("asset_name"), StaticMesh->GetName());
	Result->SetNumberField(TEXT("num_lods"), StaticMesh->GetNumLODs());
	Result->SetNumberField(TEXT("num_source_models"), StaticMesh->GetNumSourceModels());
	Result->SetBoolField(TEXT("nanite_enabled"), StaticMesh->IsNaniteEnabled());

	// Bounds
	const FBox Bounds = StaticMesh->GetBoundingBox();
	TSharedPtr<FJsonObject> BoundsObj = MakeShareable(new FJsonObject);
	BoundsObj->SetObjectField(TEXT("min"), VectorToJson(Bounds.Min));
	BoundsObj->SetObjectField(TEXT("max"), VectorToJson(Bounds.Max));
	BoundsObj->SetObjectField(TEXT("extent"), VectorToJson(Bounds.GetExtent()));
	BoundsObj->SetObjectField(TEXT("center"), VectorToJson(Bounds.GetCenter()));
	Result->SetObjectField(TEXT("bounds"), BoundsObj);

	// Sockets
	const TArray<TObjectPtr<UStaticMeshSocket>>& Sockets = StaticMesh->Sockets;
	Result->SetNumberField(TEXT("num_sockets"), Sockets.Num());

	TArray<TSharedPtr<FJsonValue>> SocketsArray;
	for (const TObjectPtr<UStaticMeshSocket>& SocketPtr : Sockets)
	{
		if (!SocketPtr) continue;
		SocketsArray.Add(MakeShareable(new FJsonValueObject(SerializeStaticMeshSocket(SocketPtr))));
	}
	Result->SetArrayField(TEXT("sockets"), SocketsArray);

	// Material slots
	if (bIncludeMaterials)
	{
		const TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();
		Result->SetNumberField(TEXT("num_materials"), StaticMaterials.Num());

		TArray<TSharedPtr<FJsonValue>> MatsArray;
		for (int32 i = 0; i < StaticMaterials.Num(); ++i)
		{
			const FStaticMaterial& Slot = StaticMaterials[i];
			TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
			SlotObj->SetNumberField(TEXT("index"), i);
			SlotObj->SetStringField(TEXT("slot_name"), Slot.MaterialSlotName.ToString());
			SlotObj->SetStringField(TEXT("imported_slot_name"), Slot.ImportedMaterialSlotName.ToString());
			if (Slot.MaterialInterface)
			{
				SlotObj->SetStringField(TEXT("material"), Slot.MaterialInterface->GetPathName());
			}
			MatsArray.Add(MakeShareable(new FJsonValueObject(SlotObj)));
		}
		Result->SetArrayField(TEXT("materials"), MatsArray);
	}

	return Result;
}

// ============================================================================
// get_mesh_footprint  — XY extent of mesh geometry within an optional Z band.
// Reads the LOD0 position vertex buffer directly (no GeometryScript dependency),
// so it returns the true wall/floor footprint without the roof inflating the box.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleGetMeshFootprint(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UStaticMesh* StaticMesh = LoadStaticMesh(Params, TEXT("asset_path"), Error);
	if (!StaticMesh) return Error;

	FStaticMeshRenderData* RD = StaticMesh->GetRenderData();
	if (!RD || RD->LODResources.Num() == 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Mesh has no render data / LOD0 (Nanite-only meshes expose no CPU vertices here)"));
	}
	const FPositionVertexBuffer& Pos = RD->LODResources[0].VertexBuffers.PositionVertexBuffer;
	const uint32 NumVerts = Pos.GetNumVertices();

	const FBox Bounds = StaticMesh->GetBoundingBox();

	// Resolve the Z band. Priority: explicit z_min/z_max; else band_above_floor (from mesh min Z); else full range.
	double ZMin = Bounds.Min.Z;
	double ZMax = Bounds.Max.Z;
	if (Params->HasField(TEXT("z_min")) || Params->HasField(TEXT("z_max")))
	{
		if (Params->HasField(TEXT("z_min"))) ZMin = Params->GetNumberField(TEXT("z_min"));
		if (Params->HasField(TEXT("z_max"))) ZMax = Params->GetNumberField(TEXT("z_max"));
	}
	else if (Params->HasField(TEXT("band_above_floor")))
	{
		ZMin = Bounds.Min.Z;
		ZMax = Bounds.Min.Z + Params->GetNumberField(TEXT("band_above_floor"));
	}

	double MinX = TNumericLimits<double>::Max(), MinY = TNumericLimits<double>::Max();
	double MaxX = TNumericLimits<double>::Lowest(), MaxY = TNumericLimits<double>::Lowest();
	int32 Count = 0;
	for (uint32 i = 0; i < NumVerts; ++i)
	{
		const FVector3f P = Pos.VertexPosition(i);
		if ((double)P.Z >= ZMin && (double)P.Z <= ZMax)
		{
			MinX = FMath::Min(MinX, (double)P.X); MaxX = FMath::Max(MaxX, (double)P.X);
			MinY = FMath::Min(MinY, (double)P.Y); MaxY = FMath::Max(MaxY, (double)P.Y);
			++Count;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), StaticMesh->GetPathName());
	Result->SetNumberField(TEXT("total_vertices"), (double)NumVerts);

	TSharedPtr<FJsonObject> BoundsObj = MakeShareable(new FJsonObject);
	BoundsObj->SetObjectField(TEXT("min"), VectorToJson(Bounds.Min));
	BoundsObj->SetObjectField(TEXT("max"), VectorToJson(Bounds.Max));
	BoundsObj->SetObjectField(TEXT("center"), VectorToJson(Bounds.GetCenter()));
	Result->SetObjectField(TEXT("full_bounds"), BoundsObj);

	TSharedPtr<FJsonObject> FP = MakeShareable(new FJsonObject);
	FP->SetNumberField(TEXT("z_min"), ZMin);
	FP->SetNumberField(TEXT("z_max"), ZMax);
	FP->SetNumberField(TEXT("vertex_count"), Count);
	if (Count > 0)
	{
		FP->SetNumberField(TEXT("x_min"), MinX);
		FP->SetNumberField(TEXT("x_max"), MaxX);
		FP->SetNumberField(TEXT("y_min"), MinY);
		FP->SetNumberField(TEXT("y_max"), MaxY);
		FP->SetNumberField(TEXT("center_x"), (MinX + MaxX) * 0.5);
		FP->SetNumberField(TEXT("center_y"), (MinY + MaxY) * 0.5);
		FP->SetNumberField(TEXT("size_x"), MaxX - MinX);
		FP->SetNumberField(TEXT("size_y"), MaxY - MinY);
	}
	Result->SetObjectField(TEXT("footprint"), FP);
	return Result;
}

// ============================================================================
// get_mesh_collision_info — simple-collision primitive counts + trace flag +
// nav-collision presence. Diagnoses "this mesh won't carve navmesh" (0 simple
// hulls + ComplexAsSimple): Recast carves from SIMPLE collision only, so a baked
// shell with zero simple prims floods nav through its walls. Pairs with
// get_mesh_footprint as the mesh-audit duo.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleGetMeshCollisionInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UStaticMesh* StaticMesh = LoadStaticMesh(Params, TEXT("asset_path"), Error);
	if (!StaticMesh) return Error;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), StaticMesh->GetPathName());

	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
	if (!BodySetup)
	{
		// No body setup at all = no simple collision whatsoever.
		Result->SetBoolField(TEXT("has_body_setup"), false);
		Result->SetNumberField(TEXT("convex_count"), 0);
		Result->SetNumberField(TEXT("box_count"), 0);
		Result->SetNumberField(TEXT("sphere_count"), 0);
		Result->SetNumberField(TEXT("capsule_count"), 0);
		Result->SetNumberField(TEXT("simple_primitive_count"), 0);
		Result->SetBoolField(TEXT("has_simple_collision"), false);
		Result->SetStringField(TEXT("collision_trace_flag"), TEXT("None"));
		Result->SetBoolField(TEXT("nav_collision_present"), false);
		Result->SetBoolField(TEXT("carves_navmesh"), false);
		return Result;
	}

	const FKAggregateGeom& Agg = BodySetup->AggGeom;
	const int32 ConvexCount = Agg.ConvexElems.Num();
	const int32 BoxCount = Agg.BoxElems.Num();
	const int32 SphereCount = Agg.SphereElems.Num();
	const int32 CapsuleCount = Agg.SphylElems.Num() + Agg.TaperedCapsuleElems.Num();
	const int32 SimpleCount = ConvexCount + BoxCount + SphereCount + CapsuleCount;

	Result->SetBoolField(TEXT("has_body_setup"), true);
	Result->SetNumberField(TEXT("convex_count"), ConvexCount);
	Result->SetNumberField(TEXT("box_count"), BoxCount);
	Result->SetNumberField(TEXT("sphere_count"), SphereCount);
	Result->SetNumberField(TEXT("capsule_count"), CapsuleCount);
	Result->SetNumberField(TEXT("simple_primitive_count"), SimpleCount);
	Result->SetBoolField(TEXT("has_simple_collision"), SimpleCount > 0);

	const ECollisionTraceFlag Flag = BodySetup->GetCollisionTraceFlag().GetValue();
	FString FlagStr;
	switch (Flag)
	{
		case CTF_UseDefault:          FlagStr = TEXT("UseDefault"); break;
		case CTF_UseSimpleAndComplex: FlagStr = TEXT("SimpleAndComplex"); break;
		case CTF_UseSimpleAsComplex:  FlagStr = TEXT("SimpleAsComplex"); break;
		case CTF_UseComplexAsSimple:  FlagStr = TEXT("ComplexAsSimple"); break;
		default:                      FlagStr = TEXT("Unknown"); break;
	}
	Result->SetStringField(TEXT("collision_trace_flag"), FlagStr);

	const UNavCollisionBase* NavCollision = StaticMesh->GetNavCollision();
	Result->SetBoolField(TEXT("nav_collision_present"), NavCollision != nullptr);

	// Recast carves the navmesh from SIMPLE collision. ComplexAsSimple with no simple
	// prims still blocks pawn line-traces but does NOT carve nav → floods through walls.
	const bool bCarvesNav = SimpleCount > 0 || Flag == CTF_UseSimpleAsComplex;
	Result->SetBoolField(TEXT("carves_navmesh"), bCarvesNav);

	return Result;
}

// ============================================================================
// generate_convex_collision — run V-HACD convex decomposition and write the
// result as the mesh's SIMPLE collision (the fix for baked/merged building
// shells that ship with zero simple hulls). Returns the FINAL hull count read
// back from the body setup so the caller can verify it stuck.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleGenerateConvexCollision(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UStaticMesh* StaticMesh = LoadStaticMesh(Params, TEXT("asset_path"), Error);
	if (!StaticMesh) return Error;

	const int32 HullCount = Params->HasField(TEXT("hull_count"))
		? static_cast<int32>(Params->GetNumberField(TEXT("hull_count"))) : 144;
	const int32 MaxHullVerts = Params->HasField(TEXT("max_hull_verts"))
		? static_cast<int32>(Params->GetNumberField(TEXT("max_hull_verts"))) : 32;
	const int32 Precision = Params->HasField(TEXT("precision"))
		? static_cast<int32>(Params->GetNumberField(TEXT("precision"))) : 1000000;

	UStaticMeshEditorSubsystem* SMES = GEditor ? GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() : nullptr;
	if (!SMES)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("StaticMeshEditorSubsystem unavailable"));
	}

	// bApplyChanges=true → rebuilds the body setup synchronously.
	const bool bOk = SMES->SetConvexDecompositionCollisions(StaticMesh, HullCount, MaxHullVerts, Precision);

	StaticMesh->MarkPackageDirty();

	// Read the real result back from the body setup, never trust the dispatch.
	int32 FinalHulls = 0;
	if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
	{
		FinalHulls = BodySetup->AggGeom.ConvexElems.Num();
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bOk && FinalHulls > 0);
	Result->SetStringField(TEXT("asset_path"), StaticMesh->GetPathName());
	Result->SetNumberField(TEXT("requested_hull_count"), HullCount);
	Result->SetNumberField(TEXT("max_hull_verts"), MaxHullVerts);
	Result->SetNumberField(TEXT("precision"), Precision);
	Result->SetNumberField(TEXT("final_hull_count"), FinalHulls);
	Result->SetBoolField(TEXT("note_save_required"), true);
	return Result;
}

// ============================================================================
// get_mesh_vertex_colors — per-channel stats of the LOD0 vertex colors within an
// optional Z band. Reads the render ColorVertexBuffer directly (same approach as
// get_mesh_footprint), so it is CRASH-FREE — unlike has_vertex_colors(), which
// hard-crashes the editor. This is exactly what a vertex-color-reading material
// (e.g. the building-hider floor/quadrant tags) sees at render time.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleGetMeshVertexColors(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UStaticMesh* StaticMesh = LoadStaticMesh(Params, TEXT("asset_path"), Error);
	if (!StaticMesh) return Error;

	FStaticMeshRenderData* RD = StaticMesh->GetRenderData();
	if (!RD || RD->LODResources.Num() == 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Mesh has no render data / LOD0 (Nanite-only meshes expose no CPU vertices here)"));
	}

	const FStaticMeshVertexBuffers& VB = RD->LODResources[0].VertexBuffers;
	const FPositionVertexBuffer& Pos = VB.PositionVertexBuffer;
	const FColorVertexBuffer& Col = VB.ColorVertexBuffer;
	const uint32 NumVerts = Pos.GetNumVertices();
	const uint32 NumColors = Col.GetNumVertices();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), StaticMesh->GetPathName());
	Result->SetNumberField(TEXT("total_vertices"), (double)NumVerts);

	// A mesh with no authored vertex colors has an empty color buffer (rendered as white).
	const bool bHasColors = NumColors > 0 && NumColors == NumVerts;
	Result->SetBoolField(TEXT("has_vertex_colors"), bHasColors);
	if (!bHasColors)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("No per-vertex colors (empty ColorVertexBuffer → renders as flat white 1,1,1,1)"));
		return Result;
	}

	const FBox Bounds = StaticMesh->GetBoundingBox();
	double ZMin = Bounds.Min.Z, ZMax = Bounds.Max.Z;
	if (Params->HasField(TEXT("z_min")) || Params->HasField(TEXT("z_max")))
	{
		if (Params->HasField(TEXT("z_min"))) ZMin = Params->GetNumberField(TEXT("z_min"));
		if (Params->HasField(TEXT("z_max"))) ZMax = Params->GetNumberField(TEXT("z_max"));
	}
	else if (Params->HasField(TEXT("band_above_floor")))
	{
		ZMin = Bounds.Min.Z;
		ZMax = Bounds.Min.Z + Params->GetNumberField(TEXT("band_above_floor"));
	}

	double MinR = 1.0, MinG = 1.0, MinB = 1.0, MinA = 1.0;
	double MaxR = 0.0, MaxG = 0.0, MaxB = 0.0, MaxA = 0.0;
	double SumR = 0.0, SumG = 0.0, SumB = 0.0, SumA = 0.0;
	int32 Count = 0;
	for (uint32 i = 0; i < NumVerts; ++i)
	{
		const FVector3f P = Pos.VertexPosition(i);
		if ((double)P.Z < ZMin || (double)P.Z > ZMax) continue;

		const FLinearColor C = Col.VertexColor(i).ReinterpretAsLinear(); // FColor 0-255 → 0-1, no sRGB
		MinR = FMath::Min(MinR, (double)C.R); MaxR = FMath::Max(MaxR, (double)C.R); SumR += C.R;
		MinG = FMath::Min(MinG, (double)C.G); MaxG = FMath::Max(MaxG, (double)C.G); SumG += C.G;
		MinB = FMath::Min(MinB, (double)C.B); MaxB = FMath::Max(MaxB, (double)C.B); SumB += C.B;
		MinA = FMath::Min(MinA, (double)C.A); MaxA = FMath::Max(MaxA, (double)C.A); SumA += C.A;
		++Count;
	}

	Result->SetNumberField(TEXT("z_min"), ZMin);
	Result->SetNumberField(TEXT("z_max"), ZMax);
	Result->SetNumberField(TEXT("sampled_vertices"), Count);

	auto Channel = [](double MinV, double MaxV, double Sum, int32 N) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetNumberField(TEXT("min"), N > 0 ? MinV : 0.0);
		Obj->SetNumberField(TEXT("max"), N > 0 ? MaxV : 0.0);
		Obj->SetNumberField(TEXT("mean"), N > 0 ? Sum / N : 0.0);
		return Obj;
	};
	if (Count > 0)
	{
		Result->SetObjectField(TEXT("r"), Channel(MinR, MaxR, SumR, Count));
		Result->SetObjectField(TEXT("g"), Channel(MinG, MaxG, SumG, Count));
		Result->SetObjectField(TEXT("b"), Channel(MinB, MaxB, SumB, Count));
		Result->SetObjectField(TEXT("a"), Channel(MinA, MaxA, SumA, Count));
	}
	return Result;
}

// ============================================================================
// delete_mesh_geometry_in_box — read a static mesh into a DynamicMesh in C++
// (CopyMeshFromStaticMesh — the reader that is NOT reachable from run_python),
// delete the triangles inside (or outside) a world-local box, and write the
// result back IN PLACE to a duplicated asset (preserving materials + build
// settings, the failure mode of create_new_static_mesh_asset_from_mesh). Used to
// author ruined/torn mesh variants. recompute_normals fixes shading on the new
// boundary; UV0 presence is reported (the source's UVs ride along on the kept
// triangles, sidestepping the NumUVs>0 bake assert).
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleDeleteMeshGeometryInBox(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UE::Geometry;

	TSharedPtr<FJsonObject> Error;
	UStaticMesh* SourceMesh = LoadStaticMesh(Params, TEXT("source_asset"), Error);
	if (!SourceMesh) return Error;

	auto ReadVec = [&](const TCHAR* Field, FVector& Out) -> bool
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Params->TryGetArrayField(Field, Arr) || !Arr || Arr->Num() < 3) return false;
		Out = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
		return true;
	};
	FVector BoxMin, BoxMax;
	if (!ReadVec(TEXT("box_min"), BoxMin) || !ReadVec(TEXT("box_max"), BoxMax))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing/invalid box_min or box_max (each must be [x,y,z])"));
	}
	const FBox Box(BoxMin.ComponentMin(BoxMax), BoxMin.ComponentMax(BoxMax));

	// keep = "outside" (default): delete triangles whose centroid is INSIDE the box (cut a hole).
	// keep = "inside": delete triangles whose centroid is OUTSIDE the box (keep only the region).
	FString Keep = Params->HasField(TEXT("keep")) ? Params->GetStringField(TEXT("keep")) : TEXT("outside");
	const bool bKeepInside = Keep.Equals(TEXT("inside"), ESearchCase::IgnoreCase);
	const bool bRecomputeNormals = !Params->HasField(TEXT("recompute_normals")) || Params->GetBoolField(TEXT("recompute_normals"));
	const bool bSave = Params->HasField(TEXT("save")) && Params->GetBoolField(TEXT("save"));

	// Resolve destination — in place unless a distinct dest_asset is given.
	FString DestPath = Params->HasField(TEXT("dest_asset")) ? Params->GetStringField(TEXT("dest_asset")) : FString();
	UStaticMesh* DestMesh = SourceMesh;
	if (!DestPath.IsEmpty() && DestPath != SourceMesh->GetPathName())
	{
		if (UEditorAssetLibrary::DoesAssetExist(DestPath))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("dest_asset already exists: %s"), *DestPath));
		}
		UObject* Dup = UEditorAssetLibrary::DuplicateAsset(SourceMesh->GetPathName(), DestPath);
		DestMesh = Cast<UStaticMesh>(Dup);
		if (!DestMesh)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to duplicate to dest_asset: %s"), *DestPath));
		}
	}

	// Read source geometry into a DynamicMesh (the run_python-unreachable step).
	UDynamicMesh* Dyn = NewObject<UDynamicMesh>();
	Dyn->AddToRoot();
	ON_SCOPE_EXIT { Dyn->RemoveFromRoot(); };

	FGeometryScriptCopyMeshFromAssetOptions FromOpts; // defaults: apply build settings, request tangents
	FGeometryScriptMeshReadLOD ReadLOD;               // MaxAvailable SourceModel, index 0
	EGeometryScriptOutcomePins FromOutcome = EGeometryScriptOutcomePins::Failure;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMesh(SourceMesh, Dyn, FromOpts, ReadLOD, FromOutcome);
	if (FromOutcome != EGeometryScriptOutcomePins::Success)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("CopyMeshFromStaticMesh failed (no readable SourceModel LOD0?)"));
	}

	int32 TrisBefore = 0, TrisDeleted = 0, TrisAfter = 0;
	bool bUV0Present = false;
	Dyn->EditMesh([&](FDynamicMesh3& Mesh)
	{
		TrisBefore = Mesh.TriangleCount();
		TArray<int32> ToDelete;
		ToDelete.Reserve(TrisBefore);
		for (int32 tid : Mesh.TriangleIndicesItr())
		{
			const FVector3d C = Mesh.GetTriCentroid(tid);
			const bool bInside = Box.IsInsideOrOn(FVector(C));
			// Delete when the triangle is on the side we are NOT keeping.
			if (bInside != bKeepInside)
			{
				ToDelete.Add(tid);
			}
		}
		for (int32 tid : ToDelete)
		{
			if (Mesh.RemoveTriangle(tid) == EMeshResult::Ok)
			{
				++TrisDeleted;
			}
		}
		TrisAfter = Mesh.TriangleCount();
		bUV0Present = Mesh.HasAttributes() && Mesh.Attributes()->PrimaryUV() != nullptr
			&& Mesh.Attributes()->PrimaryUV()->ElementCount() > 0;
	});

	if (TrisAfter == 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Edit would delete ALL triangles — aborting (check box bounds / keep direction)"));
	}

	// Write back in place — preserve the asset's materials (bReplaceMaterials=false).
	FGeometryScriptCopyMeshToAssetOptions ToOpts;
	ToOpts.bEnableRecomputeNormals = bRecomputeNormals;
	ToOpts.bReplaceMaterials = false;
	ToOpts.bEmitTransaction = true;
	FGeometryScriptMeshWriteLOD WriteLOD; // LODIndex 0
	EGeometryScriptOutcomePins ToOutcome = EGeometryScriptOutcomePins::Failure;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(Dyn, DestMesh, ToOpts, WriteLOD, ToOutcome);
	if (ToOutcome != EGeometryScriptOutcomePins::Success)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("CopyMeshToStaticMesh failed"));
	}

	DestMesh->MarkPackageDirty();
	bool bSaved = false;
	if (bSave)
	{
		bSaved = UEditorAssetLibrary::SaveLoadedAsset(DestMesh, /*bOnlyIfIsDirty=*/false);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("source_asset"), SourceMesh->GetPathName());
	Result->SetStringField(TEXT("dest_asset"), DestMesh->GetPathName());
	Result->SetBoolField(TEXT("in_place"), DestMesh == SourceMesh);
	Result->SetStringField(TEXT("keep"), bKeepInside ? TEXT("inside") : TEXT("outside"));
	Result->SetNumberField(TEXT("triangles_before"), TrisBefore);
	Result->SetNumberField(TEXT("triangles_deleted"), TrisDeleted);
	Result->SetNumberField(TEXT("triangles_after"), TrisAfter);
	Result->SetBoolField(TEXT("uv0_present"), bUV0Present);
	Result->SetBoolField(TEXT("note_save_required"), !bSave);
	if (bSave)
	{
		Result->SetBoolField(TEXT("saved"), bSaved);
	}
	return Result;
}

// ============================================================================
// add_socket_to_static_mesh
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleAddSocketToStaticMesh(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UStaticMesh* StaticMesh = LoadStaticMesh(Params, TEXT("asset_path"), Error);
	if (!StaticMesh) return Error;

	if (!Params->HasField(TEXT("socket_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: socket_name"));

	const FString SocketName = Params->GetStringField(TEXT("socket_name"));

	if (StaticMesh->FindSocket(FName(*SocketName)))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Socket '%s' already exists on static mesh"), *SocketName));
	}

	UStaticMeshSocket* NewSocket = NewObject<UStaticMeshSocket>(StaticMesh);
	NewSocket->SocketName = FName(*SocketName);

	if (Params->HasField(TEXT("tag")))
	{
		NewSocket->Tag = Params->GetStringField(TEXT("tag"));
	}

	const TSharedPtr<FJsonObject>* LocPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocPtr) && LocPtr)
		NewSocket->RelativeLocation = JsonToVector(*LocPtr);

	const TSharedPtr<FJsonObject>* RotPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotPtr) && RotPtr)
		NewSocket->RelativeRotation = JsonToRotator(*RotPtr);

	const TSharedPtr<FJsonObject>* ScalePtr = nullptr;
	if (Params->TryGetObjectField(TEXT("scale"), ScalePtr) && ScalePtr)
		NewSocket->RelativeScale = JsonToVector(*ScalePtr, FVector::OneVector);
	else
		NewSocket->RelativeScale = FVector::OneVector;

	StaticMesh->AddSocket(NewSocket);
	StaticMesh->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("socket_name"), SocketName);
	Result->SetStringField(TEXT("static_mesh"), StaticMesh->GetPathName());
	Result->SetObjectField(TEXT("socket"), SerializeStaticMeshSocket(NewSocket));
	return Result;
}

// ============================================================================
// modify_static_mesh_socket
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleModifyStaticMeshSocket(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UStaticMesh* StaticMesh = LoadStaticMesh(Params, TEXT("asset_path"), Error);
	if (!StaticMesh) return Error;

	if (!Params->HasField(TEXT("socket_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: socket_name"));

	const FString SocketName = Params->GetStringField(TEXT("socket_name"));
	UStaticMeshSocket* Socket = StaticMesh->FindSocket(FName(*SocketName));
	if (!Socket)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Socket '%s' not found on static mesh"), *SocketName));
	}

	if (Params->HasField(TEXT("tag")))
	{
		Socket->Tag = Params->GetStringField(TEXT("tag"));
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

	StaticMesh->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("socket_name"), SocketName);
	Result->SetObjectField(TEXT("socket"), SerializeStaticMeshSocket(Socket));
	return Result;
}

// ============================================================================
// remove_static_mesh_socket
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleRemoveStaticMeshSocket(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UStaticMesh* StaticMesh = LoadStaticMesh(Params, TEXT("asset_path"), Error);
	if (!StaticMesh) return Error;

	if (!Params->HasField(TEXT("socket_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: socket_name"));

	const FString SocketName = Params->GetStringField(TEXT("socket_name"));
	UStaticMeshSocket* Socket = StaticMesh->FindSocket(FName(*SocketName));
	if (!Socket)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Socket '%s' not found on static mesh"), *SocketName));
	}

	StaticMesh->RemoveSocket(Socket);
	StaticMesh->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_socket"), SocketName);
	Result->SetNumberField(TEXT("remaining_sockets"), StaticMesh->Sockets.Num());
	return Result;
}

// ============================================================================
// capture_static_mesh_socket_preview
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPStaticMeshCommands::HandleCaptureStaticMeshSocketPreview(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Error;
	UStaticMesh* BaseMesh = LoadStaticMesh(Params, TEXT("static_mesh_path"), Error);
	if (!BaseMesh) return Error;

	if (!Params->HasField(TEXT("socket_name")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: socket_name"));
	if (!Params->HasField(TEXT("mesh_path")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: mesh_path"));

	const FString SocketName = Params->GetStringField(TEXT("socket_name"));
	const FString PreviewMeshPath = Params->GetStringField(TEXT("mesh_path"));
	const int32 Resolution = Params->HasField(TEXT("resolution"))
		? static_cast<int32>(Params->GetNumberField(TEXT("resolution")))
		: 512;

	if (!BaseMesh->FindSocket(FName(*SocketName)))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Socket '%s' not found on base static mesh"), *SocketName));
	}

	UObject* PreviewAsset = FEpicUnrealMCPCommonUtils::ResolveAsset(PreviewMeshPath);
	if (!PreviewAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load preview mesh: %s"), *PreviewMeshPath));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world"));
	}

	const FVector SpawnOrigin(0.0f, 0.0f, 50000.0f);
	AActor* TempActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnOrigin, FRotator::ZeroRotator);
	TempActor->SetFlags(RF_Transient);

	USceneComponent* RootComp = NewObject<USceneComponent>(TempActor, TEXT("Root"));
	TempActor->SetRootComponent(RootComp);
	RootComp->RegisterComponent();

	UStaticMeshComponent* BaseComp = NewObject<UStaticMeshComponent>(TempActor, TEXT("Base"));
	BaseComp->SetupAttachment(RootComp);
	BaseComp->SetStaticMesh(BaseMesh);
	BaseComp->RegisterComponent();

	// Attach preview — static or skeletal, matching capture_socket_preview
	UStaticMesh* PreviewStaticMesh = Cast<UStaticMesh>(PreviewAsset);
	USkeletalMesh* PreviewSkelMesh = Cast<USkeletalMesh>(PreviewAsset);
	if (PreviewStaticMesh)
	{
		UStaticMeshComponent* SMComp = NewObject<UStaticMeshComponent>(TempActor, TEXT("Preview"));
		SMComp->SetupAttachment(BaseComp, FName(*SocketName));
		SMComp->SetStaticMesh(PreviewStaticMesh);
		SMComp->RegisterComponent();
	}
	else if (PreviewSkelMesh)
	{
		USkeletalMeshComponent* SKComp = NewObject<USkeletalMeshComponent>(TempActor, TEXT("Preview"));
		SKComp->SetupAttachment(BaseComp, FName(*SocketName));
		SKComp->SetSkeletalMeshAsset(PreviewSkelMesh);
		SKComp->RegisterComponent();
	}
	else
	{
		TempActor->Destroy();
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Preview asset is neither StaticMesh nor SkeletalMesh: %s"), *PreviewMeshPath));
	}

	const FBoxSphereBounds BaseBounds = BaseComp->CalcBounds(BaseComp->GetComponentTransform());
	FVector SocketLocation = SpawnOrigin;
	if (BaseComp->DoesSocketExist(FName(*SocketName)))
	{
		SocketLocation = BaseComp->GetSocketLocation(FName(*SocketName));
	}

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RenderTarget->InitCustomFormat(Resolution, Resolution, PF_B8G8R8A8, false);
	RenderTarget->ClearColor = FLinearColor(0.15f, 0.15f, 0.15f, 1.0f);
	RenderTarget->UpdateResourceImmediate(true);

	USceneCaptureComponent2D* CaptureComp = NewObject<USceneCaptureComponent2D>(TempActor, TEXT("Capture"));
	CaptureComp->SetupAttachment(RootComp);
	CaptureComp->TextureTarget = RenderTarget;
	CaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	CaptureComp->bCaptureEveryFrame = false;
	CaptureComp->bCaptureOnMovement = false;
	CaptureComp->RegisterComponent();

	const float CamDist = FMath::Max(BaseBounds.SphereRadius, 10.0f) * 1.5f;
	const FVector CamOffset = FVector(-CamDist * 0.7f, CamDist * 0.5f, CamDist * 0.3f);
	CaptureComp->SetWorldLocationAndRotation(
		SocketLocation + CamOffset,
		(SocketLocation - (SocketLocation + CamOffset)).Rotation()
	);

	CaptureComp->CaptureScene();
	FlushRenderingCommands();

	TArray<FColor> Pixels;
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (RTResource)
	{
		RTResource->ReadPixels(Pixels);
	}

	const FString OutputDir = FPaths::ProjectSavedDir() / TEXT("MCP_Previews");
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	const FString FileName = FString::Printf(TEXT("sm_socket_preview_%s_%s.png"), *SocketName, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	const FString FullPath = OutputDir / FileName;

	if (Pixels.Num() > 0)
	{
		TArray64<uint8> PNGData;
		FImageUtils::PNGCompressImageArray(Resolution, Resolution, Pixels, PNGData);
		FFileHelper::SaveArrayToFile(PNGData, *FullPath);
	}

	TempActor->Destroy();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Pixels.Num() > 0);
	Result->SetStringField(TEXT("saved_path"), FullPath);
	Result->SetStringField(TEXT("socket_name"), SocketName);
	Result->SetStringField(TEXT("static_mesh_path"), BaseMesh->GetPathName());
	Result->SetStringField(TEXT("mesh_path"), PreviewMeshPath);
	Result->SetNumberField(TEXT("resolution"), Resolution);
	return Result;
}
