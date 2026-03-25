#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureEnum.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "ImageUtils.h"
#include "HighResScreenshot.h"
#include "Engine/GameViewportClient.h"
#include "Misc/FileHelper.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EditorAssetLibrary.h"
#include "Commands/EpicUnrealMCPBlueprintCommands.h"

FEpicUnrealMCPEditorCommands::FEpicUnrealMCPEditorCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    // Actor manipulation commands
    if (CommandType == TEXT("get_actors_in_level"))
    {
        return HandleGetActorsInLevel(Params);
    }
    else if (CommandType == TEXT("find_actors_by_name"))
    {
        return HandleFindActorsByName(Params);
    }
    else if (CommandType == TEXT("spawn_actor"))
    {
        return HandleSpawnActor(Params);
    }
    else if (CommandType == TEXT("delete_actor"))
    {
        return HandleDeleteActor(Params);
    }
    else if (CommandType == TEXT("set_actor_transform"))
    {
        return HandleSetActorTransform(Params);
    }
    // Blueprint actor spawning
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    // Level analysis commands
    else if (CommandType == TEXT("get_rvt_volumes"))
    {
        return HandleGetRVTVolumes(Params);
    }
    else if (CommandType == TEXT("get_landscape_info"))
    {
        return HandleGetLandscapeInfo(Params);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown editor command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> ActorArray;
    for (AActor* Actor : AllActors)
    {
        if (Actor)
        {
            ActorArray.Add(FEpicUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), ActorArray);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params)
{
    FString Pattern;
    if (!Params->TryGetStringField(TEXT("pattern"), Pattern))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pattern' parameter"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> MatchingActors;
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName().Contains(Pattern))
        {
            MatchingActors.Add(FEpicUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), MatchingActors);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorType;
    if (!Params->TryGetStringField(TEXT("type"), ActorType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    // Get actor name (required parameter)
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Get optional transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);
    FVector Scale(1.0f, 1.0f, 1.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    // Create the actor based on type
    AActor* NewActor = nullptr;
    UWorld* World = GEditor->GetEditorWorldContext().World();

    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    // Check if an actor with this name already exists
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor with name '%s' already exists"), *ActorName));
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;

    if (ActorType == TEXT("StaticMeshActor"))
    {
        AStaticMeshActor* NewMeshActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
        if (NewMeshActor)
        {
            // Check for an optional static_mesh parameter to assign a mesh
            FString MeshPath;
            if (Params->TryGetStringField(TEXT("static_mesh"), MeshPath))
            {
                UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
                if (Mesh)
                {
                    NewMeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("Could not find static mesh at path: %s"), *MeshPath);
                }
            }
        }
        NewActor = NewMeshActor;
    }
    else if (ActorType == TEXT("PointLight"))
    {
        NewActor = World->SpawnActor<APointLight>(APointLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("SpotLight"))
    {
        NewActor = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("DirectionalLight"))
    {
        NewActor = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("CameraActor"))
    {
        NewActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Location, Rotation, SpawnParams);
    }
    else
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown actor type: %s"), *ActorType));
    }

    if (NewActor)
    {
        // Set scale (since SpawnActor only takes location and rotation)
        FTransform Transform = NewActor->GetTransform();
        Transform.SetScale3D(Scale);
        NewActor->SetActorTransform(Transform);

        // Return the created actor's details
        return FEpicUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create actor"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            // Store actor info before deletion for the response
            TSharedPtr<FJsonObject> ActorInfo = FEpicUnrealMCPCommonUtils::ActorToJsonObject(Actor);
            
            // Delete the actor
            Actor->Destroy();
            
            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetObjectField(TEXT("deleted_actor"), ActorInfo);
            return ResultObj;
        }
    }
    
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get transform parameters
    FTransform NewTransform = TargetActor->GetTransform();

    if (Params->HasField(TEXT("location")))
    {
        NewTransform.SetLocation(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        NewTransform.SetRotation(FQuat(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
    }
    if (Params->HasField(TEXT("scale")))
    {
        NewTransform.SetScale3D(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
    }

    // Set the new transform
    TargetActor->SetActorTransform(NewTransform);

    // Return updated actor info
    return FEpicUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    // This function will now correctly call the implementation in BlueprintCommands
    FEpicUnrealMCPBlueprintCommands BlueprintCommands;
    return BlueprintCommands.HandleCommand(TEXT("spawn_blueprint_actor"), Params);
}

// ============================================================================
// get_rvt_volumes — Return all RuntimeVirtualTextureVolume actors in the level
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetRVTVolumes(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world available"));
    }

    TArray<TSharedPtr<FJsonValue>> VolumesArray;

    for (TActorIterator<ARuntimeVirtualTextureVolume> It(World); It; ++It)
    {
        ARuntimeVirtualTextureVolume* Volume = *It;
        if (!Volume)
        {
            continue;
        }

        TSharedPtr<FJsonObject> VolumeJson = MakeShareable(new FJsonObject);
        VolumeJson->SetStringField(TEXT("name"), Volume->GetName());
        VolumeJson->SetStringField(TEXT("label"), Volume->GetActorLabel());

        // Transform
        FVector Location = Volume->GetActorLocation();
        TArray<TSharedPtr<FJsonValue>> LocArray;
        LocArray.Add(MakeShareable(new FJsonValueNumber(Location.X)));
        LocArray.Add(MakeShareable(new FJsonValueNumber(Location.Y)));
        LocArray.Add(MakeShareable(new FJsonValueNumber(Location.Z)));
        VolumeJson->SetArrayField(TEXT("location"), LocArray);

        FVector Scale = Volume->GetActorScale3D();
        TArray<TSharedPtr<FJsonValue>> ScaleArray;
        ScaleArray.Add(MakeShareable(new FJsonValueNumber(Scale.X)));
        ScaleArray.Add(MakeShareable(new FJsonValueNumber(Scale.Y)));
        ScaleArray.Add(MakeShareable(new FJsonValueNumber(Scale.Z)));
        VolumeJson->SetArrayField(TEXT("scale"), ScaleArray);

        FRotator Rotation = Volume->GetActorRotation();
        TArray<TSharedPtr<FJsonValue>> RotArray;
        RotArray.Add(MakeShareable(new FJsonValueNumber(Rotation.Pitch)));
        RotArray.Add(MakeShareable(new FJsonValueNumber(Rotation.Yaw)));
        RotArray.Add(MakeShareable(new FJsonValueNumber(Rotation.Roll)));
        VolumeJson->SetArrayField(TEXT("rotation"), RotArray);

        // Bounds
        FBox Bounds = Volume->GetComponentsBoundingBox();
        TArray<TSharedPtr<FJsonValue>> BoundsMinArr;
        BoundsMinArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Min.X)));
        BoundsMinArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Min.Y)));
        BoundsMinArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Min.Z)));
        VolumeJson->SetArrayField(TEXT("bounds_min"), BoundsMinArr);

        TArray<TSharedPtr<FJsonValue>> BoundsMaxArr;
        BoundsMaxArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Max.X)));
        BoundsMaxArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Max.Y)));
        BoundsMaxArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Max.Z)));
        VolumeJson->SetArrayField(TEXT("bounds_max"), BoundsMaxArr);

        // VirtualTextureComponent details
        URuntimeVirtualTextureComponent* VTComp = Volume->VirtualTextureComponent;
        if (VTComp)
        {
            TSharedPtr<FJsonObject> CompJson = MakeShareable(new FJsonObject);

            CompJson->SetBoolField(TEXT("hide_primitives"), VTComp->IsScalable()); // expose bHidePrimitives via reflection
            CompJson->SetNumberField(TEXT("stream_low_mips"), VTComp->NumStreamingMips());

            // Read bHidePrimitives via reflection
            FProperty* HidePrimProp = FindFProperty<FProperty>(VTComp->GetClass(), TEXT("bHidePrimitives"));
            if (HidePrimProp)
            {
                bool bHidePrimitives = false;
                HidePrimProp->GetValue_InContainer(VTComp, &bHidePrimitives);
                CompJson->SetBoolField(TEXT("hide_primitives"), bHidePrimitives);
            }

            // SnapBoundsToLandscape
            FProperty* SnapProp = FindFProperty<FProperty>(VTComp->GetClass(), TEXT("bSnapBoundsToLandscape"));
            if (SnapProp)
            {
                bool bSnap = false;
                SnapProp->GetValue_InContainer(VTComp, &bSnap);
                CompJson->SetBoolField(TEXT("snap_bounds_to_landscape"), bSnap);
            }

            // BoundsAlignActor
            FProperty* BoundsAlignProp = FindFProperty<FProperty>(VTComp->GetClass(), TEXT("BoundsAlignActor"));
            if (BoundsAlignProp)
            {
                TSoftObjectPtr<AActor>* AlignActor = BoundsAlignProp->ContainerPtrToValuePtr<TSoftObjectPtr<AActor>>(VTComp);
                if (AlignActor && !AlignActor->IsNull())
                {
                    CompJson->SetStringField(TEXT("bounds_align_actor"), AlignActor->ToString());
                }
            }

            // ExpandBounds
            FProperty* ExpandProp = FindFProperty<FProperty>(VTComp->GetClass(), TEXT("ExpandBounds"));
            if (ExpandProp)
            {
                float ExpandVal = 0.f;
                ExpandProp->GetValue_InContainer(VTComp, &ExpandVal);
                CompJson->SetNumberField(TEXT("expand_bounds"), ExpandVal);
            }

            // The RVT asset
            URuntimeVirtualTexture* RVT = VTComp->GetVirtualTexture();
            if (RVT)
            {
                TSharedPtr<FJsonObject> RVTJson = MakeShareable(new FJsonObject);
                RVTJson->SetStringField(TEXT("asset_path"), RVT->GetPathName());
                RVTJson->SetStringField(TEXT("asset_name"), RVT->GetName());

                // Material type
                static const TMap<ERuntimeVirtualTextureMaterialType, FString> MatTypeNames = {
                    {ERuntimeVirtualTextureMaterialType::BaseColor, TEXT("BaseColor")},
                    {ERuntimeVirtualTextureMaterialType::Mask4, TEXT("Mask4")},
                    {ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Roughness, TEXT("BaseColor_Normal_Roughness")},
                    {ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular, TEXT("BaseColor_Normal_Specular")},
                    {ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg, TEXT("BaseColor_Normal_Specular_YCoCg")},
                    {ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg, TEXT("BaseColor_Normal_Specular_Mask_YCoCg")},
                    {ERuntimeVirtualTextureMaterialType::WorldHeight, TEXT("WorldHeight")},
                    {ERuntimeVirtualTextureMaterialType::Displacement, TEXT("Displacement")},
                };
                const FString* TypeName = MatTypeNames.Find(RVT->GetMaterialType());
                RVTJson->SetStringField(TEXT("material_type"), TypeName ? *TypeName : TEXT("Unknown"));

                RVTJson->SetNumberField(TEXT("tile_count"), RVT->GetTileCount());
                RVTJson->SetNumberField(TEXT("tile_size"), RVT->GetTileSize());
                RVTJson->SetNumberField(TEXT("tile_border_size"), RVT->GetTileBorderSize());
                RVTJson->SetNumberField(TEXT("size"), RVT->GetSize());
                RVTJson->SetNumberField(TEXT("layer_count"), RVT->GetLayerCount());
                RVTJson->SetBoolField(TEXT("compress_textures"), RVT->GetCompressTextures());
                RVTJson->SetBoolField(TEXT("adaptive"), RVT->GetAdaptivePageTable());
                RVTJson->SetBoolField(TEXT("continuous_update"), RVT->GetContinuousUpdate());

                CompJson->SetObjectField(TEXT("virtual_texture"), RVTJson);
            }

            VolumeJson->SetObjectField(TEXT("component"), CompJson);
        }

        VolumesArray.Add(MakeShareable(new FJsonValueObject(VolumeJson)));
    }

    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("count"), VolumesArray.Num());
    Result->SetArrayField(TEXT("volumes"), VolumesArray);
    return Result;
}

// ============================================================================
// get_landscape_info — Return all Landscape/LandscapeProxy actors in the level
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetLandscapeInfo(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world available"));
    }

    TArray<TSharedPtr<FJsonValue>> LandscapesArray;

    for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (!Proxy)
        {
            continue;
        }

        TSharedPtr<FJsonObject> LandJson = MakeShareable(new FJsonObject);
        LandJson->SetStringField(TEXT("name"), Proxy->GetName());
        LandJson->SetStringField(TEXT("label"), Proxy->GetActorLabel());
        LandJson->SetStringField(TEXT("class"), Proxy->GetClass()->GetName());

        // Transform
        FVector Location = Proxy->GetActorLocation();
        TArray<TSharedPtr<FJsonValue>> LocArray;
        LocArray.Add(MakeShareable(new FJsonValueNumber(Location.X)));
        LocArray.Add(MakeShareable(new FJsonValueNumber(Location.Y)));
        LocArray.Add(MakeShareable(new FJsonValueNumber(Location.Z)));
        LandJson->SetArrayField(TEXT("location"), LocArray);

        // Bounds
        FBox Bounds = Proxy->GetComponentsBoundingBox();
        TArray<TSharedPtr<FJsonValue>> BoundsMinArr;
        BoundsMinArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Min.X)));
        BoundsMinArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Min.Y)));
        BoundsMinArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Min.Z)));
        LandJson->SetArrayField(TEXT("bounds_min"), BoundsMinArr);

        TArray<TSharedPtr<FJsonValue>> BoundsMaxArr;
        BoundsMaxArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Max.X)));
        BoundsMaxArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Max.Y)));
        BoundsMaxArr.Add(MakeShareable(new FJsonValueNumber(Bounds.Max.Z)));
        LandJson->SetArrayField(TEXT("bounds_max"), BoundsMaxArr);

        // Grid dimensions
        LandJson->SetNumberField(TEXT("component_size_quads"), Proxy->ComponentSizeQuads);
        LandJson->SetNumberField(TEXT("subsection_size_quads"), Proxy->SubsectionSizeQuads);
        LandJson->SetNumberField(TEXT("num_subsections"), Proxy->NumSubsections);
        LandJson->SetNumberField(TEXT("num_landscape_components"), Proxy->LandscapeComponents.Num());
        LandJson->SetNumberField(TEXT("num_collision_components"), Proxy->CollisionComponents.Num());

        // LOD settings
        LandJson->SetNumberField(TEXT("max_lod_level"), Proxy->MaxLODLevel);
        LandJson->SetNumberField(TEXT("lod0_screen_size"), Proxy->LOD0ScreenSize);
        LandJson->SetNumberField(TEXT("lod0_distribution_setting"), Proxy->LOD0DistributionSetting);
        LandJson->SetNumberField(TEXT("lod_distribution_setting"), Proxy->LODDistributionSetting);
        LandJson->SetNumberField(TEXT("lod_blend_range"), Proxy->LODBlendRange);

        // Nanite
        LandJson->SetBoolField(TEXT("nanite_enabled"), Proxy->IsNaniteEnabled());
        LandJson->SetNumberField(TEXT("num_nanite_components"), Proxy->NaniteComponents.Num());

        // Material
        if (Proxy->LandscapeMaterial)
        {
            LandJson->SetStringField(TEXT("landscape_material"), Proxy->LandscapeMaterial->GetPathName());
            LandJson->SetStringField(TEXT("landscape_material_name"), Proxy->LandscapeMaterial->GetName());
        }
        if (Proxy->LandscapeHoleMaterial)
        {
            LandJson->SetStringField(TEXT("landscape_hole_material"), Proxy->LandscapeHoleMaterial->GetPathName());
        }

        // Virtual Texture settings
        LandJson->SetNumberField(TEXT("virtual_texture_num_lods"), Proxy->VirtualTextureNumLods);
        LandJson->SetNumberField(TEXT("virtual_texture_lod_bias"), Proxy->VirtualTextureLodBias);
        LandJson->SetBoolField(TEXT("virtual_texture_render_with_quad"), Proxy->bVirtualTextureRenderWithQuad);
        LandJson->SetBoolField(TEXT("virtual_texture_render_with_quad_hq"), Proxy->bVirtualTextureRenderWithQuadHQ);

        // Main pass type
        static const TMap<ERuntimeVirtualTextureMainPassType, FString> MainPassNames = {
            {ERuntimeVirtualTextureMainPassType::Never, TEXT("Never")},
            {ERuntimeVirtualTextureMainPassType::Exclusive, TEXT("FromVirtualTexture")},
            {ERuntimeVirtualTextureMainPassType::Always, TEXT("Always")},
        };
        const FString* MainPassName = MainPassNames.Find(Proxy->VirtualTextureRenderPassType);
        LandJson->SetStringField(TEXT("virtual_texture_main_pass"), MainPassName ? *MainPassName : TEXT("Always"));

        // RuntimeVirtualTextures array
        TArray<TSharedPtr<FJsonValue>> RVTArray;
        for (const TObjectPtr<URuntimeVirtualTexture>& RVT : Proxy->RuntimeVirtualTextures)
        {
            if (RVT)
            {
                TSharedPtr<FJsonObject> RVTJson = MakeShareable(new FJsonObject);
                RVTJson->SetStringField(TEXT("asset_path"), RVT->GetPathName());
                RVTJson->SetStringField(TEXT("asset_name"), RVT->GetName());

                static const TMap<ERuntimeVirtualTextureMaterialType, FString> MatTypeNames = {
                    {ERuntimeVirtualTextureMaterialType::BaseColor, TEXT("BaseColor")},
                    {ERuntimeVirtualTextureMaterialType::Mask4, TEXT("Mask4")},
                    {ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Roughness, TEXT("BaseColor_Normal_Roughness")},
                    {ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular, TEXT("BaseColor_Normal_Specular")},
                    {ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg, TEXT("BaseColor_Normal_Specular_YCoCg")},
                    {ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg, TEXT("BaseColor_Normal_Specular_Mask_YCoCg")},
                    {ERuntimeVirtualTextureMaterialType::WorldHeight, TEXT("WorldHeight")},
                    {ERuntimeVirtualTextureMaterialType::Displacement, TEXT("Displacement")},
                };
                const FString* TypeName = MatTypeNames.Find(RVT->GetMaterialType());
                RVTJson->SetStringField(TEXT("material_type"), TypeName ? *TypeName : TEXT("Unknown"));
                RVTJson->SetNumberField(TEXT("size"), RVT->GetSize());
                RVTJson->SetNumberField(TEXT("layer_count"), RVT->GetLayerCount());

                RVTArray.Add(MakeShareable(new FJsonValueObject(RVTJson)));
            }
            else
            {
                RVTArray.Add(MakeShareable(new FJsonValueNull()));
            }
        }
        LandJson->SetArrayField(TEXT("runtime_virtual_textures"), RVTArray);

        // Bounds extension
        LandJson->SetNumberField(TEXT("negative_z_bounds_extension"), Proxy->NegativeZBoundsExtension);
        LandJson->SetNumberField(TEXT("positive_z_bounds_extension"), Proxy->PositiveZBoundsExtension);

        // Streaming
        LandJson->SetNumberField(TEXT("streaming_distance_multiplier"), Proxy->StreamingDistanceMultiplier);

        // Physical material
        if (Proxy->DefaultPhysMaterial)
        {
            LandJson->SetStringField(TEXT("default_phys_material"), Proxy->DefaultPhysMaterial->GetPathName());
        }

        // Is this the main ALandscape actor?
        ALandscape* LandscapeActor = Cast<ALandscape>(Proxy);
        LandJson->SetBoolField(TEXT("is_landscape_actor"), LandscapeActor != nullptr);

        LandscapesArray.Add(MakeShareable(new FJsonValueObject(LandJson)));
    }

    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("count"), LandscapesArray.Num());
    Result->SetArrayField(TEXT("landscapes"), LandscapesArray);
    return Result;
}
