#include "EpicUnrealMCPBridge.h"
#include "MCPServerRunnable.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "CoreGlobals.h"
// Add Blueprint related includes
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
// UE5.5 correct includes
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
// Blueprint Graph specific includes
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "GameFramework/InputSettings.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
// Include our new command handler classes
#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPBlueprintGraphCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPThumbnailCommands.h"
#include "Commands/EpicUnrealMCPMaterialCommands.h"
#include "Commands/EpicUnrealMCPRVTCommands.h"
#include "Commands/EpicUnrealMCPAnimationCommands.h"
#include "Commands/EpicUnrealMCPDataAssetCommands.h"
#include "Commands/EpicUnrealMCPDataTableCommands.h"
#include "Commands/EpicUnrealMCPCurveTableCommands.h"
#include "Commands/EpicUnrealMCPCurveAssetCommands.h"
#include "Commands/EpicUnrealMCPMPCCommands.h"
#include "Commands/EpicUnrealMCPSoundCommands.h"
#include "Commands/EpicUnrealMCPWidgetCommands.h"
#include "Commands/EpicUnrealMCPWidgetPreviewCommands.h"
#include "Commands/EpicUnrealMCPWorldCommands.h"
#include "Commands/EpicUnrealMCPStateTreeCommands.h"
#include "Commands/EpicUnrealMCPStaticMeshCommands.h"
#include "Commands/EpicUnrealMCPPythonCommands.h"
#include "Commands/EpicUnrealMCPAssetCommands.h"
#include "Commands/EpicUnrealMCPTextureImportCommands.h"
#include "Commands/EpicUnrealMCPPCGCommands.h"

// Default settings
#define MCP_SERVER_HOST "127.0.0.1"
#define MCP_SERVER_PORT 55557

UEpicUnrealMCPBridge::UEpicUnrealMCPBridge()
{
    EditorCommands = MakeShared<FEpicUnrealMCPEditorCommands>();
    BlueprintCommands = MakeShared<FEpicUnrealMCPBlueprintCommands>();
    BlueprintGraphCommands = MakeShared<FEpicUnrealMCPBlueprintGraphCommands>();
    ThumbnailCommands = MakeShared<FEpicUnrealMCPThumbnailCommands>();
    MaterialCommands = MakeShared<FEpicUnrealMCPMaterialCommands>();
    RVTCommands = MakeShared<FEpicUnrealMCPRVTCommands>();
    AnimationCommands = MakeShared<FEpicUnrealMCPAnimationCommands>();
    DataAssetCommands = MakeShared<FEpicUnrealMCPDataAssetCommands>();
    DataTableCommands = MakeShared<FEpicUnrealMCPDataTableCommands>();
    CurveTableCommands = MakeShared<FEpicUnrealMCPCurveTableCommands>();
    CurveAssetCommands = MakeShared<FEpicUnrealMCPCurveAssetCommands>();
    MPCCommands = MakeShared<FEpicUnrealMCPMPCCommands>();
    SoundCommands = MakeShared<FEpicUnrealMCPSoundCommands>();
    WidgetCommands = MakeShared<FEpicUnrealMCPWidgetCommands>();
    WidgetPreviewCommands = MakeShared<FEpicUnrealMCPWidgetPreviewCommands>();
    WorldCommands = MakeShared<FEpicUnrealMCPWorldCommands>();
    AnimBlueprintCommands = MakeShared<FEpicUnrealMCPAnimBlueprintCommands>();
    StateTreeCommands = MakeShared<FEpicUnrealMCPStateTreeCommands>();
    StaticMeshCommands = MakeShared<FEpicUnrealMCPStaticMeshCommands>();
    PythonCommands = MakeShared<FEpicUnrealMCPPythonCommands>();
    AssetCommands = MakeShared<FEpicUnrealMCPAssetCommands>();
    TextureImportCommands = MakeShared<FEpicUnrealMCPTextureImportCommands>();
    EnumTagCommands = MakeShared<FEpicUnrealMCPEnumTagCommands>();
    PCGCommands = MakeShared<FEpicUnrealMCPPCGCommands>();
}

UEpicUnrealMCPBridge::~UEpicUnrealMCPBridge()
{
    EditorCommands.Reset();
    BlueprintCommands.Reset();
    BlueprintGraphCommands.Reset();
    ThumbnailCommands.Reset();
    MaterialCommands.Reset();
    RVTCommands.Reset();
    AnimationCommands.Reset();
    DataAssetCommands.Reset();
    DataTableCommands.Reset();
    CurveTableCommands.Reset();
    CurveAssetCommands.Reset();
    MPCCommands.Reset();
    SoundCommands.Reset();
    WidgetCommands.Reset();
    WidgetPreviewCommands.Reset();
    WorldCommands.Reset();
    StateTreeCommands.Reset();
    StaticMeshCommands.Reset();
    PythonCommands.Reset();
    AssetCommands.Reset();
    TextureImportCommands.Reset();
    EnumTagCommands.Reset();
    PCGCommands.Reset();
}

// Initialize subsystem
void UEpicUnrealMCPBridge::Initialize(FSubsystemCollectionBase& Collection)
{
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Initializing"));

    bIsRunning = false;
    ListenerSocket = nullptr;
    ConnectionSocket = nullptr;
    ServerThread = nullptr;
    Port = MCP_SERVER_PORT;
    // Allow a second editor instance to serve MCP on its own port:
    //   UnrealEditor.exe <project> -MCPPort=55558
    // Without an override a second editor also binds 55557 (SetReuseAddr lets
    // both listen), and a new client connection may land in either one.
    int32 OverridePort = 0;
    const bool bPortOverride = FParse::Value(FCommandLine::Get(), TEXT("MCPPort="), OverridePort)
        && OverridePort > 0 && OverridePort <= 65535;
    if (bPortOverride)
    {
        Port = static_cast<uint16>(OverridePort);
        UE_LOG(LogTemp, Display,
            TEXT("EpicUnrealMCPBridge: -MCPPort override -> %d"), Port);
    }
    FIPv4Address::Parse(MCP_SERVER_HOST, ServerAddress);

    BuildDispatchTable();

    // Headless processes (commandlets, -unattended automation runs) serve MCP only
    // on an explicit -MCPPort. On the default port they would share 55557 with the
    // interactive editor and capture clients meant for it.
    if (!bPortOverride && (IsRunningCommandlet() || FApp::IsUnattended()))
    {
        UE_LOG(LogTemp, Display,
            TEXT("EpicUnrealMCPBridge: headless process, server not started (pass -MCPPort= to serve)"));
        return;
    }

    // Start the server automatically
    StartServer();
}

void UEpicUnrealMCPBridge::BuildDispatchTable()
{
    Dispatch.Reset();

    auto Register = [this](std::initializer_list<const TCHAR*> Names, auto HandlerPtr)
    {
        auto Handler = HandlerPtr; // captures TSharedPtr by value, keeps handler alive
        for (const TCHAR* Name : Names)
        {
            Dispatch.Add(FName(Name), [Handler](const FString& Cmd, const TSharedPtr<FJsonObject>& Params)
            {
                return Handler->HandleCommand(Cmd, Params);
            });
        }
    };

    // Editor
    Register({
        TEXT("get_actors_in_level"), TEXT("find_actors_by_name"), TEXT("spawn_actor"),
        TEXT("delete_actor"), TEXT("set_actor_transform"), TEXT("spawn_blueprint_actor"),
        TEXT("get_rvt_volumes"), TEXT("get_landscape_info"),
        TEXT("place_actors_batch")
    }, EditorCommands);

    // Blueprint
    Register({
        TEXT("create_blueprint"), TEXT("add_component_to_blueprint"), TEXT("add_blueprint_component"),
        TEXT("reparent_blueprint_component"),
        TEXT("remove_component_from_blueprint"),
        TEXT("get_blueprint_components"), TEXT("list_blueprint_components"),
        TEXT("set_physics_properties"), TEXT("compile_blueprint"),
        TEXT("compile_blueprint_with_diagnostics"),
        TEXT("save_blueprint"), TEXT("list_blueprints"), TEXT("set_static_mesh_properties"),
        TEXT("set_mesh_material_color"), TEXT("get_available_materials"), TEXT("apply_material_to_actor"),
        TEXT("apply_material_to_blueprint"), TEXT("get_actor_material_info"), TEXT("get_blueprint_material_info"),
        TEXT("read_blueprint_content"), TEXT("analyze_blueprint_graph"), TEXT("get_blueprint_variable_details"),
        TEXT("get_blueprint_function_details"), TEXT("get_material_instance_parameters"),
        TEXT("set_material_instance_parameters"), TEXT("get_blueprint_default_value"),
        TEXT("set_blueprint_default_value"), TEXT("reset_blueprint_default_value"),
        TEXT("get_component_default_value"), TEXT("set_component_default_value"),
        TEXT("reset_component_default_value")
    }, BlueprintCommands);

    // Blueprint graph
    Register({
        TEXT("add_blueprint_node"), TEXT("connect_nodes"), TEXT("create_variable"),
        TEXT("set_blueprint_variable_properties"), TEXT("delete_variable"), TEXT("add_event_node"),
        TEXT("delete_node"), TEXT("set_node_property"), TEXT("create_function"),
        TEXT("add_function_input"), TEXT("add_function_output"), TEXT("delete_function"),
        TEXT("rename_function"),
        TEXT("export_nodes_t3d"), TEXT("import_nodes_t3d")
    }, BlueprintGraphCommands);

    // Thumbnails
    Register({
        TEXT("generate_thumbnail"), TEXT("generate_thumbnails"), TEXT("scan_assets_for_thumbnails")
    }, ThumbnailCommands);

    // Material analysis + editing
    Register({
        // READ
        TEXT("get_material_expressions"), TEXT("get_material_connections"),
        TEXT("get_material_function_info"), TEXT("get_material_stats"),
        // WRITE (graph editing with FScopedTransaction + post-condition verify)
        TEXT("material_create_expression"), TEXT("material_delete_expression"),
        TEXT("material_connect_expressions"), TEXT("material_connect_property"),
        TEXT("material_disconnect_input"),
        TEXT("material_set_expression_position"), TEXT("material_set_expression_property"),
        TEXT("material_recompile"), TEXT("material_update_function"),
        TEXT("material_save"), TEXT("material_batch_edit")
    }, MaterialCommands);

    // RVT
    Register({ TEXT("get_rvt_info"), TEXT("set_rvt_properties") }, RVTCommands);

    // Animation & skeleton
    Register({
        TEXT("analyze_anim_montage"), TEXT("analyze_anim_sequence"), TEXT("list_animation_assets"),
        TEXT("get_skeletal_mesh_info"), TEXT("add_socket_to_skeleton"), TEXT("modify_socket"),
        TEXT("remove_socket"), TEXT("preview_mesh_on_socket"), TEXT("clear_socket_preview"),
        TEXT("capture_socket_preview"), TEXT("create_anim_montage"),
        // Montage section mutation
        TEXT("add_montage_section"), TEXT("remove_montage_section"), TEXT("update_montage_section"),
        TEXT("set_montage_section_link"), TEXT("reorder_montage_section"),
        TEXT("add_section_metadata"), TEXT("remove_section_metadata"),
        // Montage notify mutation
        TEXT("add_montage_notify"), TEXT("remove_montage_notify"), TEXT("update_montage_notify"),
        TEXT("add_montage_notify_track"), TEXT("rename_montage_notify_track"),
        TEXT("remove_montage_notify_track"), TEXT("reorder_montage_notify_track"),
        TEXT("set_montage_notify_track_color"),
        // Montage segment mutation
        TEXT("add_montage_segment"), TEXT("remove_montage_segment"),
        TEXT("update_montage_segment"), TEXT("reorder_montage_segment"),
        // Montage slot mutation
        TEXT("add_montage_slot"), TEXT("rename_montage_slot"),
        TEXT("remove_montage_slot"), TEXT("reorder_montage_slot"),
        // Montage-level settings
        TEXT("set_montage_blend_times"), TEXT("set_montage_root_motion"),
        TEXT("set_montage_sync_group"), TEXT("set_montage_playback"),
        TEXT("set_montage_time_stretch"), TEXT("set_montage_preview_base_pose"),
        // AnimSequence settings
        TEXT("set_anim_sequence_root_motion"), TEXT("set_anim_sequence_properties"),
        // Curves & root motion data
        TEXT("get_anim_curve_keys"), TEXT("add_anim_curve"), TEXT("set_anim_curve_keys"),
        TEXT("remove_anim_curve"), TEXT("get_root_motion_data"), TEXT("batch_add_speed_curves"),
        // Mirror data tables
        TEXT("create_mirror_data_table"), TEXT("analyze_mirror_data_table")
    }, AnimationCommands);

    // Data assets
    Register({
        TEXT("read_data_asset"), TEXT("list_data_assets"), TEXT("create_data_asset"),
        TEXT("update_data_asset"), TEXT("set_dataasset_property"), TEXT("get_dataasset_property"),
        TEXT("describe_data_asset_class"), TEXT("list_data_asset_classes"),
        TEXT("save_data_asset"), TEXT("delete_data_asset"), TEXT("rename_data_asset"),
        TEXT("duplicate_data_asset"), TEXT("data_asset_array_remove")
    }, DataAssetCommands);

    // Data tables
    Register({
        TEXT("read_data_table"), TEXT("read_data_table_row"), TEXT("write_data_table_row"),
        TEXT("write_data_table_rows"), TEXT("delete_data_table_row")
    }, DataTableCommands);

    // Curve tables
    Register({
        TEXT("list_curve_tables"), TEXT("read_curve_table"), TEXT("read_curve_table_row"),
        TEXT("create_curve_table"), TEXT("update_curve_table_row"), TEXT("delete_curve_table_row")
    }, CurveTableCommands);

    // Curve assets (UCurveFloat)
    Register({
        TEXT("list_curve_floats"), TEXT("read_curve_float"),
        TEXT("create_curve_float"), TEXT("update_curve_float")
    }, CurveAssetCommands);

    // MPC
    Register({ TEXT("get_mpc_parameters"), TEXT("set_mpc_parameters") }, MPCCommands);

    // Sound
    Register({ TEXT("get_sound_info") }, SoundCommands);

    // Widgets
    Register({
        TEXT("analyze_widget_blueprint"), TEXT("get_widget_details"), TEXT("create_widget_blueprint"),
        TEXT("add_widget_child"), TEXT("set_widget_property"), TEXT("remove_widget"),
        TEXT("save_widget_blueprint"), TEXT("delete_widget_blueprint"),
        TEXT("rename_widget_blueprint"), TEXT("duplicate_widget_blueprint"),
        TEXT("list_widget_blueprints"), TEXT("move_widget"),
        TEXT("reorder_widget_children"), TEXT("set_widget_slot_property"),
        TEXT("set_widget_root"), TEXT("compile_widget_blueprint"),
        TEXT("list_required_bindwidgets"), TEXT("set_widget_class_property"),
        TEXT("set_widget_object_property"), TEXT("create_common_style_asset"),
        TEXT("set_named_slot_content"),
        TEXT("wrap_widget"), TEXT("set_widget_is_variable"), TEXT("replace_widget_class")
    }, WidgetCommands);

    // Widget previews (offscreen FWidgetRenderer captures)
    Register({
        TEXT("capture_widget_preview"), TEXT("capture_widget_state")
    }, WidgetPreviewCommands);

    // World
    Register({ TEXT("get_world_info"), TEXT("get_level_details") }, WorldCommands);

    // StateTree
    Register({
        TEXT("create_state_tree"), TEXT("read_state_tree"), TEXT("list_state_trees"),
        TEXT("save_state_tree"), TEXT("delete_state_tree"), TEXT("rename_state_tree"),
        TEXT("duplicate_state_tree"), TEXT("compile_state_tree"), TEXT("validate_state_tree"),
        TEXT("list_state_tree_schemas"), TEXT("add_subtree"), TEXT("add_state_tree_state"),
        TEXT("remove_state_tree_state"), TEXT("rename_state_tree_state"), TEXT("move_state_tree_state"),
        TEXT("set_state_tree_state_property"), TEXT("add_state_task"), TEXT("add_state_enter_condition"),
        TEXT("add_state_consideration"), TEXT("add_global_task"), TEXT("add_evaluator"),
        TEXT("remove_state_tree_node"), TEXT("set_state_tree_node_property"),
        TEXT("add_state_tree_transition"), TEXT("remove_state_tree_transition"),
        TEXT("set_state_tree_transition_property"), TEXT("add_transition_condition"),
        TEXT("add_root_parameter"), TEXT("remove_root_parameter"), TEXT("set_root_parameter_value"),
        TEXT("add_property_binding"), TEXT("remove_property_binding"), TEXT("list_property_bindings"),
        TEXT("analyze_state_tree"), TEXT("list_state_tree_node_classes"), TEXT("add_state_parameter"),
        TEXT("remove_state_parameter"), TEXT("set_state_parameter_value"), TEXT("add_function_binding"),
        TEXT("add_output_binding"), TEXT("list_schema_context_data"), TEXT("set_linked_state"),
        TEXT("set_state_parameter_override")
    }, StateTreeCommands);

    // AnimBlueprint
    Register({
        TEXT("create_anim_blueprint"), TEXT("read_anim_blueprint"), TEXT("add_state_machine"),
        TEXT("add_state"), TEXT("add_transition"), TEXT("set_default_state"),
        TEXT("set_state_animation"), TEXT("set_transition_rule"), TEXT("add_blend_space_player"),
        TEXT("connect_anim_nodes"), TEXT("disconnect_anim_nodes"), TEXT("set_pin_default_value"),
        TEXT("set_transition_property"), TEXT("simplify_transition_rule"),
        TEXT("get_state_machine_info"), TEXT("add_anim_node"), TEXT("delete_anim_node")
    }, AnimBlueprintCommands);

    // Static mesh sockets + geometry/collision audit
    Register({
        TEXT("get_static_mesh_info"), TEXT("get_mesh_footprint"),
        TEXT("get_mesh_collision_info"), TEXT("generate_convex_collision"),
        TEXT("get_mesh_vertex_colors"), TEXT("delete_mesh_geometry_in_box"),
        TEXT("add_socket_to_static_mesh"),
        TEXT("modify_static_mesh_socket"), TEXT("remove_static_mesh_socket"),
        TEXT("capture_static_mesh_socket_preview")
    }, StaticMeshCommands);

    // Python execution (live editor)
    Register({
        TEXT("run_python"), TEXT("run_python_file")
    }, PythonCommands);

    // Asset management
    Register({
        TEXT("fixup_redirectors"),
        TEXT("delete_blueprint"),
        TEXT("find_asset_referencers"),
        TEXT("replace_asset_references"),
        TEXT("search_assets_fts"),
        TEXT("wait_for_asset_compile"), TEXT("wait_for_shaders"),
        TEXT("get_navmesh_build_status"), TEXT("save_asset_checked")
    }, AssetCommands);

    // Texture import (used by generate_icon_asset for AI-generated icons)
    Register({
        TEXT("import_texture_for_icon"),
        TEXT("import_texture")              // role-aware: Color/Normal/ORD/Mask/Displacement/BlueNoise/UI
    }, TextureImportCommands);

    // Enums + gameplay tags
    Register({
        TEXT("create_enum"), TEXT("set_enum_entries"), TEXT("read_enum"),
        TEXT("create_gameplay_tag"), TEXT("list_gameplay_tags")
    }, EnumTagCommands);


    // PCG (Procedural Content Generation) graph editing — Epic plugin with
    // full BlueprintCallable API surface; commands thin-wrap UPCGGraph methods.
    Register({
        TEXT("pcg_graph_list_nodes"),
        TEXT("pcg_graph_list_edges"),
        TEXT("pcg_graph_add_node"),
        TEXT("pcg_graph_remove_node"),
        TEXT("pcg_graph_add_edge"),
        TEXT("pcg_graph_remove_edge"),
        TEXT("pcg_graph_set_node_position"),
        TEXT("pcg_graph_list_settings_classes"),
        TEXT("pcg_component_generate"),
        TEXT("pcg_component_get_state"),
        TEXT("pcg_sample_point_attributes"),
        TEXT("pcg_component_output_summary"),
        TEXT("pcg_component_diagnose"),
        TEXT("pcg_set_filter_target"),
        TEXT("pcg_regen_start"),
        TEXT("pcg_regen_poll"),
        TEXT("pcg_regen_finalize"),
        TEXT("pcg_regen_recover")
    }, PCGCommands);
}

// Clean up resources when subsystem is destroyed
void UEpicUnrealMCPBridge::Deinitialize()
{
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Shutting down"));
    StopServer();
}

// Start the MCP server
void UEpicUnrealMCPBridge::StartServer()
{
    if (bIsRunning)
    {
        UE_LOG(LogTemp, Warning, TEXT("EpicUnrealMCPBridge: Server is already running"));
        return;
    }

    // Create socket subsystem
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to get socket subsystem"));
        return;
    }

    // Create listener socket
    TSharedPtr<FSocket> NewListenerSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealMCPListener"), false));
    if (!NewListenerSocket.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to create listener socket"));
        return;
    }

    // Allow address reuse for quick restarts
    NewListenerSocket->SetReuseAddr(true);
    NewListenerSocket->SetNonBlocking(true);

    // Bind to address
    FIPv4Endpoint Endpoint(ServerAddress, Port);
    if (!NewListenerSocket->Bind(*Endpoint.ToInternetAddr()))
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to bind listener socket to %s:%d"), *ServerAddress.ToString(), Port);
        return;
    }

    // Start listening — backlog sized for bursts of session reconnects.
    if (!NewListenerSocket->Listen(32))
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to start listening"));
        return;
    }

    ListenerSocket = NewListenerSocket;
    bIsRunning = true;
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Server started on %s:%d"), *ServerAddress.ToString(), Port);

    // Start server thread
    ServerThread = FRunnableThread::Create(
        new FMCPServerRunnable(this, ListenerSocket),
        TEXT("UnrealMCPServerThread"),
        0, TPri_Normal
    );

    if (!ServerThread)
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to create server thread"));
        StopServer();
        return;
    }
}

// Stop the MCP server
void UEpicUnrealMCPBridge::StopServer()
{
    if (!bIsRunning)
    {
        return;
    }

    bIsRunning = false;

    // Clean up thread
    if (ServerThread)
    {
        ServerThread->Kill(true);
        delete ServerThread;
        ServerThread = nullptr;
    }

    // Close sockets
    if (ConnectionSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ConnectionSocket.Get());
        ConnectionSocket.Reset();
    }

    if (ListenerSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket.Get());
        ListenerSocket.Reset();
    }

    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Server stopped"));
}

// Execute a command received from a client.
// The handler runs on the game thread via FTSTicker (top-level FEngineLoop::Tick
// callsite), NOT AsyncTask(GameThread). Reason: AsyncTask enqueues onto the GT
// named-thread queue and any pump can run it — including the nested pump inside
// MassEntityEditorSubsystem::Tick's WaitWithNamedThreadsSupport. If a handler
// then calls Interchange (which pumps the same queue inside WaitUntilDone),
// FNamedTaskThread::RecursionGuard trips and the editor crashes. FTSTicker
// callbacks fire from a clean tick context, so Interchange's wait is the first
// pump on the queue.
//
// Building the outer envelope and JSON serialization happen on the caller's
// worker thread so the game thread is held for the minimum possible time.
FString UEpicUnrealMCPBridge::ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    TSharedRef<TPromise<TSharedPtr<FJsonObject>>> Promise = MakeShared<TPromise<TSharedPtr<FJsonObject>>>();
    TFuture<TSharedPtr<FJsonObject>> Future = Promise->GetFuture();

    // Exactly one side completes the promise: the game-thread ticker (it ran the
    // handler) or this worker (the bridge stopped before the ticker fired). The core
    // ticker is not pumped during editor shutdown, so an unconditional wait here hangs
    // Deinitialize -> StopServer -> worker join. TPromise check()s if destroyed
    // incomplete or completed twice, hence the claim flag rather than a plain timeout.
    TSharedRef<std::atomic<bool>> bClaimed = MakeShared<std::atomic<bool>>(false);

    auto RunHandler = [this, CommandType, Params, Promise, bClaimed]()
    {
        if (bClaimed->exchange(true))
        {
            return; // worker gave up during shutdown and already answered
        }

        try
        {
            if (CommandType == TEXT("ping"))
            {
                TSharedPtr<FJsonObject> Pong = MakeShared<FJsonObject>();
                Pong->SetStringField(TEXT("message"), TEXT("pong"));
                Promise->SetValue(Pong);
                return;
            }

            if (const FCommandHandler* Handler = Dispatch.Find(FName(*CommandType)))
            {
                Promise->SetValue((*Handler)(CommandType, Params));
                return;
            }

            TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
            Err->SetBoolField(TEXT("success"), false);
            Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown command: %s"), *CommandType));
            Promise->SetValue(Err);
        }
        catch (const std::exception& e)
        {
            TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
            Err->SetBoolField(TEXT("success"), false);
            Err->SetStringField(TEXT("error"), UTF8_TO_TCHAR(e.what()));
            Promise->SetValue(Err);
        }
    };

    FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([RunHandler = MoveTemp(RunHandler)](float) -> bool
        {
            RunHandler();
            return false; // one-shot
        }),
        0.0f);

    while (!Future.WaitFor(FTimespan::FromMilliseconds(100)))
    {
        if (!bIsRunning && !bClaimed->exchange(true))
        {
            TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
            Err->SetBoolField(TEXT("success"), false);
            Err->SetStringField(TEXT("error"),
                FString::Printf(TEXT("MCP bridge shutting down; '%s' was not executed"), *CommandType));
            Promise->SetValue(Err);
        }
    }

    TSharedPtr<FJsonObject> ResultJson = Future.Get();

    // Wrap and serialize on this (worker) thread.
    TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();

    bool bSuccess = true;
    FString ErrorMessage;
    if (ResultJson.IsValid() && ResultJson->HasField(TEXT("success")))
    {
        bSuccess = ResultJson->GetBoolField(TEXT("success"));
        if (!bSuccess && ResultJson->HasField(TEXT("error")))
        {
            ErrorMessage = ResultJson->GetStringField(TEXT("error"));
        }
    }

    if (bSuccess && ResultJson.IsValid())
    {
        ResponseJson->SetStringField(TEXT("status"), TEXT("success"));
        ResponseJson->SetObjectField(TEXT("result"), ResultJson);
    }
    else
    {
        ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
        ResponseJson->SetStringField(TEXT("error"), ErrorMessage.IsEmpty() ? TEXT("Unknown error") : *ErrorMessage);
    }

    FString ResultString;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ResultString);
    FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
    return ResultString;
}