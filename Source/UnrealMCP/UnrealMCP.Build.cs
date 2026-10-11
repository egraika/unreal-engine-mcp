// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealMCP : ModuleRules
{
	public UnrealMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDefinitions.Add("UNREALMCP_EXPORTS=1");

		PublicIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "Public"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Commands"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Commands/BlueprintGraph"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Commands/BlueprintGraph/Nodes")
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "Private"),
				System.IO.Path.Combine(ModuleDirectory, "Private/Commands"),
				System.IO.Path.Combine(ModuleDirectory, "Private/Commands/BlueprintGraph"),
				System.IO.Path.Combine(ModuleDirectory, "Private/Commands/BlueprintGraph/Nodes")
			}
		);
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Networking",
				"Sockets",
				"HTTP",
				"Json",
				"JsonUtilities",
				"DeveloperSettings",
				"PhysicsCore",
				"UnrealEd",           // For Blueprint editing
				"BlueprintGraph",     // For K2Node classes (F15-F22)
				"KismetCompiler",     // For Blueprint compilation (F15-F22)
				"AnimGraph",          // For AnimGraphNode_StateMachine (AnimBP commands)
				"AnimGraphRuntime",   // For AnimNode_ApplyAdditive, AnimNode_Slot
				"GameplayTags",       // For FGameplayTag property support
				"AnimationWarpingRuntime", // For FAnimNode_OrientationWarping, FAnimNode_StrideWarping
				"AnimationWarpingEditor"   // For UAnimGraphNode_OrientationWarping, UAnimGraphNode_StrideWarping
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"EditorScriptingUtilities",
				"EditorSubsystem",
				"Slate",
				"SlateCore",
				"Kismet",
				"Projects",
				"AssetRegistry",
				"RHI",
				"RenderCore",
				"Landscape",
				"UMG",
				"UMGEditor",
				"CommonUI",                // For UCommonActivatableWidget*, UCommonUserWidget, UCommonButtonBase, UCommonTextBlock
				"PRK",
				"StateTreeModule",         // For UStateTree, UStateTreeSchema runtime types
				"StateTreeEditorModule",   // For UStateTreeEditorData, UStateTreeState, factory, editing subsystem
				"GameplayStateTreeModule", // For UStateTreeComponentSchema (default schema)
				"PropertyBindingUtils",    // For FPropertyBindingPath / binding collection base
				"PythonScriptPlugin",      // For IPythonScriptPlugin::ExecPythonCommandEx (run_python tool)
				"AssetTools",              // For IAssetTools::FixupReferencers (fixup_redirectors tool)
				"GameplayTagsEditor",      // For IGameplayTagsEditorModule::AddNewGameplayTagToINI
				"PCG",                     // For UPCGGraph, UPCGNode, UPCGSettings (pcg_* commands)
				"MaterialEditor",          // For UMaterialEditingLibrary (material_* graph editing commands)
				"StaticMeshEditor",        // For UStaticMeshEditorSubsystem (generate_convex_collision)
				"NavigationSystem",        // For UNavigationSystemV1 build-status (get_navmesh_build_status)
				"SourceControl",           // For USourceControlHelpers::CheckOutOrAddFile (save_asset_checked)
				"GeometryCore",            // For FDynamicMesh3 (delete_mesh_geometry_in_box)
				"GeometryFramework",       // For UDynamicMesh (EditMesh/ProcessMesh)
				"GeometryScriptingCore"    // For CopyMeshFrom/ToStaticMesh (static-mesh <-> DynamicMesh)
			}
		);
		
		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"PropertyEditor",      // For property editing
					"ToolMenus",           // For editor UI
					"BlueprintEditorLibrary" // For Blueprint utilities
				}
			);
		}
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
	}
} 