#pragma once

#include "CoreMinimal.h"
#include "Json.h"

// Forward declarations
struct FAssetData;
class AActor;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UK2Node_Event;
class UK2Node_CallFunction;
class UK2Node_VariableGet;
class UK2Node_VariableSet;
class UK2Node_InputAction;
class UK2Node_Self;
class UFunction;

/**
 * Common utilities for EpicUnrealMCP commands
 */
class UNREALMCP_API FEpicUnrealMCPCommonUtils
{
public:
    // JSON utilities
    static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& Message);
    static TSharedPtr<FJsonObject> CreateSuccessResponse(const TSharedPtr<FJsonObject>& Data = nullptr);
    static void GetIntArrayFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<int32>& OutArray);
    static void GetFloatArrayFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<float>& OutArray);
    static FVector2D GetVector2DFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);
    static FVector GetVectorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);
    static FRotator GetRotatorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);
    
    // Actor utilities
    static TSharedPtr<FJsonValue> ActorToJson(AActor* Actor);
    static TSharedPtr<FJsonObject> ActorToJsonObject(AActor* Actor, bool bDetailed = false);
    
    // Blueprint utilities
    static UBlueprint* FindBlueprint(const FString& BlueprintName);
    static UBlueprint* FindBlueprintByName(const FString& BlueprintName);
    static UEdGraph* FindOrCreateEventGraph(UBlueprint* Blueprint);
    
    // Blueprint node utilities
    static UK2Node_Event* CreateEventNode(UEdGraph* Graph, const FString& EventName, const FVector2D& Position);
    static UK2Node_CallFunction* CreateFunctionCallNode(UEdGraph* Graph, UFunction* Function, const FVector2D& Position);
    static UK2Node_VariableGet* CreateVariableGetNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, const FVector2D& Position);
    static UK2Node_VariableSet* CreateVariableSetNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, const FVector2D& Position);
    static UK2Node_InputAction* CreateInputActionNode(UEdGraph* Graph, const FString& ActionName, const FVector2D& Position);
    static UK2Node_Self* CreateSelfReferenceNode(UEdGraph* Graph, const FVector2D& Position);
    static bool ConnectGraphNodes(UEdGraph* Graph, UEdGraphNode* SourceNode, const FString& SourcePinName, 
                                UEdGraphNode* TargetNode, const FString& TargetPinName);
    static UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction = EGPD_MAX);
    static UK2Node_Event* FindExistingEventNode(UEdGraph* Graph, const FString& EventName);

    // Property utilities
    static bool SetObjectProperty(UObject* Object, const FString& PropertyName,
                                 const TSharedPtr<FJsonValue>& Value, FString& OutErrorMessage);

    /**
     * Resolve dot/bracket notation ("Actions[0].Variants[3].MaxDistance") against a root
     * UObject to a (leaf property, container address) pair. OutContainer is the address of
     * the struct/UObject that directly contains the leaf; obtain the value address via
     * OutLeaf->ContainerPtrToValuePtr<void>(OutContainer). For a bare property path the leaf
     * is that property itself (e.g. an FArrayProperty when the path names an array).
     * Descends structs and indexes into arrays of structs; does NOT descend into TMaps.
     */
    static bool ResolvePropertyPath(UObject* Root, const FString& Path,
                                    FProperty*& OutLeaf, void*& OutContainer, FString& OutError);

    /**
     * Apply a JSON value to a single field on an in-memory UScriptStruct instance.
     * Resolves the struct member by name, then dispatches through the same property
     * handler used by SetObjectProperty, so nested structs, gameplay tags, arrays,
     * object references, etc. are all supported.
     *
     * @param StructType      Struct descriptor.
     * @param StructData      Pointer to the raw struct instance memory.
     * @param FieldName       Name of the struct member to set.
     * @param Value           JSON value to apply.
     * @param OptionalOwner   Owning UObject used only by ImportText fallbacks; may be nullptr.
     * @param OutErrorMessage Populated on failure.
     * @return true on success.
     */
    static bool SetStructField(UScriptStruct* StructType, void* StructData,
                               const FString& FieldName,
                               const TSharedPtr<FJsonValue>& Value,
                               UObject* OptionalOwner,
                               FString& OutErrorMessage);

    /**
     * Resolve a class name to a UClass*.
     * Accepts full script paths ("/Script/Module.Class"), short names ("Foo"),
     * short names missing the "U" prefix ("Moveset" -> "UMoveset"), and probes
     * every loaded module via FModuleManager when FindFirstObject fails.
     * Returns nullptr if no match is found.
     */
    static UClass* ResolveClass(const FString& Name);

    // ------------------------------------------------------------------
    // Registry-truth asset resolution
    // ------------------------------------------------------------------
    //
    // UEditorAssetLibrary::DoesAssetExist / LoadAsset are string-keyed: they
    // re-resolve the path through FSoftObjectPath normalization + a package open,
    // and FALSE-NEGATIVE (exists==false / load==null) when the string does not
    // match the registry's canonical key. Common divergences:
    //   * package path vs object path ("/Game/Foo/DA_Bar" needs ".DA_Bar"),
    //   * Blueprint asset vs generated class ("BP_Foo" vs "BP_Foo_C"),
    //   * a stale / mid-scan registry or an ObjectRedirector the string load
    //     won't chase,
    //   * mount-point / case / trailing-slash mismatches.
    //
    // These helpers resolve through the AssetRegistry's FAssetData (which already
    // holds the canonical SoftObjectPath) and load FROM that, so existence and
    // loading are reliable. Prefer them over the raw EditorAssetLibrary calls in
    // every command handler that GATES on "asset not found".

    /**
     * Normalize an asset path to an object path. "/Game/Foo/DA_Bar" ->
     * "/Game/Foo/DA_Bar.DA_Bar"; an already-qualified "Pkg.Object" (incl. "_C"
     * class paths) and trailing slashes are handled. Pure string transform.
     */
    static FString NormalizeObjectPath(const FString& AssetPath);

    /**
     * Resolve the FAssetData the registry holds for AssetPath. Tries the object
     * path, then a package-name match, then one synchronous path scan (defeats
     * the just-created/renamed-this-session staleness case). Returns true and
     * fills OutData on success.
     */
    static bool ResolveAssetData(const FString& AssetPath, FAssetData& OutData);

    /**
     * Registry-truth existence check. Use instead of
     * UEditorAssetLibrary::DoesAssetExist for "not found" gates.
     */
    static bool AssetExists(const FString& AssetPath);

    /**
     * Load an asset reliably. Resolves the FAssetData first, then loads from its
     * canonical path; falls back to the normalized object path, then the raw
     * string. A strict superset of UEditorAssetLibrary::LoadAsset — never worse,
     * succeeds in the false-negative cases. Returns nullptr only if the asset
     * genuinely does not exist.
     */
    static UObject* ResolveAsset(const FString& AssetPath);
};