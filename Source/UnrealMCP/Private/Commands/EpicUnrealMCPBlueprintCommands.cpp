#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPWidgetCommands.h"
#include "WidgetBlueprint.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstance.h"
#include "VT/RuntimeVirtualTexture.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/Engine.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

FEpicUnrealMCPBlueprintCommands::FEpicUnrealMCPBlueprintCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_blueprint"))
    {
        return HandleCreateBlueprint(Params);
    }
    else if (CommandType == TEXT("add_component_to_blueprint"))
    {
        return HandleAddComponentToBlueprint(Params);
    }
    else if (CommandType == TEXT("add_blueprint_component"))
    {
        return HandleAddBlueprintComponent(Params);
    }
    else if (CommandType == TEXT("reparent_blueprint_component"))
    {
        return HandleReparentBlueprintComponent(Params);
    }
    else if (CommandType == TEXT("remove_component_from_blueprint"))
    {
        return HandleRemoveComponentFromBlueprint(Params);
    }
    else if (CommandType == TEXT("get_blueprint_components"))
    {
        return HandleGetBlueprintComponents(Params);
    }
    else if (CommandType == TEXT("list_blueprint_components"))
    {
        return HandleListBlueprintComponents(Params);
    }
    else if (CommandType == TEXT("set_physics_properties"))
    {
        return HandleSetPhysicsProperties(Params);
    }
    else if (CommandType == TEXT("compile_blueprint"))
    {
        return HandleCompileBlueprint(Params);
    }
    else if (CommandType == TEXT("compile_blueprint_with_diagnostics"))
    {
        return HandleCompileBlueprintWithDiagnostics(Params);
    }
    else if (CommandType == TEXT("save_blueprint"))
    {
        return HandleSaveBlueprint(Params);
    }
    else if (CommandType == TEXT("list_blueprints"))
    {
        return HandleListBlueprints(Params);
    }
    else if (CommandType == TEXT("set_static_mesh_properties"))
    {
        return HandleSetStaticMeshProperties(Params);
    }
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    else if (CommandType == TEXT("set_mesh_material_color"))
    {
        return HandleSetMeshMaterialColor(Params);
    }
    // Material management commands
    else if (CommandType == TEXT("get_available_materials"))
    {
        return HandleGetAvailableMaterials(Params);
    }
    else if (CommandType == TEXT("apply_material_to_actor"))
    {
        return HandleApplyMaterialToActor(Params);
    }
    else if (CommandType == TEXT("apply_material_to_blueprint"))
    {
        return HandleApplyMaterialToBlueprint(Params);
    }
    else if (CommandType == TEXT("get_actor_material_info"))
    {
        return HandleGetActorMaterialInfo(Params);
    }
    else if (CommandType == TEXT("get_blueprint_material_info"))
    {
        return HandleGetBlueprintMaterialInfo(Params);
    }
    // Blueprint analysis commands
    else if (CommandType == TEXT("read_blueprint_content"))
    {
        return HandleReadBlueprintContent(Params);
    }
    else if (CommandType == TEXT("analyze_blueprint_graph"))
    {
        return HandleAnalyzeBlueprintGraph(Params);
    }
    else if (CommandType == TEXT("get_blueprint_variable_details"))
    {
        return HandleGetBlueprintVariableDetails(Params);
    }
    else if (CommandType == TEXT("get_blueprint_function_details"))
    {
        return HandleGetBlueprintFunctionDetails(Params);
    }
    // Material instance parameter extraction
    else if (CommandType == TEXT("get_material_instance_parameters"))
    {
        return HandleGetMaterialInstanceParameters(Params);
    }
    else if (CommandType == TEXT("set_material_instance_parameters"))
    {
        return HandleSetMaterialInstanceParameters(Params);
    }
    // CDO default-value editing
    else if (CommandType == TEXT("get_blueprint_default_value"))
    {
        return HandleGetBlueprintDefaultValue(Params);
    }
    else if (CommandType == TEXT("set_blueprint_default_value"))
    {
        return HandleSetBlueprintDefaultValue(Params);
    }
    else if (CommandType == TEXT("reset_blueprint_default_value"))
    {
        return HandleResetBlueprintDefaultValue(Params);
    }
    // Component archetype editing (inherited C++ default subobjects + SCS-added)
    else if (CommandType == TEXT("get_component_default_value"))
    {
        return HandleGetComponentDefaultValue(Params);
    }
    else if (CommandType == TEXT("set_component_default_value"))
    {
        return HandleSetComponentDefaultValue(Params);
    }
    else if (CommandType == TEXT("reset_component_default_value"))
    {
        return HandleResetComponentDefaultValue(Params);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Check if blueprint already exists
    FString PackagePath = TEXT("/Game/Blueprints/");
    FString AssetName = BlueprintName;
    if (FEpicUnrealMCPCommonUtils::AssetExists(PackagePath + AssetName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint already exists: %s"), *BlueprintName));
    }

    // Create the blueprint factory
    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    
    // Handle parent class
    FString ParentClass;
    Params->TryGetStringField(TEXT("parent_class"), ParentClass);
    
    // Default to Actor if no parent class specified
    UClass* SelectedParentClass = AActor::StaticClass();
    
    // Try to find the specified parent class
    if (!ParentClass.IsEmpty())
    {
        FString ClassName = ParentClass;
        if (!ClassName.StartsWith(TEXT("A")))
        {
            ClassName = TEXT("A") + ClassName;
        }
        
        // First try direct StaticClass lookup for common classes
        UClass* FoundClass = nullptr;
        if (ClassName == TEXT("APawn"))
        {
            FoundClass = APawn::StaticClass();
        }
        else if (ClassName == TEXT("AActor"))
        {
            FoundClass = AActor::StaticClass();
        }
        else
        {
            // Try loading the class using LoadClass which is more reliable than FindObject
            const FString ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
            FoundClass = LoadClass<AActor>(nullptr, *ClassPath);
            
            if (!FoundClass)
            {
                // Try alternate paths if not found
                const FString GameClassPath = FString::Printf(TEXT("/Script/Game.%s"), *ClassName);
                FoundClass = LoadClass<AActor>(nullptr, *GameClassPath);
            }
        }

        if (FoundClass)
        {
            SelectedParentClass = FoundClass;
            UE_LOG(LogTemp, Log, TEXT("Successfully set parent class to '%s'"), *ClassName);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find specified parent class '%s' at paths: /Script/Engine.%s or /Script/Game.%s, defaulting to AActor"), 
                *ClassName, *ClassName, *ClassName);
        }
    }
    
    Factory->ParentClass = SelectedParentClass;

    // Create the blueprint
    UPackage* Package = CreatePackage(*(PackagePath + AssetName));
    UBlueprint* NewBlueprint = Cast<UBlueprint>(Factory->FactoryCreateNew(UBlueprint::StaticClass(), Package, *AssetName, RF_Standalone | RF_Public, nullptr, GWarn));

    if (NewBlueprint)
    {
        // Notify the asset registry
        FAssetRegistryModule::AssetCreated(NewBlueprint);

        // Mark the package dirty
        Package->MarkPackageDirty();

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("name"), AssetName);
        ResultObj->SetStringField(TEXT("path"), PackagePath + AssetName);
        return ResultObj;
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create blueprint"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleAddComponentToBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentType;
    if (!Params->TryGetStringField(TEXT("component_type"), ComponentType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Create the component - dynamically find the component class by name
    UClass* ComponentClass = nullptr;

    // Try to find the class with exact name first
    ComponentClass = FindObject<UClass>(nullptr, *ComponentType);
    
    // If not found, try with "Component" suffix
    if (!ComponentClass && !ComponentType.EndsWith(TEXT("Component")))
    {
        FString ComponentTypeWithSuffix = ComponentType + TEXT("Component");
        ComponentClass = FindObject<UClass>(nullptr, *ComponentTypeWithSuffix);
    }
    
    // If still not found, try with "U" prefix
    if (!ComponentClass && !ComponentType.StartsWith(TEXT("U")))
    {
        FString ComponentTypeWithPrefix = TEXT("U") + ComponentType;
        ComponentClass = FindObject<UClass>(nullptr, *ComponentTypeWithPrefix);
        
        // Try with both prefix and suffix
        if (!ComponentClass && !ComponentType.EndsWith(TEXT("Component")))
        {
            FString ComponentTypeWithBoth = TEXT("U") + ComponentType + TEXT("Component");
            ComponentClass = FindObject<UClass>(nullptr, *ComponentTypeWithBoth);
        }
    }
    
    // Verify that the class is a valid component type
    if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown component type: %s"), *ComponentType));
    }

    // Add the component to the blueprint. Guard the SCS: data-only / function-library
    // blueprints have no SimpleConstructionScript, and dereferencing it would crash the
    // shared editor process instead of returning an error.
    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint has no SimpleConstructionScript (cannot add components): %s"), *BlueprintName));
    }
    USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, *ComponentName);
    if (NewNode)
    {
        // Set transform if provided
        USceneComponent* SceneComponent = Cast<USceneComponent>(NewNode->ComponentTemplate);
        if (SceneComponent)
        {
            if (Params->HasField(TEXT("location")))
            {
                SceneComponent->SetRelativeLocation(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
            }
            if (Params->HasField(TEXT("rotation")))
            {
                SceneComponent->SetRelativeRotation(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
            }
            if (Params->HasField(TEXT("scale")))
            {
                SceneComponent->SetRelativeScale3D(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
            }
        }

        // Add to root if no parent specified
        Blueprint->SimpleConstructionScript->AddNode(NewNode);

        // Compile the blueprint
        FKismetEditorUtilities::CompileBlueprint(Blueprint);

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("component_name"), ComponentName);
        ResultObj->SetStringField(TEXT("component_type"), ComponentType);
        return ResultObj;
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to add component to blueprint"));
}

// ============================================================================
// add_blueprint_component — reliable component-add. The older
// add_component_to_blueprint resolves the class with FindObject<UClass>(nullptr, name)
// (fails for normal short names like "StaticMeshComponent") and ALWAYS adds at the
// SCS root (no parent attach → orphaned components whose relative transform looks
// inert). This one:
//   • resolves the class via UClass::TryFindTypeSlow with U-prefix / Component-suffix /
//     /Script path fallbacks, so normal type names work;
//   • attaches under an existing parent component (SCS node OR native component) when
//     parent_component is given;
//   • returns the post-compile state (resolved class, attached parent, transform) to verify.
// ============================================================================

// File-local robust component-class resolver.
static UClass* ResolveComponentClass(const FString& TypeName)
{
    auto TryName = [](const FString& N) -> UClass*
    {
        UClass* C = UClass::TryFindTypeSlow<UClass>(N);
        return (C && C->IsChildOf(UActorComponent::StaticClass())) ? C : nullptr;
    };

    // Direct: handles "StaticMeshComponent" and "/Script/Engine.StaticMeshComponent".
    if (UClass* C = TryName(TypeName)) return C;
    // + "Component" suffix: "StaticMesh" -> "StaticMeshComponent".
    if (!TypeName.EndsWith(TEXT("Component")))
    {
        if (UClass* C = TryName(TypeName + TEXT("Component"))) return C;
    }
    // Strip a leading C++ "U" prefix: "UStaticMeshComponent" -> "StaticMeshComponent".
    if (TypeName.Len() > 1 && TypeName[0] == TEXT('U') && FChar::IsUpper(TypeName[1]))
    {
        const FString Stripped = TypeName.RightChop(1);
        if (UClass* C = TryName(Stripped)) return C;
        if (!Stripped.EndsWith(TEXT("Component")))
        {
            if (UClass* C = TryName(Stripped + TEXT("Component"))) return C;
        }
    }
    return nullptr;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleAddBlueprintComponent(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString ComponentType;
    if (!Params->TryGetStringField(TEXT("component_type"), ComponentType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_type' parameter"));
    }
    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }
    FString ParentComponent;
    Params->TryGetStringField(TEXT("parent_component"), ParentComponent);

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UClass* ComponentClass = ResolveComponentClass(ComponentType);
    if (!ComponentClass)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown component type: '%s' (no UActorComponent subclass resolved)"), *ComponentType));
    }

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (!SCS)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint has no SimpleConstructionScript (cannot add components): %s"), *BlueprintName));
    }

    // Reject duplicate variable names — they cause compile errors / silent renames.
    for (USCS_Node* Node : SCS->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Component '%s' already exists on blueprint"), *ComponentName));
        }
    }

    USCS_Node* NewNode = SCS->CreateNode(ComponentClass, *ComponentName);
    if (!NewNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create component node"));
    }

    // Transform (scene components only).
    USceneComponent* SceneTemplate = Cast<USceneComponent>(NewNode->ComponentTemplate);
    if (SceneTemplate)
    {
        if (Params->HasField(TEXT("location")))
            SceneTemplate->SetRelativeLocation(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
        if (Params->HasField(TEXT("rotation")))
            SceneTemplate->SetRelativeRotation(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
        if (Params->HasField(TEXT("scale")))
            SceneTemplate->SetRelativeScale3D(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
    }

    // Attachment.
    FString ResolvedParent;
    bool bParentIgnored = false;
    if (!ParentComponent.IsEmpty() && SceneTemplate)
    {
        // 1) An existing SCS node (this blueprint).
        USCS_Node* ParentNode = nullptr;
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (Node && Node->GetVariableName().ToString() == ParentComponent)
            {
                ParentNode = Node;
                break;
            }
        }

        if (ParentNode)
        {
            ParentNode->AddChildNode(NewNode); // registers in AllNodes + child array
            ResolvedParent = ParentComponent;
        }
        else
        {
            // 2) A native component on the CDO (e.g. the native scene root or a native mesh).
            USceneComponent* NativeParent = nullptr;
            UClass* GenClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->SkeletonGeneratedClass.Get();
            if (AActor* CDO = GenClass ? Cast<AActor>(GenClass->GetDefaultObject()) : nullptr)
            {
                TInlineComponentArray<USceneComponent*> SceneComps(CDO);
                for (USceneComponent* Comp : SceneComps)
                {
                    if (Comp && Comp->GetFName().ToString() == ParentComponent)
                    {
                        NativeParent = Comp;
                        break;
                    }
                }
            }

            if (NativeParent)
            {
                SCS->AddNode(NewNode);
                NewNode->SetParent(NativeParent); // marks native-parent linkage
                ResolvedParent = ParentComponent;
            }
            else
            {
                // Parent not found — fail cleanly rather than silently orphaning at root.
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("parent_component '%s' not found (no SCS node or native component)"), *ParentComponent));
            }
        }
    }
    else
    {
        if (!ParentComponent.IsEmpty() && !SceneTemplate)
        {
            bParentIgnored = true; // non-scene components can't attach
        }
        SCS->AddNode(NewNode); // root
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    ResultObj->SetStringField(TEXT("component_type"), ComponentType);
    ResultObj->SetStringField(TEXT("resolved_class"), ComponentClass->GetName());
    ResultObj->SetStringField(TEXT("parent"), ResolvedParent);
    ResultObj->SetBoolField(TEXT("is_scene_component"), SceneTemplate != nullptr);
    if (bParentIgnored)
    {
        ResultObj->SetBoolField(TEXT("parent_ignored"), true);
        ResultObj->SetStringField(TEXT("warning"), TEXT("parent_component ignored: component is not a scene component and cannot attach"));
    }
    if (SceneTemplate)
    {
        const FVector Loc = SceneTemplate->GetRelativeLocation();
        const FRotator Rot = SceneTemplate->GetRelativeRotation();
        const FVector Scl = SceneTemplate->GetRelativeScale3D();
        TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
        LocObj->SetNumberField(TEXT("x"), Loc.X); LocObj->SetNumberField(TEXT("y"), Loc.Y); LocObj->SetNumberField(TEXT("z"), Loc.Z);
        Transform->SetObjectField(TEXT("location"), LocObj);
        TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
        RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch); RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw); RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
        Transform->SetObjectField(TEXT("rotation"), RotObj);
        TSharedPtr<FJsonObject> SclObj = MakeShared<FJsonObject>();
        SclObj->SetNumberField(TEXT("x"), Scl.X); SclObj->SetNumberField(TEXT("y"), Scl.Y); SclObj->SetNumberField(TEXT("z"), Scl.Z);
        Transform->SetObjectField(TEXT("scale"), SclObj);
        ResultObj->SetObjectField(TEXT("transform"), Transform);
    }
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetPhysicsProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint has no SimpleConstructionScript: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Set physics properties
    if (Params->HasField(TEXT("simulate_physics")))
    {
        PrimComponent->SetSimulatePhysics(Params->GetBoolField(TEXT("simulate_physics")));
    }

    if (Params->HasField(TEXT("mass")))
    {
        float Mass = Params->GetNumberField(TEXT("mass"));
        // In UE5.5, use proper overrideMass instead of just scaling
        PrimComponent->SetMassOverrideInKg(NAME_None, Mass);
        UE_LOG(LogTemp, Display, TEXT("Set mass for component %s to %f kg"), *ComponentName, Mass);
    }

    if (Params->HasField(TEXT("linear_damping")))
    {
        PrimComponent->SetLinearDamping(Params->GetNumberField(TEXT("linear_damping")));
    }

    if (Params->HasField(TEXT("angular_damping")))
    {
        PrimComponent->SetAngularDamping(Params->GetNumberField(TEXT("angular_damping")));
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Widget Blueprints need GUID-map reconciliation before compile or the
    // compiler's ValidateAndFixUpVariableGuids ensure fires (and drops
    // unresolved widgets to TRASH_*). Tree mutations made via run_python or
    // any tool that bypassed UWidgetBlueprint::OnVariableAdded land here.
    if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Blueprint))
    {
        EpicUnrealMCP_ReconcileWidgetBlueprintGuids(WBP);
    }

    // Compile the blueprint
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), BlueprintName);
    ResultObj->SetBoolField(TEXT("compiled"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCompileBlueprintWithDiagnostics(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Blueprint))
    {
        EpicUnrealMCP_ReconcileWidgetBlueprintGuids(WBP);
    }

    FCompilerResultsLog Results;
    Results.SetSourcePath(Blueprint->GetPathName());
    Results.BeginEvent(TEXT("MCP_CompileBlueprintWithDiagnostics"));

    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);

    Results.EndEvent();

    TArray<TSharedPtr<FJsonValue>> ErrorEntries;
    TArray<TSharedPtr<FJsonValue>> WarningEntries;
    TArray<TSharedPtr<FJsonValue>> NoteEntries;

    for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        const EMessageSeverity::Type Sev = Msg->GetSeverity();
        FString SeverityStr;
        switch (Sev)
        {
            case EMessageSeverity::Error:              SeverityStr = TEXT("Error");        break;
            case EMessageSeverity::PerformanceWarning: SeverityStr = TEXT("PerfWarning");  break;
            case EMessageSeverity::Warning:            SeverityStr = TEXT("Warning");      break;
            case EMessageSeverity::Info:               SeverityStr = TEXT("Info");         break;
            default:                                   SeverityStr = TEXT("Unknown");      break;
        }
        Entry->SetStringField(TEXT("severity"), SeverityStr);
        Entry->SetStringField(TEXT("text"), Msg->ToText().ToString());

        TSharedPtr<FJsonValue> JsonValue = MakeShared<FJsonValueObject>(Entry);
        if (Sev == EMessageSeverity::Error)
        {
            ErrorEntries.Add(JsonValue);
        }
        else if (Sev == EMessageSeverity::Warning || Sev == EMessageSeverity::PerformanceWarning)
        {
            WarningEntries.Add(JsonValue);
        }
        else
        {
            NoteEntries.Add(JsonValue);
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), BlueprintName);
    ResultObj->SetStringField(TEXT("path"), Blueprint->GetPathName());
    ResultObj->SetNumberField(TEXT("num_errors"), Results.NumErrors);
    ResultObj->SetNumberField(TEXT("num_warnings"), Results.NumWarnings);
    ResultObj->SetBoolField(TEXT("compiled"), Results.NumErrors == 0);
    ResultObj->SetArrayField(TEXT("errors"), ErrorEntries);
    ResultObj->SetArrayField(TEXT("warnings"), WarningEntries);
    ResultObj->SetArrayField(TEXT("notes"), NoteEntries);

    const FString StatusStr = (Blueprint->Status == BS_UpToDate) ? TEXT("UpToDate")
        : (Blueprint->Status == BS_Dirty) ? TEXT("Dirty")
        : (Blueprint->Status == BS_Error) ? TEXT("Error")
        : (Blueprint->Status == BS_Unknown) ? TEXT("Unknown")
        : TEXT("UpToDateWithWarnings");
    ResultObj->SetStringField(TEXT("status"), StatusStr);

    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Starting blueprint actor spawn"));
    
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Missing blueprint_name parameter"));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Missing actor_name parameter"));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Looking for blueprint '%s'"), *BlueprintName);

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Blueprint not found: %s"), *BlueprintName);
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Blueprint found, getting transform parameters"));

    // Get transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Location set to (%f, %f, %f)"), Location.X, Location.Y, Location.Z);
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Rotation set to (%f, %f, %f)"), Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Getting editor world"));

    // Spawn the actor
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Failed to get editor world"));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Creating spawn transform"));

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));

    // Add a small delay to allow the engine to process the newly compiled class
    FPlatformProcess::Sleep(0.2f);

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: About to spawn actor from blueprint '%s' with GeneratedClass: %s"), 
           *BlueprintName, Blueprint->GeneratedClass ? *Blueprint->GeneratedClass->GetName() : TEXT("NULL"));

    AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform);
    
    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: SpawnActor completed, NewActor: %s"), 
           NewActor ? *NewActor->GetName() : TEXT("NULL"));
    
    if (NewActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Setting actor label to '%s'"), *ActorName);
        NewActor->SetActorLabel(*ActorName);
        
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: About to convert actor to JSON"));
        TSharedPtr<FJsonObject> Result = FEpicUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
        
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: JSON conversion completed, returning result"));
        return Result;
    }

    UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Failed to spawn blueprint actor"));
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to spawn blueprint actor"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetStaticMeshProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint has no SimpleConstructionScript: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentNode->ComponentTemplate);
    if (!MeshComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a static mesh component"));
    }

    // Set static mesh properties
    if (Params->HasField(TEXT("static_mesh")))
    {
        FString MeshPath = Params->GetStringField(TEXT("static_mesh"));
        UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
        if (Mesh)
        {
            MeshComponent->SetStaticMesh(Mesh);
        }
    }

    if (Params->HasField(TEXT("material")))
    {
        FString MaterialPath = Params->GetStringField(TEXT("material"));
        UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
        if (Material)
        {
            MeshComponent->SetMaterial(0, Material);
        }
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetMeshMaterialColor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint has no SimpleConstructionScript: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    // Try to cast to StaticMeshComponent or PrimitiveComponent
    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Get color parameter
    TArray<float> ColorArray;
    const TArray<TSharedPtr<FJsonValue>>* ColorJsonArray;
    if (!Params->TryGetArrayField(TEXT("color"), ColorJsonArray) || ColorJsonArray->Num() != 4)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'color' must be an array of 4 float values [R, G, B, A]"));
    }

    for (const TSharedPtr<FJsonValue>& Value : *ColorJsonArray)
    {
        ColorArray.Add(FMath::Clamp(Value->AsNumber(), 0.0f, 1.0f));
    }

    FLinearColor Color(ColorArray[0], ColorArray[1], ColorArray[2], ColorArray[3]);

    // Get material slot index
    int32 MaterialSlot = 0;
    if (Params->HasField(TEXT("material_slot")))
    {
        MaterialSlot = Params->GetIntegerField(TEXT("material_slot"));
    }

    // Get parameter name
    FString ParameterName = TEXT("BaseColor");
    Params->TryGetStringField(TEXT("parameter_name"), ParameterName);

    // Get or create material
    UMaterialInterface* Material = nullptr;
    
    // Check if a specific material path was provided
    FString MaterialPath;
    if (Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
        if (!Material)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material: %s"), *MaterialPath));
        }
    }
    else
    {
        // Use existing material on the component
        Material = PrimComponent->GetMaterial(MaterialSlot);
        if (!Material)
        {
            // Try to use a default material
            Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(TEXT("/Engine/BasicShapes/BasicShapeMaterial")));
            if (!Material)
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No material found on component and failed to load default material"));
            }
        }
    }

    // Create a dynamic material instance
    UMaterialInstanceDynamic* DynMaterial = UMaterialInstanceDynamic::Create(Material, PrimComponent);
    if (!DynMaterial)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create dynamic material instance"));
    }

    // Set the color parameter
    DynMaterial->SetVectorParameterValue(*ParameterName, Color);

    // Apply the material to the component
    PrimComponent->SetMaterial(MaterialSlot, DynMaterial);

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    // Log success
    UE_LOG(LogTemp, Log, TEXT("Successfully set material color on component %s: R=%f, G=%f, B=%f, A=%f"), 
        *ComponentName, Color.R, Color.G, Color.B, Color.A);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    ResultObj->SetNumberField(TEXT("material_slot"), MaterialSlot);
    ResultObj->SetStringField(TEXT("parameter_name"), ParameterName);
    
    TArray<TSharedPtr<FJsonValue>> ColorResultArray;
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.R));
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.G));
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.B));
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.A));
    ResultObj->SetArrayField(TEXT("color"), ColorResultArray);
    
    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetAvailableMaterials(const TSharedPtr<FJsonObject>& Params)
{
    // Get parameters - make search path completely dynamic
    FString SearchPath;
    if (!Params->TryGetStringField(TEXT("search_path"), SearchPath))
    {
        // Default to empty string to search everywhere
        SearchPath = TEXT("");
    }
    
    bool bIncludeEngineMaterials = true;
    if (Params->HasField(TEXT("include_engine_materials")))
    {
        bIncludeEngineMaterials = Params->GetBoolField(TEXT("include_engine_materials"));
    }

    // Get asset registry module
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Create filter for materials
    FARFilter Filter;
    Filter.ClassPaths.Add(UMaterialInterface::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterialInstanceDynamic::StaticClass()->GetClassPathName());
    
    // Add search paths dynamically
    if (!SearchPath.IsEmpty())
    {
        // Ensure the path starts with /
        if (!SearchPath.StartsWith(TEXT("/")))
        {
            SearchPath = TEXT("/") + SearchPath;
        }
        // Ensure the path ends with / for proper directory search
        if (!SearchPath.EndsWith(TEXT("/")))
        {
            SearchPath += TEXT("/");
        }
        Filter.PackagePaths.Add(*SearchPath);
        UE_LOG(LogTemp, Log, TEXT("Searching for materials in: %s"), *SearchPath);
    }
    else
    {
        // Search in common game content locations
        Filter.PackagePaths.Add(TEXT("/Game/"));
        UE_LOG(LogTemp, Log, TEXT("Searching for materials in all game content"));
    }
    
    if (bIncludeEngineMaterials)
    {
        Filter.PackagePaths.Add(TEXT("/Engine/"));
        UE_LOG(LogTemp, Log, TEXT("Including Engine materials in search"));
    }
    
    Filter.bRecursivePaths = true;

    // Get assets from registry
    TArray<FAssetData> AssetDataArray;
    AssetRegistry.GetAssets(Filter, AssetDataArray);
    
    UE_LOG(LogTemp, Log, TEXT("Asset registry found %d materials"), AssetDataArray.Num());

    // Also try manual search using EditorAssetLibrary for more comprehensive results
    TArray<FString> AllAssetPaths;
    if (!SearchPath.IsEmpty())
    {
        AllAssetPaths = UEditorAssetLibrary::ListAssets(SearchPath, true, false);
    }
    else
    {
        AllAssetPaths = UEditorAssetLibrary::ListAssets(TEXT("/Game/"), true, false);
    }
    
    // Filter for materials from the manual search
    for (const FString& AssetPath : AllAssetPaths)
    {
        if (AssetPath.Contains(TEXT("Material")) && !AssetPath.Contains(TEXT(".uasset")))
        {
            UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
            if (Asset && Asset->IsA<UMaterialInterface>())
            {
                // Check if we already have this asset from registry search
                bool bAlreadyFound = false;
                for (const FAssetData& ExistingData : AssetDataArray)
                {
                    if (ExistingData.GetObjectPathString() == AssetPath)
                    {
                        bAlreadyFound = true;
                        break;
                    }
                }
                
                if (!bAlreadyFound)
                {
                    // Create FAssetData manually for this asset
                    FAssetData ManualAssetData(Asset);
                    AssetDataArray.Add(ManualAssetData);
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Total materials found after manual search: %d"), AssetDataArray.Num());

    // Convert to JSON
    TArray<TSharedPtr<FJsonValue>> MaterialArray;
    for (const FAssetData& AssetData : AssetDataArray)
    {
        TSharedPtr<FJsonObject> MaterialObj = MakeShared<FJsonObject>();
        MaterialObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
        MaterialObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
        MaterialObj->SetStringField(TEXT("package"), AssetData.PackageName.ToString());
        MaterialObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
        
        MaterialArray.Add(MakeShared<FJsonValueObject>(MaterialObj));
        
        UE_LOG(LogTemp, Verbose, TEXT("Found material: %s at %s"), *AssetData.AssetName.ToString(), *AssetData.GetObjectPathString());
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("materials"), MaterialArray);
    ResultObj->SetNumberField(TEXT("count"), MaterialArray.Num());
    ResultObj->SetStringField(TEXT("search_path_used"), SearchPath.IsEmpty() ? TEXT("/Game/") : SearchPath);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleApplyMaterialToActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    int32 MaterialSlot = 0;
    if (Params->HasField(TEXT("material_slot")))
    {
        MaterialSlot = Params->GetIntegerField(TEXT("material_slot"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    
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

    // Load the material
    UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
    if (!Material)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material: %s"), *MaterialPath));
    }

    // Find mesh components and apply material
    TArray<UStaticMeshComponent*> MeshComponents;
    TargetActor->GetComponents<UStaticMeshComponent>(MeshComponents);
    
    bool bAppliedToAny = false;
    for (UStaticMeshComponent* MeshComp : MeshComponents)
    {
        if (MeshComp)
        {
            MeshComp->SetMaterial(MaterialSlot, Material);
            bAppliedToAny = true;
        }
    }

    if (!bAppliedToAny)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No mesh components found on actor"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
    ResultObj->SetNumberField(TEXT("material_slot"), MaterialSlot);
    ResultObj->SetBoolField(TEXT("success"), true);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleApplyMaterialToBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    int32 MaterialSlot = 0;
    if (Params->HasField(TEXT("material_slot")))
    {
        MaterialSlot = Params->GetIntegerField(TEXT("material_slot"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint has no SimpleConstructionScript: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Load the material
    UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
    if (!Material)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material: %s"), *MaterialPath));
    }

    // Apply the material
    PrimComponent->SetMaterial(MaterialSlot, Material);

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
    ResultObj->SetNumberField(TEXT("material_slot"), MaterialSlot);
    ResultObj->SetBoolField(TEXT("success"), true);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetActorMaterialInfo(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    
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

    // Get mesh components and their materials
    TArray<UStaticMeshComponent*> MeshComponents;
    TargetActor->GetComponents<UStaticMeshComponent>(MeshComponents);
    
    TArray<TSharedPtr<FJsonValue>> MaterialSlots;
    
    for (UStaticMeshComponent* MeshComp : MeshComponents)
    {
        if (MeshComp)
        {
            for (int32 i = 0; i < MeshComp->GetNumMaterials(); i++)
            {
                TSharedPtr<FJsonObject> SlotInfo = MakeShared<FJsonObject>();
                SlotInfo->SetNumberField(TEXT("slot"), i);
                SlotInfo->SetStringField(TEXT("component"), MeshComp->GetName());
                
                UMaterialInterface* Material = MeshComp->GetMaterial(i);
                if (Material)
                {
                    SlotInfo->SetStringField(TEXT("material_name"), Material->GetName());
                    SlotInfo->SetStringField(TEXT("material_path"), Material->GetPathName());
                    SlotInfo->SetStringField(TEXT("material_class"), Material->GetClass()->GetName());
                }
                else
                {
                    SlotInfo->SetStringField(TEXT("material_name"), TEXT("None"));
                    SlotInfo->SetStringField(TEXT("material_path"), TEXT(""));
                    SlotInfo->SetStringField(TEXT("material_class"), TEXT(""));
                }
                
                MaterialSlots.Add(MakeShared<FJsonValueObject>(SlotInfo));
            }
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetArrayField(TEXT("material_slots"), MaterialSlots);
    ResultObj->SetNumberField(TEXT("total_slots"), MaterialSlots.Num());
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintMaterialInfo(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint has no SimpleConstructionScript: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentNode->ComponentTemplate);
    if (!MeshComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a static mesh component"));
    }

    // Get material slot information
    TArray<TSharedPtr<FJsonValue>> MaterialSlots;
    int32 NumMaterials = 0;
    
    // Check if we have a static mesh assigned
    UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
    if (StaticMesh)
    {
        NumMaterials = StaticMesh->GetNumSections(0); // Get number of material slots for LOD 0
        
        for (int32 i = 0; i < NumMaterials; i++)
        {
            TSharedPtr<FJsonObject> SlotInfo = MakeShared<FJsonObject>();
            SlotInfo->SetNumberField(TEXT("slot"), i);
            SlotInfo->SetStringField(TEXT("component"), ComponentName);
            
            UMaterialInterface* Material = MeshComponent->GetMaterial(i);
            if (Material)
            {
                SlotInfo->SetStringField(TEXT("material_name"), Material->GetName());
                SlotInfo->SetStringField(TEXT("material_path"), Material->GetPathName());
                SlotInfo->SetStringField(TEXT("material_class"), Material->GetClass()->GetName());
            }
            else
            {
                SlotInfo->SetStringField(TEXT("material_name"), TEXT("None"));
                SlotInfo->SetStringField(TEXT("material_path"), TEXT(""));
                SlotInfo->SetStringField(TEXT("material_class"), TEXT(""));
            }
            
            MaterialSlots.Add(MakeShared<FJsonValueObject>(SlotInfo));
        }
    }
    else
    {
        // If no static mesh is assigned, we can't determine material slots
        UE_LOG(LogTemp, Warning, TEXT("No static mesh assigned to component %s in blueprint %s"), *ComponentName, *BlueprintName);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    ResultObj->SetArrayField(TEXT("material_slots"), MaterialSlots);
    ResultObj->SetNumberField(TEXT("total_slots"), MaterialSlots.Num());
    ResultObj->SetBoolField(TEXT("has_static_mesh"), StaticMesh != nullptr);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleReadBlueprintContent(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    // Get optional parameters
    bool bIncludeEventGraph = true;
    bool bIncludeFunctions = true;
    bool bIncludeVariables = true;
    bool bIncludeComponents = true;
    bool bIncludeInterfaces = true;

    Params->TryGetBoolField(TEXT("include_event_graph"), bIncludeEventGraph);
    Params->TryGetBoolField(TEXT("include_functions"), bIncludeFunctions);
    Params->TryGetBoolField(TEXT("include_variables"), bIncludeVariables);
    Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
    Params->TryGetBoolField(TEXT("include_interfaces"), bIncludeInterfaces);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(FEpicUnrealMCPCommonUtils::ResolveAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));

    // Include variables if requested
    if (bIncludeVariables)
    {
        TArray<TSharedPtr<FJsonValue>> VariableArray;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
            VarObj->SetStringField(TEXT("name"), Variable.VarName.ToString());
            VarObj->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
            VarObj->SetStringField(TEXT("default_value"), Variable.DefaultValue);
            VarObj->SetBoolField(TEXT("is_editable"), (Variable.PropertyFlags & CPF_Edit) != 0);
            VariableArray.Add(MakeShared<FJsonValueObject>(VarObj));
        }
        ResultObj->SetArrayField(TEXT("variables"), VariableArray);
    }

    // Include functions if requested
    if (bIncludeFunctions)
    {
        TArray<TSharedPtr<FJsonValue>> FunctionArray;
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph)
            {
                TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
                FuncObj->SetStringField(TEXT("name"), Graph->GetName());
                FuncObj->SetStringField(TEXT("graph_type"), TEXT("Function"));
                
                // Count nodes in function
                int32 NodeCount = Graph->Nodes.Num();
                FuncObj->SetNumberField(TEXT("node_count"), NodeCount);
                
                FunctionArray.Add(MakeShared<FJsonValueObject>(FuncObj));
            }
        }
        ResultObj->SetArrayField(TEXT("functions"), FunctionArray);
    }

    // Include event graph if requested
    if (bIncludeEventGraph)
    {
        TSharedPtr<FJsonObject> EventGraphObj = MakeShared<FJsonObject>();
        
        // Find the main event graph
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Graph && Graph->GetName() == TEXT("EventGraph"))
            {
                EventGraphObj->SetStringField(TEXT("name"), Graph->GetName());
                EventGraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
                
                // Get basic node information
                TArray<TSharedPtr<FJsonValue>> NodeArray;
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (Node)
                    {
                        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
                        NodeObj->SetStringField(TEXT("name"), Node->GetName());
                        NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
                        NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                        NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
                    }
                }
                EventGraphObj->SetArrayField(TEXT("nodes"), NodeArray);
                break;
            }
        }
        
        ResultObj->SetObjectField(TEXT("event_graph"), EventGraphObj);
    }

    // Include components if requested
    if (bIncludeComponents)
    {
        TArray<TSharedPtr<FJsonValue>> ComponentArray;
        if (Blueprint->SimpleConstructionScript)
        {
            for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (Node && Node->ComponentTemplate)
                {
                    TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
                    CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
                    CompObj->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
                    CompObj->SetBoolField(TEXT("is_root"), Node == Blueprint->SimpleConstructionScript->GetDefaultSceneRootNode());
                    ComponentArray.Add(MakeShared<FJsonValueObject>(CompObj));
                }
            }
        }
        ResultObj->SetArrayField(TEXT("components"), ComponentArray);
    }

    // Include interfaces if requested
    if (bIncludeInterfaces)
    {
        TArray<TSharedPtr<FJsonValue>> InterfaceArray;
        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        {
            TSharedPtr<FJsonObject> InterfaceObj = MakeShared<FJsonObject>();
            InterfaceObj->SetStringField(TEXT("name"), Interface.Interface ? Interface.Interface->GetName() : TEXT("Unknown"));
            InterfaceArray.Add(MakeShared<FJsonValueObject>(InterfaceObj));
        }
        ResultObj->SetArrayField(TEXT("interfaces"), InterfaceArray);
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleAnalyzeBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString GraphName = TEXT("EventGraph");
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    // Get optional parameters
    bool bIncludeNodeDetails = true;
    bool bIncludePinConnections = true;
    bool bTraceExecutionFlow = true;

    Params->TryGetBoolField(TEXT("include_node_details"), bIncludeNodeDetails);
    Params->TryGetBoolField(TEXT("include_pin_connections"), bIncludePinConnections);
    Params->TryGetBoolField(TEXT("trace_execution_flow"), bTraceExecutionFlow);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(FEpicUnrealMCPCommonUtils::ResolveAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    // Find the specified graph
    UEdGraph* TargetGraph = nullptr;
    
    // Check event graphs first
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph && Graph->GetName() == GraphName)
        {
            TargetGraph = Graph;
            break;
        }
    }
    
    // Check function graphs if not found
    if (!TargetGraph)
    {
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph && Graph->GetName() == GraphName)
            {
                TargetGraph = Graph;
                break;
            }
        }
    }

    if (!TargetGraph)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
    }

    TSharedPtr<FJsonObject> GraphData = MakeShared<FJsonObject>();
    GraphData->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
    GraphData->SetStringField(TEXT("graph_type"), TargetGraph->GetClass()->GetName());

    // Analyze nodes
    TArray<TSharedPtr<FJsonValue>> NodeArray;
    TArray<TSharedPtr<FJsonValue>> ConnectionArray;

    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (Node)
        {
            TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
            NodeObj->SetStringField(TEXT("name"), Node->GetName());
            NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
            NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

            if (bIncludeNodeDetails)
            {
                NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
                NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);
                NodeObj->SetBoolField(TEXT("can_rename"), Node->bCanRenameNode);
            }

            // Include pin information if requested
            if (bIncludePinConnections)
            {
                TArray<TSharedPtr<FJsonValue>> PinArray;
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin)
                    {
                        TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                        PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                        PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                        PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
                        PinObj->SetNumberField(TEXT("connections"), Pin->LinkedTo.Num());

                        // Emit default values for unconnected input pins
                        if (Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 0)
                        {
                            FString DefaultStr = Pin->GetDefaultAsString();
                            if (!DefaultStr.IsEmpty())
                            {
                                PinObj->SetStringField(TEXT("default_value"), DefaultStr);
                            }
                        }

                        // Emit pin sub-type (struct/class) if present
                        if (Pin->PinType.PinSubCategoryObject.IsValid())
                        {
                            PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategoryObject->GetName());
                        }

                        // Record connections for this pin
                        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                        {
                            if (LinkedPin && LinkedPin->GetOwningNode())
                            {
                                TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
                                ConnObj->SetStringField(TEXT("from_node"), Pin->GetOwningNode()->GetName());
                                ConnObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
                                ConnObj->SetStringField(TEXT("to_node"), LinkedPin->GetOwningNode()->GetName());
                                ConnObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
                                ConnectionArray.Add(MakeShared<FJsonValueObject>(ConnObj));
                            }
                        }
                        
                        PinArray.Add(MakeShared<FJsonValueObject>(PinObj));
                    }
                }
                NodeObj->SetArrayField(TEXT("pins"), PinArray);
            }

            NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
        }
    }

    GraphData->SetArrayField(TEXT("nodes"), NodeArray);
    GraphData->SetArrayField(TEXT("connections"), ConnectionArray);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    ResultObj->SetObjectField(TEXT("graph_data"), GraphData);
    ResultObj->SetBoolField(TEXT("success"), true);

    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintVariableDetails(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString VariableName;
    bool bSpecificVariable = Params->TryGetStringField(TEXT("variable_name"), VariableName);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(FEpicUnrealMCPCommonUtils::ResolveAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TArray<TSharedPtr<FJsonValue>> VariableArray;

    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        // If looking for specific variable, skip others
        if (bSpecificVariable && Variable.VarName.ToString() != VariableName)
        {
            continue;
        }

        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Variable.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
        VarObj->SetStringField(TEXT("sub_category"), Variable.VarType.PinSubCategory.ToString());
        VarObj->SetStringField(TEXT("default_value"), Variable.DefaultValue);
        VarObj->SetStringField(TEXT("friendly_name"), Variable.FriendlyName.IsEmpty() ? Variable.VarName.ToString() : Variable.FriendlyName);
        
        // Get tooltip from metadata (VarTooltip doesn't exist in UE 5.5)
        FString TooltipValue;
        if (Variable.HasMetaData(FBlueprintMetadata::MD_Tooltip))
        {
            TooltipValue = Variable.GetMetaData(FBlueprintMetadata::MD_Tooltip);
        }
        VarObj->SetStringField(TEXT("tooltip"), TooltipValue);
        
        VarObj->SetStringField(TEXT("category"), Variable.Category.ToString());

        // Property flags
        VarObj->SetBoolField(TEXT("is_editable"), (Variable.PropertyFlags & CPF_Edit) != 0);
        VarObj->SetBoolField(TEXT("is_blueprint_visible"), (Variable.PropertyFlags & CPF_BlueprintVisible) != 0);
        VarObj->SetBoolField(TEXT("is_editable_in_instance"), (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0);
        VarObj->SetBoolField(TEXT("is_config"), (Variable.PropertyFlags & CPF_Config) != 0);

        // Replication
        VarObj->SetNumberField(TEXT("replication"), (int32)Variable.ReplicationCondition);

        VariableArray.Add(MakeShared<FJsonValueObject>(VarObj));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    
    if (bSpecificVariable)
    {
        ResultObj->SetStringField(TEXT("variable_name"), VariableName);
        if (VariableArray.Num() > 0)
        {
            ResultObj->SetObjectField(TEXT("variable"), VariableArray[0]->AsObject());
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Variable not found: %s"), *VariableName));
        }
    }
    else
    {
        ResultObj->SetArrayField(TEXT("variables"), VariableArray);
        ResultObj->SetNumberField(TEXT("variable_count"), VariableArray.Num());
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintFunctionDetails(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString FunctionName;
    bool bSpecificFunction = Params->TryGetStringField(TEXT("function_name"), FunctionName);

    bool bIncludeGraph = true;
    Params->TryGetBoolField(TEXT("include_graph"), bIncludeGraph);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(FEpicUnrealMCPCommonUtils::ResolveAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TArray<TSharedPtr<FJsonValue>> FunctionArray;

    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (!Graph) continue;

        // If looking for specific function, skip others
        if (bSpecificFunction && Graph->GetName() != FunctionName)
        {
            continue;
        }

        TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
        FuncObj->SetStringField(TEXT("name"), Graph->GetName());
        FuncObj->SetStringField(TEXT("graph_type"), TEXT("Function"));

        // Get function signature from graph
        TArray<TSharedPtr<FJsonValue>> InputPins;
        TArray<TSharedPtr<FJsonValue>> OutputPins;

        // Find function entry and result nodes
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node)
            {
                if (Node->GetClass()->GetName().Contains(TEXT("FunctionEntry")))
                {
                    // Process input parameters
                    for (UEdGraphPin* Pin : Node->Pins)
                    {
                        if (Pin && Pin->Direction == EGPD_Output && Pin->PinName != TEXT("then"))
                        {
                            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                            InputPins.Add(MakeShared<FJsonValueObject>(PinObj));
                        }
                    }
                }
                else if (Node->GetClass()->GetName().Contains(TEXT("FunctionResult")))
                {
                    // Process output parameters
                    for (UEdGraphPin* Pin : Node->Pins)
                    {
                        if (Pin && Pin->Direction == EGPD_Input && Pin->PinName != TEXT("exec"))
                        {
                            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                            OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
                        }
                    }
                }
            }
        }

        FuncObj->SetArrayField(TEXT("input_parameters"), InputPins);
        FuncObj->SetArrayField(TEXT("output_parameters"), OutputPins);
        FuncObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

        // Include graph details if requested
        if (bIncludeGraph)
        {
            TArray<TSharedPtr<FJsonValue>> NodeArray;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node)
                {
                    TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
                    NodeObj->SetStringField(TEXT("name"), Node->GetName());
                    NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
                    NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                    NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
                }
            }
            FuncObj->SetArrayField(TEXT("graph_nodes"), NodeArray);
        }

        FunctionArray.Add(MakeShared<FJsonValueObject>(FuncObj));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    
    if (bSpecificFunction)
    {
        ResultObj->SetStringField(TEXT("function_name"), FunctionName);
        if (FunctionArray.Num() > 0)
        {
            ResultObj->SetObjectField(TEXT("function"), FunctionArray[0]->AsObject());
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Function not found: %s"), *FunctionName));
        }
    }
    else
    {
        ResultObj->SetArrayField(TEXT("functions"), FunctionArray);
        ResultObj->SetNumberField(TEXT("function_count"), FunctionArray.Num());
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetMaterialInstanceParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
	}

	// Load the asset
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(MaterialPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *MaterialPath));
	}

	UMaterialInstance* MatInstance = Cast<UMaterialInstance>(LoadedAsset);
	if (!MatInstance)
	{
		// Check if it's a base UMaterial — return basic info
		UMaterial* BaseMat = Cast<UMaterial>(LoadedAsset);
		if (BaseMat)
		{
			TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
			ResultObj->SetBoolField(TEXT("success"), true);
			ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
			ResultObj->SetBoolField(TEXT("is_material_instance"), false);
			ResultObj->SetStringField(TEXT("note"), TEXT("This is a base Material. Showing parameter names available for Material Instances to override."));

			// Scalar parameters
			{
				TArray<FMaterialParameterInfo> ParamInfos;
				TArray<FGuid> ParamGuids;
				BaseMat->GetAllScalarParameterInfo(ParamInfos, ParamGuids);
				TArray<TSharedPtr<FJsonValue>> ScalarArray;
				for (const FMaterialParameterInfo& Info : ParamInfos)
				{
					TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
					ParamObj->SetStringField(TEXT("name"), Info.Name.ToString());
					float DefaultValue = 0.f;
					BaseMat->GetScalarParameterValue(Info, DefaultValue);
					ParamObj->SetNumberField(TEXT("default_value"), DefaultValue);
					ScalarArray.Add(MakeShared<FJsonValueObject>(ParamObj));
				}
				ResultObj->SetArrayField(TEXT("scalar_parameters"), ScalarArray);
			}

			// Vector parameters
			{
				TArray<FMaterialParameterInfo> ParamInfos;
				TArray<FGuid> ParamGuids;
				BaseMat->GetAllVectorParameterInfo(ParamInfos, ParamGuids);
				TArray<TSharedPtr<FJsonValue>> VectorArray;
				for (const FMaterialParameterInfo& Info : ParamInfos)
				{
					TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
					ParamObj->SetStringField(TEXT("name"), Info.Name.ToString());
					FLinearColor DefaultValue = FLinearColor::Black;
					BaseMat->GetVectorParameterValue(Info, DefaultValue);
					TArray<TSharedPtr<FJsonValue>> ColorValues;
					ColorValues.Add(MakeShared<FJsonValueNumber>(DefaultValue.R));
					ColorValues.Add(MakeShared<FJsonValueNumber>(DefaultValue.G));
					ColorValues.Add(MakeShared<FJsonValueNumber>(DefaultValue.B));
					ColorValues.Add(MakeShared<FJsonValueNumber>(DefaultValue.A));
					ParamObj->SetArrayField(TEXT("default_value"), ColorValues);
					VectorArray.Add(MakeShared<FJsonValueObject>(ParamObj));
				}
				ResultObj->SetArrayField(TEXT("vector_parameters"), VectorArray);
			}

			// Texture parameters
			{
				TArray<FMaterialParameterInfo> ParamInfos;
				TArray<FGuid> ParamGuids;
				BaseMat->GetAllTextureParameterInfo(ParamInfos, ParamGuids);
				TArray<TSharedPtr<FJsonValue>> TextureArray;
				for (const FMaterialParameterInfo& Info : ParamInfos)
				{
					TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
					ParamObj->SetStringField(TEXT("name"), Info.Name.ToString());
					UTexture* TexValue = nullptr;
					BaseMat->GetTextureParameterValue(Info, TexValue);
					ParamObj->SetStringField(TEXT("default_value"), TexValue ? TexValue->GetPathName() : TEXT("None"));
					TextureArray.Add(MakeShared<FJsonValueObject>(ParamObj));
				}
				ResultObj->SetArrayField(TEXT("texture_parameters"), TextureArray);
			}

			return ResultObj;
		}
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a Material or MaterialInstance: %s"), *MaterialPath));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
	ResultObj->SetBoolField(TEXT("is_material_instance"), true);

	// Parent material
	if (MatInstance->Parent)
	{
		ResultObj->SetStringField(TEXT("parent_material"), MatInstance->Parent->GetPathName());
	}

	// --- Scalar Parameters ---
	{
		TArray<FMaterialParameterInfo> ParamInfos;
		TArray<FGuid> ParamGuids;
		MatInstance->GetAllScalarParameterInfo(ParamInfos, ParamGuids);

		TArray<TSharedPtr<FJsonValue>> ScalarArray;
		for (int32 i = 0; i < ParamInfos.Num(); i++)
		{
			float Value = 0.f;
			MatInstance->GetScalarParameterValue(ParamInfos[i], Value, true);

			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), ParamInfos[i].Name.ToString());
			ParamObj->SetStringField(TEXT("group"), ParamInfos[i].Association == EMaterialParameterAssociation::GlobalParameter ? TEXT("Global") : TEXT("Layer"));
			ParamObj->SetNumberField(TEXT("value"), Value);

			// Check if overridden in this instance
			bool bHasOverride = false;
			for (const FScalarParameterValue& SPV : MatInstance->ScalarParameterValues)
			{
				if (SPV.ParameterInfo.Name == ParamInfos[i].Name)
				{
					bHasOverride = true;
					break;
				}
			}
			ParamObj->SetBoolField(TEXT("overridden"), bHasOverride);

			ScalarArray.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
		ResultObj->SetArrayField(TEXT("scalar_parameters"), ScalarArray);
	}

	// --- Vector Parameters ---
	{
		TArray<FMaterialParameterInfo> ParamInfos;
		TArray<FGuid> ParamGuids;
		MatInstance->GetAllVectorParameterInfo(ParamInfos, ParamGuids);

		TArray<TSharedPtr<FJsonValue>> VectorArray;
		for (int32 i = 0; i < ParamInfos.Num(); i++)
		{
			FLinearColor Value = FLinearColor::Black;
			MatInstance->GetVectorParameterValue(ParamInfos[i], Value, true);

			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), ParamInfos[i].Name.ToString());
			ParamObj->SetStringField(TEXT("group"), ParamInfos[i].Association == EMaterialParameterAssociation::GlobalParameter ? TEXT("Global") : TEXT("Layer"));

			TArray<TSharedPtr<FJsonValue>> ColorValues;
			ColorValues.Add(MakeShared<FJsonValueNumber>(Value.R));
			ColorValues.Add(MakeShared<FJsonValueNumber>(Value.G));
			ColorValues.Add(MakeShared<FJsonValueNumber>(Value.B));
			ColorValues.Add(MakeShared<FJsonValueNumber>(Value.A));
			ParamObj->SetArrayField(TEXT("value"), ColorValues);

			bool bHasOverride = false;
			for (const FVectorParameterValue& VPV : MatInstance->VectorParameterValues)
			{
				if (VPV.ParameterInfo.Name == ParamInfos[i].Name)
				{
					bHasOverride = true;
					break;
				}
			}
			ParamObj->SetBoolField(TEXT("overridden"), bHasOverride);

			VectorArray.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
		ResultObj->SetArrayField(TEXT("vector_parameters"), VectorArray);
	}

	// --- Texture Parameters ---
	{
		TArray<FMaterialParameterInfo> ParamInfos;
		TArray<FGuid> ParamGuids;
		MatInstance->GetAllTextureParameterInfo(ParamInfos, ParamGuids);

		TArray<TSharedPtr<FJsonValue>> TextureArray;
		for (int32 i = 0; i < ParamInfos.Num(); i++)
		{
			UTexture* TexValue = nullptr;
			MatInstance->GetTextureParameterValue(ParamInfos[i], TexValue, true);

			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), ParamInfos[i].Name.ToString());
			ParamObj->SetStringField(TEXT("group"), ParamInfos[i].Association == EMaterialParameterAssociation::GlobalParameter ? TEXT("Global") : TEXT("Layer"));
			ParamObj->SetStringField(TEXT("value"), TexValue ? TexValue->GetPathName() : TEXT("None"));

			bool bHasOverride = false;
			for (const FTextureParameterValue& TPV : MatInstance->TextureParameterValues)
			{
				if (TPV.ParameterInfo.Name == ParamInfos[i].Name)
				{
					bHasOverride = true;
					break;
				}
			}
			ParamObj->SetBoolField(TEXT("overridden"), bHasOverride);

			TextureArray.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
		ResultObj->SetArrayField(TEXT("texture_parameters"), TextureArray);
	}

	// --- Static Switch Parameters (MIC only) ---
	{
		UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(MatInstance);
		if (MIC)
		{
			TArray<TSharedPtr<FJsonValue>> SwitchArray;

			// In UE5.7, StaticSwitchParameters live on FStaticParameterSetRuntimeData (base of FStaticParameterSet)
			const FStaticParameterSet& StaticParams = MIC->GetStaticParameters();
			for (const FStaticSwitchParameter& SwitchParam : StaticParams.StaticSwitchParameters)
			{
				TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
				ParamObj->SetStringField(TEXT("name"), SwitchParam.ParameterInfo.Name.ToString());
				ParamObj->SetBoolField(TEXT("value"), SwitchParam.Value);
				ParamObj->SetBoolField(TEXT("overridden"), SwitchParam.bOverride);
				SwitchArray.Add(MakeShared<FJsonValueObject>(ParamObj));
			}
			ResultObj->SetArrayField(TEXT("static_switch_parameters"), SwitchArray);

#if WITH_EDITORONLY_DATA
			// Static Component Mask Parameters are editor-only
			TArray<TSharedPtr<FJsonValue>> MaskArray;
			for (const FStaticComponentMaskParameter& MaskParam : StaticParams.EditorOnly.StaticComponentMaskParameters)
			{
				TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
				ParamObj->SetStringField(TEXT("name"), MaskParam.ParameterInfo.Name.ToString());
				ParamObj->SetBoolField(TEXT("r"), MaskParam.R);
				ParamObj->SetBoolField(TEXT("g"), MaskParam.G);
				ParamObj->SetBoolField(TEXT("b"), MaskParam.B);
				ParamObj->SetBoolField(TEXT("a"), MaskParam.A);
				ParamObj->SetBoolField(TEXT("overridden"), MaskParam.bOverride);
				MaskArray.Add(MakeShared<FJsonValueObject>(ParamObj));
			}
			ResultObj->SetArrayField(TEXT("static_component_mask_parameters"), MaskArray);
#endif
		}
	}

	// --- Runtime Virtual Texture Parameters ---
	{
		TArray<FMaterialParameterInfo> ParamInfos;
		TArray<FGuid> ParamGuids;
		MatInstance->GetAllRuntimeVirtualTextureParameterInfo(ParamInfos, ParamGuids);

		TArray<TSharedPtr<FJsonValue>> RVTArray;
		for (int32 i = 0; i < ParamInfos.Num(); i++)
		{
			URuntimeVirtualTexture* RVTValue = nullptr;
			MatInstance->GetRuntimeVirtualTextureParameterValue(ParamInfos[i], RVTValue, true);

			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), ParamInfos[i].Name.ToString());
			ParamObj->SetStringField(TEXT("value"), RVTValue ? RVTValue->GetPathName() : TEXT("None"));
			RVTArray.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
		ResultObj->SetArrayField(TEXT("runtime_virtual_texture_parameters"), RVTArray);
	}

	return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetMaterialInstanceParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
	}

	// Load the asset
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(MaterialPath);
	if (!LoadedAsset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *MaterialPath));
	}

	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(LoadedAsset);
	if (!MIC)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a MaterialInstanceConstant: %s. Only MICs support editor parameter setting."), *MaterialPath));
	}

	TArray<TSharedPtr<FJsonValue>> ErrorsArray;
	int32 ScalarsSet = 0;
	int32 VectorsSet = 0;
	int32 TexturesSet = 0;
	int32 StaticSwitchesSet = 0;
	bool bNeedPostEditChange = false;

	// --- Scalar Parameters ---
	const TArray<TSharedPtr<FJsonValue>>* ScalarParams;
	if (Params->TryGetArrayField(TEXT("scalar_parameters"), ScalarParams))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *ScalarParams)
		{
			const TSharedPtr<FJsonObject>* EntryObj;
			if (!Entry->TryGetObject(EntryObj))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(TEXT("scalar_parameters: entry is not an object")));
				continue;
			}

			FString ParamName;
			if (!(*EntryObj)->TryGetStringField(TEXT("name"), ParamName))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(TEXT("scalar_parameters: entry missing 'name'")));
				continue;
			}

			double Value;
			if (!(*EntryObj)->TryGetNumberField(TEXT("value"), Value))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("scalar_parameters: '%s' missing 'value'"), *ParamName)));
				continue;
			}

			FMaterialParameterInfo ParamInfo;
			ParamInfo.Name = FName(*ParamName);
			MIC->SetScalarParameterValueEditorOnly(ParamInfo, static_cast<float>(Value));
			ScalarsSet++;
			bNeedPostEditChange = true;
		}
	}

	// --- Vector Parameters ---
	const TArray<TSharedPtr<FJsonValue>>* VectorParams;
	if (Params->TryGetArrayField(TEXT("vector_parameters"), VectorParams))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *VectorParams)
		{
			const TSharedPtr<FJsonObject>* EntryObj;
			if (!Entry->TryGetObject(EntryObj))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(TEXT("vector_parameters: entry is not an object")));
				continue;
			}

			FString ParamName;
			if (!(*EntryObj)->TryGetStringField(TEXT("name"), ParamName))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(TEXT("vector_parameters: entry missing 'name'")));
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* ColorArray;
			if (!(*EntryObj)->TryGetArrayField(TEXT("value"), ColorArray) || ColorArray->Num() < 3)
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("vector_parameters: '%s' missing or invalid 'value' (need array of 3-4 floats)"), *ParamName)));
				continue;
			}

			float R = static_cast<float>((*ColorArray)[0]->AsNumber());
			float G = static_cast<float>((*ColorArray)[1]->AsNumber());
			float B = static_cast<float>((*ColorArray)[2]->AsNumber());
			float A = ColorArray->Num() >= 4 ? static_cast<float>((*ColorArray)[3]->AsNumber()) : 1.0f;

			FMaterialParameterInfo ParamInfo;
			ParamInfo.Name = FName(*ParamName);
			MIC->SetVectorParameterValueEditorOnly(ParamInfo, FLinearColor(R, G, B, A));
			VectorsSet++;
			bNeedPostEditChange = true;
		}
	}

	// --- Texture Parameters ---
	const TArray<TSharedPtr<FJsonValue>>* TextureParams;
	if (Params->TryGetArrayField(TEXT("texture_parameters"), TextureParams))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *TextureParams)
		{
			const TSharedPtr<FJsonObject>* EntryObj;
			if (!Entry->TryGetObject(EntryObj))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(TEXT("texture_parameters: entry is not an object")));
				continue;
			}

			FString ParamName;
			if (!(*EntryObj)->TryGetStringField(TEXT("name"), ParamName))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(TEXT("texture_parameters: entry missing 'name'")));
				continue;
			}

			FString TexturePath;
			if (!(*EntryObj)->TryGetStringField(TEXT("value"), TexturePath))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("texture_parameters: '%s' missing 'value'"), *ParamName)));
				continue;
			}

			UTexture* Texture = nullptr;
			if (!TexturePath.IsEmpty() && TexturePath != TEXT("None"))
			{
				UObject* TexAsset = UEditorAssetLibrary::LoadAsset(TexturePath);
				Texture = Cast<UTexture>(TexAsset);
				if (!Texture)
				{
					ErrorsArray.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("texture_parameters: '%s' failed to load texture at '%s'"), *ParamName, *TexturePath)));
					continue;
				}
			}

			FMaterialParameterInfo ParamInfo;
			ParamInfo.Name = FName(*ParamName);
			MIC->SetTextureParameterValueEditorOnly(ParamInfo, Texture);
			TexturesSet++;
			bNeedPostEditChange = true;
		}
	}

	// Propagate dynamic parameter changes
	if (bNeedPostEditChange)
	{
		MIC->PostEditChange();
	}

	// --- Static Switch Parameters (separate update path) ---
	const TArray<TSharedPtr<FJsonValue>>* SwitchParams;
	if (Params->TryGetArrayField(TEXT("static_switch_parameters"), SwitchParams) && SwitchParams->Num() > 0)
	{
		FStaticParameterSet StaticParams = MIC->GetStaticParameters();

		for (const TSharedPtr<FJsonValue>& Entry : *SwitchParams)
		{
			const TSharedPtr<FJsonObject>* EntryObj;
			if (!Entry->TryGetObject(EntryObj))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(TEXT("static_switch_parameters: entry is not an object")));
				continue;
			}

			FString ParamName;
			if (!(*EntryObj)->TryGetStringField(TEXT("name"), ParamName))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(TEXT("static_switch_parameters: entry missing 'name'")));
				continue;
			}

			bool Value;
			if (!(*EntryObj)->TryGetBoolField(TEXT("value"), Value))
			{
				ErrorsArray.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("static_switch_parameters: '%s' missing 'value'"), *ParamName)));
				continue;
			}

			// Find existing or add new entry
			bool bFound = false;
			for (FStaticSwitchParameter& SwitchParam : StaticParams.StaticSwitchParameters)
			{
				if (SwitchParam.ParameterInfo.Name == FName(*ParamName))
				{
					SwitchParam.Value = Value;
					SwitchParam.bOverride = true;
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				FStaticSwitchParameter NewParam;
				NewParam.ParameterInfo.Name = FName(*ParamName);
				NewParam.Value = Value;
				NewParam.bOverride = true;
				StaticParams.StaticSwitchParameters.Add(NewParam);
			}

			StaticSwitchesSet++;
		}

		MIC->UpdateStaticPermutation(StaticParams);
	}

	MIC->MarkPackageDirty();

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
	ResultObj->SetNumberField(TEXT("scalars_set"), ScalarsSet);
	ResultObj->SetNumberField(TEXT("vectors_set"), VectorsSet);
	ResultObj->SetNumberField(TEXT("textures_set"), TexturesSet);
	ResultObj->SetNumberField(TEXT("static_switches_set"), StaticSwitchesSet);
	ResultObj->SetArrayField(TEXT("errors"), ErrorsArray);
	return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleRemoveComponentFromBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint has no SimpleConstructionScript"));
    }

    USCS_Node* NodeToRemove = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            NodeToRemove = Node;
            break;
        }
    }

    if (!NodeToRemove)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    // RemoveNode handles reparenting children of the removed node under the
    // removed node's parent, and preserves SCS consistency.
    Blueprint->SimpleConstructionScript->RemoveNodeAndPromoteChildren(NodeToRemove);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    return ResultObj;
}

// ============================================================================
// reparent_blueprint_component — move an EXISTING SCS component under a new
// parent (SCS node or native CDO component; empty new_parent => root). The
// non-destructive complement to add_blueprint_component: SCS node-graph editing
// is editor C++ and not Python-reachable, so this is the only reliable path to
// restructure a blueprint's component hierarchy (e.g. reparent a door mesh under
// a hinge-pivot scene component). Optional location/rotation/scale set the
// post-reparent RELATIVE transform (its meaning changes under the new parent).
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleReparentBlueprintComponent(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }
    // Empty / omitted new_parent_component => reparent to the SCS root.
    FString NewParent;
    Params->TryGetStringField(TEXT("new_parent_component"), NewParent);

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (!SCS)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint has no SimpleConstructionScript: %s"), *BlueprintName));
    }

    // Find the child node to move.
    USCS_Node* ChildNode = nullptr;
    for (USCS_Node* Node : SCS->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ChildNode = Node;
            break;
        }
    }
    if (!ChildNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    USceneComponent* SceneTemplate = Cast<USceneComponent>(ChildNode->ComponentTemplate);
    if (!SceneTemplate)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Component '%s' is not a scene component and cannot be reparented"), *ComponentName));
    }

    // Resolve the new parent: 1) an SCS node, 2) a native CDO scene component, 3) root.
    USCS_Node* NewParentNode = nullptr;
    USceneComponent* NativeParent = nullptr;
    if (!NewParent.IsEmpty())
    {
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (Node && Node->GetVariableName().ToString() == NewParent)
            {
                NewParentNode = Node;
                break;
            }
        }

        if (NewParentNode == ChildNode)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot parent a component to itself"));
        }

        if (!NewParentNode)
        {
            UClass* GenClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->SkeletonGeneratedClass.Get();
            if (AActor* CDO = GenClass ? Cast<AActor>(GenClass->GetDefaultObject()) : nullptr)
            {
                TInlineComponentArray<USceneComponent*> SceneComps(CDO);
                for (USceneComponent* Comp : SceneComps)
                {
                    if (Comp && Comp->GetFName().ToString() == NewParent)
                    {
                        NativeParent = Comp;
                        break;
                    }
                }
            }
            if (!NativeParent)
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("new_parent_component '%s' not found (no SCS node or native component)"), *NewParent));
            }
        }

        // Cycle guard: the new parent must not be a descendant of the moved node.
        for (USCS_Node* Walk = NewParentNode ? SCS->FindParentNode(NewParentNode) : nullptr; Walk; Walk = SCS->FindParentNode(Walk))
        {
            if (Walk == ChildNode)
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    TEXT("Cannot reparent: target parent is a descendant of the component (would create a cycle)"));
            }
        }
    }

    // Detach from its current location (keeps ChildNode's own subtree intact).
    USCS_Node* OldParent = SCS->FindParentNode(ChildNode);
    if (OldParent)
    {
        OldParent->RemoveChildNode(ChildNode, /*bRemoveFromAllNodes=*/true);
    }
    else
    {
        SCS->RemoveNode(ChildNode); // was a root node
    }

    // Re-attach under the resolved parent (re-registers in AllNodes).
    FString ResolvedParent;
    if (NewParentNode)
    {
        NewParentNode->AddChildNode(ChildNode);
        ResolvedParent = NewParent;
    }
    else if (NativeParent)
    {
        SCS->AddNode(ChildNode);
        ChildNode->SetParent(NativeParent);
        ResolvedParent = NewParent;
    }
    else
    {
        SCS->AddNode(ChildNode); // root
    }

    // Optional post-reparent relative transform.
    if (Params->HasField(TEXT("location")))
        SceneTemplate->SetRelativeLocation(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
    if (Params->HasField(TEXT("rotation")))
        SceneTemplate->SetRelativeRotation(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
    if (Params->HasField(TEXT("scale")))
        SceneTemplate->SetRelativeScale3D(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    ResultObj->SetStringField(TEXT("parent"), ResolvedParent);
    {
        const FVector Loc = SceneTemplate->GetRelativeLocation();
        const FRotator Rot = SceneTemplate->GetRelativeRotation();
        const FVector Scl = SceneTemplate->GetRelativeScale3D();
        TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
        LocObj->SetNumberField(TEXT("x"), Loc.X); LocObj->SetNumberField(TEXT("y"), Loc.Y); LocObj->SetNumberField(TEXT("z"), Loc.Z);
        Transform->SetObjectField(TEXT("location"), LocObj);
        TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
        RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch); RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw); RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
        Transform->SetObjectField(TEXT("rotation"), RotObj);
        TSharedPtr<FJsonObject> SclObj = MakeShared<FJsonObject>();
        SclObj->SetNumberField(TEXT("x"), Scl.X); SclObj->SetNumberField(TEXT("y"), Scl.Y); SclObj->SetNumberField(TEXT("z"), Scl.Z);
        Transform->SetObjectField(TEXT("scale"), SclObj);
        ResultObj->SetObjectField(TEXT("transform"), Transform);
    }
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintComponents(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    TArray<TSharedPtr<FJsonValue>> Components;

    if (Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (!Node) continue;

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
            Entry->SetStringField(TEXT("class"),
                Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT(""));

            USCS_Node* Parent = Blueprint->SimpleConstructionScript->FindParentNode(Node);
            if (Parent)
            {
                Entry->SetStringField(TEXT("parent"), Parent->GetVariableName().ToString());
            }
            else
            {
                Entry->SetStringField(TEXT("parent"), TEXT(""));
            }

            if (USceneComponent* SceneTemplate = Cast<USceneComponent>(Node->ComponentTemplate))
            {
                const FVector Loc = SceneTemplate->GetRelativeLocation();
                const FRotator Rot = SceneTemplate->GetRelativeRotation();
                const FVector Scl = SceneTemplate->GetRelativeScale3D();

                TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
                TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
                LocObj->SetNumberField(TEXT("x"), Loc.X);
                LocObj->SetNumberField(TEXT("y"), Loc.Y);
                LocObj->SetNumberField(TEXT("z"), Loc.Z);
                Transform->SetObjectField(TEXT("location"), LocObj);

                TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
                RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
                RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
                RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
                Transform->SetObjectField(TEXT("rotation"), RotObj);

                TSharedPtr<FJsonObject> SclObj = MakeShared<FJsonObject>();
                SclObj->SetNumberField(TEXT("x"), Scl.X);
                SclObj->SetNumberField(TEXT("y"), Scl.Y);
                SclObj->SetNumberField(TEXT("z"), Scl.Z);
                Transform->SetObjectField(TEXT("scale"), SclObj);

                Entry->SetObjectField(TEXT("transform"), Transform);
            }

            Components.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetArrayField(TEXT("components"), Components);
    ResultObj->SetNumberField(TEXT("count"), Components.Num());
    return ResultObj;
}

// ============================================================================
// list_blueprint_components — COMPLETE component enumeration: native (C++ ctor)
// AND SCS components, including SCS inherited from parent blueprints, each with
// class, mesh asset, relative transform, and attach parent. The older
// get_blueprint_components returns SCS-only with no native components and no
// mesh — this is the reliable replacement for the SubobjectDataSubsystem dance.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleListBlueprintComponents(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Serialize a scene component's relative transform.
    auto AddSceneTransform = [](const TSharedPtr<FJsonObject>& Entry, const USceneComponent* SceneComp)
    {
        const FVector Loc = SceneComp->GetRelativeLocation();
        const FRotator Rot = SceneComp->GetRelativeRotation();
        const FVector Scl = SceneComp->GetRelativeScale3D();

        TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
        LocObj->SetNumberField(TEXT("x"), Loc.X); LocObj->SetNumberField(TEXT("y"), Loc.Y); LocObj->SetNumberField(TEXT("z"), Loc.Z);
        Transform->SetObjectField(TEXT("location"), LocObj);
        TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
        RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch); RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw); RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
        Transform->SetObjectField(TEXT("rotation"), RotObj);
        TSharedPtr<FJsonObject> SclObj = MakeShared<FJsonObject>();
        SclObj->SetNumberField(TEXT("x"), Scl.X); SclObj->SetNumberField(TEXT("y"), Scl.Y); SclObj->SetNumberField(TEXT("z"), Scl.Z);
        Transform->SetObjectField(TEXT("scale"), SclObj);
        Entry->SetObjectField(TEXT("transform"), Transform);
    };

    // Resolve a component's mesh asset path (static or skeletal), if any.
    auto AddMesh = [](const TSharedPtr<FJsonObject>& Entry, const UActorComponent* Comp)
    {
        if (const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp))
        {
            if (SMC->GetStaticMesh()) Entry->SetStringField(TEXT("mesh"), SMC->GetStaticMesh()->GetPathName());
        }
        else if (const USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(Comp))
        {
            if (SKC->GetSkeletalMeshAsset()) Entry->SetStringField(TEXT("mesh"), SKC->GetSkeletalMeshAsset()->GetPathName());
        }
    };

    TArray<TSharedPtr<FJsonValue>> Components;
    int32 NativeCount = 0;
    int32 ScsCount = 0;

    // --- Native components: live as default subobjects on the CDO (SCS components do not). ---
    UClass* GenClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->SkeletonGeneratedClass.Get();
    if (AActor* CDO = GenClass ? Cast<AActor>(GenClass->GetDefaultObject()) : nullptr)
    {
        TInlineComponentArray<UActorComponent*> NativeComps(CDO);
        for (UActorComponent* Comp : NativeComps)
        {
            if (!Comp) continue;

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Comp->GetFName().ToString());
            Entry->SetStringField(TEXT("class"), Comp->GetClass() ? Comp->GetClass()->GetName() : TEXT(""));
            Entry->SetStringField(TEXT("source"), TEXT("native"));
            AddMesh(Entry, Comp);

            if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
            {
                AddSceneTransform(Entry, SceneComp);
                Entry->SetStringField(TEXT("parent"),
                    SceneComp->GetAttachParent() ? SceneComp->GetAttachParent()->GetFName().ToString() : TEXT(""));
            }
            else
            {
                Entry->SetStringField(TEXT("parent"), TEXT(""));
            }

            Components.Add(MakeShared<FJsonValueObject>(Entry));
            ++NativeCount;
        }
    }

    // --- SCS components: this blueprint plus every parent blueprint up the chain. ---
    UClass* Cur = Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : nullptr;
    bool bIsLeaf = true;
    while (Cur)
    {
        UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Cur);
        USimpleConstructionScript* SCS = BPGC ? BPGC->SimpleConstructionScript : nullptr;
        if (SCS)
        {
            const FString SourceTag = bIsLeaf ? TEXT("scs") : TEXT("inherited_scs");
            for (USCS_Node* Node : SCS->GetAllNodes())
            {
                if (!Node) continue;

                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
                Entry->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT(""));
                Entry->SetStringField(TEXT("source"), SourceTag);
                AddMesh(Entry, Node->ComponentTemplate);

                // Parent: an SCS sibling node, else a native component (root SCS node attaching to the native root).
                if (USCS_Node* Parent = SCS->FindParentNode(Node))
                {
                    Entry->SetStringField(TEXT("parent"), Parent->GetVariableName().ToString());
                }
                else if (Node->ParentComponentOrVariableName != NAME_None)
                {
                    Entry->SetStringField(TEXT("parent"), Node->ParentComponentOrVariableName.ToString());
                }
                else
                {
                    Entry->SetStringField(TEXT("parent"), TEXT(""));
                }

                if (USceneComponent* SceneTemplate = Cast<USceneComponent>(Node->ComponentTemplate))
                {
                    AddSceneTransform(Entry, SceneTemplate);
                }

                Components.Add(MakeShared<FJsonValueObject>(Entry));
                ++ScsCount;
            }
        }
        Cur = Cur->GetSuperClass();
        bIsLeaf = false;
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetArrayField(TEXT("components"), Components);
    ResultObj->SetNumberField(TEXT("count"), Components.Num());
    ResultObj->SetNumberField(TEXT("native_count"), NativeCount);
    ResultObj->SetNumberField(TEXT("scs_count"), ScsCount);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSaveBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    const bool bOnlyIfDirty = Params->HasField(TEXT("only_if_dirty"))
        ? Params->GetBoolField(TEXT("only_if_dirty")) : true;

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UPackage* Package = Blueprint->GetOutermost();
    const FString PackageName = Package ? Package->GetName() : FString();

    const bool bDirty = Package ? Package->IsDirty() : false;
    if (bOnlyIfDirty && !bDirty)
    {
        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetBoolField(TEXT("success"), true);
        ResultObj->SetBoolField(TEXT("saved"), false);
        ResultObj->SetStringField(TEXT("reason"), TEXT("not dirty"));
        ResultObj->SetStringField(TEXT("package"), PackageName);
        return ResultObj;
    }

    const bool bSaved = UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), bSaved);
    ResultObj->SetBoolField(TEXT("saved"), bSaved);
    ResultObj->SetStringField(TEXT("package"), PackageName);
    if (!bSaved)
    {
        ResultObj->SetStringField(TEXT("error"), TEXT("SaveAsset returned false"));
    }
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleListBlueprints(const TSharedPtr<FJsonObject>& Params)
{
    FString PathFilter;
    Params->TryGetStringField(TEXT("path"), PathFilter);
    if (PathFilter.IsEmpty())
    {
        PathFilter = TEXT("/Game");
    }

    bool bRecursive = true;
    if (Params->HasField(TEXT("recursive")))
    {
        bRecursive = Params->GetBoolField(TEXT("recursive"));
    }

    FString NameContains;
    Params->TryGetStringField(TEXT("name_contains"), NameContains);

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

    FARFilter Filter;
    Filter.bRecursivePaths = bRecursive;
    Filter.bRecursiveClasses = true;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.PackagePaths.Add(FName(*PathFilter));

    TArray<FAssetData> AssetList;
    AssetRegistryModule.Get().GetAssets(Filter, AssetList);

    TArray<TSharedPtr<FJsonValue>> Results;
    for (const FAssetData& Asset : AssetList)
    {
        const FString AssetName = Asset.AssetName.ToString();
        if (!NameContains.IsEmpty() && !AssetName.Contains(NameContains))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), AssetName);
        Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
        Entry->SetStringField(TEXT("package"), Asset.PackageName.ToString());

        FString ParentClass;
        if (Asset.GetTagValue(FBlueprintTags::ParentClassPath, ParentClass))
        {
            Entry->SetStringField(TEXT("parent_class"), ParentClass);
        }

        Results.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("path"), PathFilter);
    ResultObj->SetNumberField(TEXT("count"), Results.Num());
    ResultObj->SetArrayField(TEXT("blueprints"), Results);
    return ResultObj;
}

// ============================================================================
// Blueprint CDO default-value editing
// ============================================================================
//
// Edits the Class Default Object on the Blueprint's GeneratedClass. For a
// Blueprint subclass of a native UObject, this is how you override the native
// constructor's default for UPROPERTY members (weapon assets, state tree refs,
// etc.) without touching C++.
//
// After mutation, we mark the Blueprint structurally modified AND dirty the
// the generated-class package so the change persists on save.

namespace
{
	// Resolve to a loaded UBlueprint, accepting either a short name or a full package path.
	UBlueprint* LoadBlueprintForDefaults(const FString& NameOrPath, FString& OutError)
	{
		if (NameOrPath.IsEmpty())
		{
			OutError = TEXT("blueprint_name is empty");
			return nullptr;
		}
		if (UBlueprint* BP = FEpicUnrealMCPCommonUtils::FindBlueprint(NameOrPath))
		{
			return BP;
		}
		if (NameOrPath.StartsWith(TEXT("/")))
		{
			if (UObject* Loaded = UEditorAssetLibrary::LoadAsset(NameOrPath))
			{
				if (UBlueprint* BP = Cast<UBlueprint>(Loaded))
				{
					return BP;
				}
				OutError = FString::Printf(TEXT("Asset '%s' is not a UBlueprint"), *NameOrPath);
				return nullptr;
			}
		}
		OutError = FString::Printf(TEXT("Could not find blueprint '%s'"), *NameOrPath);
		return nullptr;
	}

	UObject* GetBlueprintCDO(UBlueprint* Blueprint, FString& OutError)
	{
		if (!Blueprint) { OutError = TEXT("Blueprint is null"); return nullptr; }
		UClass* GeneratedClass = Blueprint->GeneratedClass;
		if (!GeneratedClass)
		{
			OutError = FString::Printf(TEXT("Blueprint '%s' has no GeneratedClass (needs compile?)"),
				*Blueprint->GetName());
			return nullptr;
		}
		UObject* CDO = GeneratedClass->GetDefaultObject();
		if (!CDO)
		{
			OutError = FString::Printf(TEXT("Blueprint '%s' GeneratedClass has no CDO"),
				*Blueprint->GetName());
			return nullptr;
		}
		return CDO;
	}
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintDefaultValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("blueprint_name")) || !Params->HasField(TEXT("property_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): blueprint_name, property_name"));
	}
	FString Err;
	UBlueprint* Blueprint = LoadBlueprintForDefaults(Params->GetStringField(TEXT("blueprint_name")), Err);
	if (!Blueprint) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UObject* CDO = GetBlueprintCDO(Blueprint, Err);
	if (!CDO) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FString PropertyName = Params->GetStringField(TEXT("property_name"));
	FProperty* Prop = CDO->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Property '%s' not found on class '%s'"),
				*PropertyName, *CDO->GetClass()->GetName()));
	}

	// Export the property value to a string (matches the editor's text representation).
	FString ValueStr;
	Prop->ExportText_InContainer(0, ValueStr, CDO, CDO, CDO, PPF_None);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("property_type"), Prop->GetCPPType());
	Result->SetStringField(TEXT("value"), ValueStr);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetBlueprintDefaultValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("blueprint_name")) || !Params->HasField(TEXT("property_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): blueprint_name, property_name"));
	}
	if (!Params->HasField(TEXT("value")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: value"));
	}
	FString Err;
	UBlueprint* Blueprint = LoadBlueprintForDefaults(Params->GetStringField(TEXT("blueprint_name")), Err);
	if (!Blueprint) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UObject* CDO = GetBlueprintCDO(Blueprint, Err);
	if (!CDO) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FString PropertyName = Params->GetStringField(TEXT("property_name"));
	FProperty* Prop = CDO->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Property '%s' not found on class '%s'"),
				*PropertyName, *CDO->GetClass()->GetName()));
	}

	Blueprint->Modify();
	CDO->Modify();
	CDO->UObject::PreEditChange(Prop);

	FString SetErr;
	const bool bOk = FEpicUnrealMCPCommonUtils::SetObjectProperty(
		CDO, PropertyName, Params->TryGetField(TEXT("value")), SetErr);

	FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
	CDO->PostEditChangeProperty(Evt);

	if (!bOk)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->GetOutermost()->MarkPackageDirty();
	if (UClass* GenClass = Blueprint->GeneratedClass)
	{
		if (UPackage* GenPkg = GenClass->GetOutermost()) { GenPkg->MarkPackageDirty(); }
	}

	const bool bRecompile = !Params->HasField(TEXT("recompile")) || Params->GetBoolField(TEXT("recompile"));
	if (bRecompile)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	// Read back the applied value for the response.
	FString ValueStr;
	Prop->ExportText_InContainer(0, ValueStr, CDO, CDO, CDO, PPF_None);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("value"), ValueStr);
	Result->SetBoolField(TEXT("recompiled"), bRecompile);
	// Surface partial-failure warnings from nested struct/array dispatchers.
	if (!SetErr.IsEmpty())
	{
		Result->SetStringField(TEXT("warning"), SetErr);
	}
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleResetBlueprintDefaultValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("blueprint_name")) || !Params->HasField(TEXT("property_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): blueprint_name, property_name"));
	}
	FString Err;
	UBlueprint* Blueprint = LoadBlueprintForDefaults(Params->GetStringField(TEXT("blueprint_name")), Err);
	if (!Blueprint) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }
	UObject* CDO = GetBlueprintCDO(Blueprint, Err);
	if (!CDO) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FString PropertyName = Params->GetStringField(TEXT("property_name"));
	FProperty* Prop = CDO->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Property '%s' not found on class '%s'"),
				*PropertyName, *CDO->GetClass()->GetName()));
	}

	// Look up the parent class's default to copy from.
	UClass* ParentClass = CDO->GetClass()->GetSuperClass();
	if (!ParentClass)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No parent class to reset from"));
	}
	UObject* ParentCDO = ParentClass->GetDefaultObject();

	Blueprint->Modify();
	CDO->Modify();
	CDO->UObject::PreEditChange(Prop);
	Prop->CopyCompleteValue_InContainer(CDO, ParentCDO);
	FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
	CDO->PostEditChangeProperty(Evt);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->GetOutermost()->MarkPackageDirty();

	const bool bRecompile = !Params->HasField(TEXT("recompile")) || Params->GetBoolField(TEXT("recompile"));
	if (bRecompile)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	FString ValueStr;
	Prop->ExportText_InContainer(0, ValueStr, CDO, CDO, CDO, PPF_None);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("value"), ValueStr);
	Result->SetBoolField(TEXT("recompiled"), bRecompile);
	return Result;
}

// ============================================================================
// Component archetype default-value editing
// ============================================================================
//
// Edits properties on a component template/archetype owned by a Blueprint:
//   - Inherited C++ default subobjects (CreateDefaultSubobject in the parent
//     class constructor): looked up via UObject::GetDefaultSubobjectByName on
//     the BP CDO. The archetype lives at <BP_CDO>:<SubobjectName>.
//   - SCS-added components: looked up via Blueprint->SimpleConstructionScript
//     ->FindSCSNode(Name)->GetActualComponentTemplate(GeneratedClass).
//
// This mirrors the editor's "Inherited Components" details panel, which is the
// only way to override array/struct properties on a default subobject without
// hand-editing the .uasset.

namespace
{
	// Resolve the component template/archetype on a Blueprint's CDO by name.
	// Handles both inherited C++ default subobjects and SCS-added components.
	UActorComponent* FindBlueprintComponentArchetype(UBlueprint* Blueprint, const FString& ComponentName, FString& OutError)
	{
		if (!Blueprint)
		{
			OutError = TEXT("Blueprint is null");
			return nullptr;
		}
		UClass* GeneratedClass = Blueprint->GeneratedClass;
		if (!GeneratedClass)
		{
			OutError = TEXT("Blueprint has no GeneratedClass (needs compile?)");
			return nullptr;
		}
		UObject* CDO = GeneratedClass->GetDefaultObject();
		if (!CDO)
		{
			OutError = TEXT("Blueprint GeneratedClass has no CDO");
			return nullptr;
		}

		// 1) Inherited C++ default subobject: created in a parent's constructor.
		if (UObject* SubObj = CDO->GetDefaultSubobjectByName(*ComponentName))
		{
			if (UActorComponent* Comp = Cast<UActorComponent>(SubObj))
			{
				return Comp;
			}
		}

		// 2) SCS-added component on this BP or any parent BP in the chain.
		for (UBlueprint* BP = Blueprint; BP; BP = Cast<UBlueprint>(BP->ParentClass ? BP->ParentClass->ClassGeneratedBy : nullptr))
		{
			if (!BP->SimpleConstructionScript) { continue; }
			for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node) { continue; }
				const FName NodeVarName = Node->GetVariableName();
				if (NodeVarName.IsNone()) { continue; }
				if (NodeVarName.ToString() == ComponentName)
				{
					if (UActorComponent* Template = Node->GetActualComponentTemplate(Cast<UBlueprintGeneratedClass>(GeneratedClass)))
					{
						return Template;
					}
					if (UActorComponent* Template = Node->ComponentTemplate)
					{
						return Template;
					}
				}
			}
		}

		OutError = FString::Printf(TEXT("Component '%s' not found on Blueprint '%s' (checked inherited subobjects + SCS chain)"),
			*ComponentName, *Blueprint->GetName());
		return nullptr;
	}
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetComponentDefaultValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("blueprint_name")) || !Params->HasField(TEXT("component_name")) || !Params->HasField(TEXT("property_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): blueprint_name, component_name, property_name"));
	}
	FString Err;
	UBlueprint* Blueprint = LoadBlueprintForDefaults(Params->GetStringField(TEXT("blueprint_name")), Err);
	if (!Blueprint) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FString ComponentName = Params->GetStringField(TEXT("component_name"));
	UActorComponent* Archetype = FindBlueprintComponentArchetype(Blueprint, ComponentName, Err);
	if (!Archetype) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FString PropertyName = Params->GetStringField(TEXT("property_name"));
	FProperty* Prop = Archetype->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Property '%s' not found on component class '%s'"),
				*PropertyName, *Archetype->GetClass()->GetName()));
	}

	FString ValueStr;
	Prop->ExportText_InContainer(0, ValueStr, Archetype, Archetype, Archetype, PPF_None);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("component_name"), ComponentName);
	Result->SetStringField(TEXT("component_class"), Archetype->GetClass()->GetName());
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("property_type"), Prop->GetCPPType());
	Result->SetStringField(TEXT("value"), ValueStr);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetComponentDefaultValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("blueprint_name")) || !Params->HasField(TEXT("component_name")) || !Params->HasField(TEXT("property_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): blueprint_name, component_name, property_name"));
	}
	if (!Params->HasField(TEXT("value")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: value"));
	}
	FString Err;
	UBlueprint* Blueprint = LoadBlueprintForDefaults(Params->GetStringField(TEXT("blueprint_name")), Err);
	if (!Blueprint) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FString ComponentName = Params->GetStringField(TEXT("component_name"));
	UActorComponent* Archetype = FindBlueprintComponentArchetype(Blueprint, ComponentName, Err);
	if (!Archetype) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FString PropertyName = Params->GetStringField(TEXT("property_name"));
	FProperty* Prop = Archetype->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Property '%s' not found on component class '%s'"),
				*PropertyName, *Archetype->GetClass()->GetName()));
	}

	Blueprint->Modify();
	Archetype->Modify();
	Archetype->UObject::PreEditChange(Prop);

	FString SetErr;
	const bool bOk = FEpicUnrealMCPCommonUtils::SetObjectProperty(
		Archetype, PropertyName, Params->TryGetField(TEXT("value")), SetErr);

	FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
	Archetype->PostEditChangeProperty(Evt);

	if (!bOk)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to set '%s' on component '%s': %s"), *PropertyName, *ComponentName, *SetErr));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->GetOutermost()->MarkPackageDirty();
	if (UClass* GenClass = Blueprint->GeneratedClass)
	{
		if (UPackage* GenPkg = GenClass->GetOutermost()) { GenPkg->MarkPackageDirty(); }
	}

	const bool bRecompile = !Params->HasField(TEXT("recompile")) || Params->GetBoolField(TEXT("recompile"));
	if (bRecompile)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	// Re-resolve archetype after compile (CDO may have been re-instanced).
	UActorComponent* PostArchetype = FindBlueprintComponentArchetype(Blueprint, ComponentName, Err);
	UObject* ReadFrom = PostArchetype ? static_cast<UObject*>(PostArchetype) : static_cast<UObject*>(Archetype);
	FProperty* ReadProp = ReadFrom->GetClass()->FindPropertyByName(*PropertyName);
	FString ValueStr;
	if (ReadProp)
	{
		ReadProp->ExportText_InContainer(0, ValueStr, ReadFrom, ReadFrom, ReadFrom, PPF_None);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("component_name"), ComponentName);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("value"), ValueStr);
	Result->SetBoolField(TEXT("recompiled"), bRecompile);
	if (!SetErr.IsEmpty())
	{
		Result->SetStringField(TEXT("warning"), SetErr);
	}
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleResetComponentDefaultValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("blueprint_name")) || !Params->HasField(TEXT("component_name")) || !Params->HasField(TEXT("property_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required field(s): blueprint_name, component_name, property_name"));
	}
	FString Err;
	UBlueprint* Blueprint = LoadBlueprintForDefaults(Params->GetStringField(TEXT("blueprint_name")), Err);
	if (!Blueprint) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FString ComponentName = Params->GetStringField(TEXT("component_name"));
	UActorComponent* Archetype = FindBlueprintComponentArchetype(Blueprint, ComponentName, Err);
	if (!Archetype) { return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err); }

	const FString PropertyName = Params->GetStringField(TEXT("property_name"));
	FProperty* Prop = Archetype->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Property '%s' not found on component class '%s'"),
				*PropertyName, *Archetype->GetClass()->GetName()));
	}

	// Walk to the nearest archetype source: the component's own archetype object
	// (UObject::GetArchetype) is the parent class's template. CopyCompleteValue
	// from there overwrites this BP's override with the inherited default.
	UObject* SourceArchetype = Archetype->GetArchetype();
	if (!SourceArchetype)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No archetype to reset from"));
	}

	Blueprint->Modify();
	Archetype->Modify();
	Archetype->UObject::PreEditChange(Prop);
	Prop->CopyCompleteValue_InContainer(Archetype, SourceArchetype);
	FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
	Archetype->PostEditChangeProperty(Evt);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->GetOutermost()->MarkPackageDirty();

	const bool bRecompile = !Params->HasField(TEXT("recompile")) || Params->GetBoolField(TEXT("recompile"));
	if (bRecompile)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	UActorComponent* PostArchetype = FindBlueprintComponentArchetype(Blueprint, ComponentName, Err);
	UObject* ReadFrom = PostArchetype ? static_cast<UObject*>(PostArchetype) : static_cast<UObject*>(Archetype);
	FProperty* ReadProp = ReadFrom->GetClass()->FindPropertyByName(*PropertyName);
	FString ValueStr;
	if (ReadProp)
	{
		ReadProp->ExportText_InContainer(0, ValueStr, ReadFrom, ReadFrom, ReadFrom, PPF_None);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("component_name"), ComponentName);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("value"), ValueStr);
	Result->SetBoolField(TEXT("recompiled"), bRecompile);
	return Result;
}