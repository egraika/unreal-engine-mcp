#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "GameplayTagContainer.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Selection.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// JSON Utilities
TSharedPtr<FJsonObject> FEpicUnrealMCPCommonUtils::CreateErrorResponse(const FString& Message)
{
    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetBoolField(TEXT("success"), false);
    ResponseObject->SetStringField(TEXT("error"), Message);
    return ResponseObject;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPCommonUtils::CreateSuccessResponse(const TSharedPtr<FJsonObject>& Data)
{
    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetBoolField(TEXT("success"), true);
    
    if (Data.IsValid())
    {
        ResponseObject->SetObjectField(TEXT("data"), Data);
    }
    
    return ResponseObject;
}

void FEpicUnrealMCPCommonUtils::GetIntArrayFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<int32>& OutArray)
{
    OutArray.Reset();
    
    if (!JsonObject->HasField(FieldName))
    {
        return;
    }
    
    const TArray<TSharedPtr<FJsonValue>>* JsonArray;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
        {
            OutArray.Add((int32)Value->AsNumber());
        }
    }
}

void FEpicUnrealMCPCommonUtils::GetFloatArrayFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<float>& OutArray)
{
    OutArray.Reset();
    
    if (!JsonObject->HasField(FieldName))
    {
        return;
    }
    
    const TArray<TSharedPtr<FJsonValue>>* JsonArray;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
        {
            OutArray.Add((float)Value->AsNumber());
        }
    }
}

FVector2D FEpicUnrealMCPCommonUtils::GetVector2DFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
{
    FVector2D Result(0.0f, 0.0f);
    
    if (!JsonObject->HasField(FieldName))
    {
        return Result;
    }
    
    const TArray<TSharedPtr<FJsonValue>>* JsonArray;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray) && JsonArray->Num() >= 2)
    {
        Result.X = (float)(*JsonArray)[0]->AsNumber();
        Result.Y = (float)(*JsonArray)[1]->AsNumber();
    }
    
    return Result;
}

FVector FEpicUnrealMCPCommonUtils::GetVectorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
{
    FVector Result(0.0f, 0.0f, 0.0f);
    
    if (!JsonObject->HasField(FieldName))
    {
        return Result;
    }
    
    const TArray<TSharedPtr<FJsonValue>>* JsonArray;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray) && JsonArray->Num() >= 3)
    {
        Result.X = (float)(*JsonArray)[0]->AsNumber();
        Result.Y = (float)(*JsonArray)[1]->AsNumber();
        Result.Z = (float)(*JsonArray)[2]->AsNumber();
    }
    
    return Result;
}

FRotator FEpicUnrealMCPCommonUtils::GetRotatorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
{
    FRotator Result(0.0f, 0.0f, 0.0f);
    
    if (!JsonObject->HasField(FieldName))
    {
        return Result;
    }
    
    const TArray<TSharedPtr<FJsonValue>>* JsonArray;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray) && JsonArray->Num() >= 3)
    {
        Result.Pitch = (float)(*JsonArray)[0]->AsNumber();
        Result.Yaw = (float)(*JsonArray)[1]->AsNumber();
        Result.Roll = (float)(*JsonArray)[2]->AsNumber();
    }
    
    return Result;
}

// Blueprint Utilities
UBlueprint* FEpicUnrealMCPCommonUtils::FindBlueprint(const FString& BlueprintName)
{
    return FindBlueprintByName(BlueprintName);
}

UBlueprint* FEpicUnrealMCPCommonUtils::FindBlueprintByName(const FString& BlueprintName)
{
    // The correct object path for a Blueprint asset is /Game/Path/AssetName.AssetName
    FString ObjectPath;

    // Check if BlueprintName is already a full path (starts with /)
    if (BlueprintName.StartsWith(TEXT("/")))
    {
        // It's already a full path, use it directly with the class suffix
        FString AssetName = FPaths::GetBaseFilename(BlueprintName);
        ObjectPath = FString::Printf(TEXT("%s.%s"), *BlueprintName, *AssetName);
    }
    else
    {
        // It's just a name, add the default /Game/Blueprints/ prefix
        ObjectPath = FString::Printf(TEXT("/Game/Blueprints/%s.%s"), *BlueprintName, *BlueprintName);
    }

    // First, try to load the object directly, as it's the fastest method.
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
    if (Blueprint)
    {
        return Blueprint;
    }

    // If direct loading fails, try to find the asset using the Asset Registry.
    // This is more robust for newly created assets.
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));

    if (AssetData.IsValid())
    {
        Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
        if (Blueprint)
        {
            return Blueprint;
        }
    }

    // Fallback for cases where the asset is in memory but not yet fully saved,
    // where it might be found via its package path.
    FString PackagePath = TEXT("/Game/Blueprints/") + BlueprintName;
    Blueprint = FindObject<UBlueprint>(nullptr, *PackagePath);

    if (!Blueprint)
    {
         UE_LOG(LogTemp, Error, TEXT("FindBlueprintByName: Failed to find or load blueprint: %s"), *BlueprintName);
    }

    return Blueprint;
}

UEdGraph* FEpicUnrealMCPCommonUtils::FindOrCreateEventGraph(UBlueprint* Blueprint)
{
    if (!Blueprint)
    {
        return nullptr;
    }
    
    // Try to find the event graph
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph->GetName().Contains(TEXT("EventGraph")))
        {
            return Graph;
        }
    }
    
    // Create a new event graph if none exists
    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(TEXT("EventGraph")), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(Blueprint, NewGraph);
    return NewGraph;
}

// Blueprint node utilities
UK2Node_Event* FEpicUnrealMCPCommonUtils::CreateEventNode(UEdGraph* Graph, const FString& EventName, const FVector2D& Position)
{
    if (!Graph)
    {
        return nullptr;
    }
    
    UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
    if (!Blueprint)
    {
        return nullptr;
    }
    
    // Check for existing event node with this exact name
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
        if (EventNode && EventNode->EventReference.GetMemberName() == FName(*EventName))
        {
            UE_LOG(LogTemp, Display, TEXT("Using existing event node with name %s (ID: %s)"), 
                *EventName, *EventNode->NodeGuid.ToString());
            return EventNode;
        }
    }

    // No existing node found, create a new one
    UK2Node_Event* EventNode = nullptr;
    
    // Find the function to create the event
    UClass* BlueprintClass = Blueprint->GeneratedClass;
    UFunction* EventFunction = BlueprintClass->FindFunctionByName(FName(*EventName));
    
    if (EventFunction)
    {
        EventNode = NewObject<UK2Node_Event>(Graph);
        EventNode->EventReference.SetExternalMember(FName(*EventName), BlueprintClass);
        EventNode->NodePosX = Position.X;
        EventNode->NodePosY = Position.Y;
        Graph->AddNode(EventNode, true);
        EventNode->PostPlacedNewNode();
        EventNode->AllocateDefaultPins();
        UE_LOG(LogTemp, Display, TEXT("Created new event node with name %s (ID: %s)"), 
            *EventName, *EventNode->NodeGuid.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to find function for event name: %s"), *EventName);
    }
    
    return EventNode;
}

UK2Node_CallFunction* FEpicUnrealMCPCommonUtils::CreateFunctionCallNode(UEdGraph* Graph, UFunction* Function, const FVector2D& Position)
{
    if (!Graph || !Function)
    {
        return nullptr;
    }
    
    UK2Node_CallFunction* FunctionNode = NewObject<UK2Node_CallFunction>(Graph);
    FunctionNode->SetFromFunction(Function);
    FunctionNode->NodePosX = Position.X;
    FunctionNode->NodePosY = Position.Y;
    Graph->AddNode(FunctionNode, true);
    FunctionNode->CreateNewGuid();
    FunctionNode->PostPlacedNewNode();
    FunctionNode->AllocateDefaultPins();
    
    return FunctionNode;
}

UK2Node_VariableGet* FEpicUnrealMCPCommonUtils::CreateVariableGetNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, const FVector2D& Position)
{
    if (!Graph || !Blueprint)
    {
        return nullptr;
    }
    
    UK2Node_VariableGet* VariableGetNode = NewObject<UK2Node_VariableGet>(Graph);
    
    FName VarName(*VariableName);
    FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, VarName);
    
    if (Property)
    {
        VariableGetNode->VariableReference.SetFromField<FProperty>(Property, false);
        VariableGetNode->NodePosX = Position.X;
        VariableGetNode->NodePosY = Position.Y;
        Graph->AddNode(VariableGetNode, true);
        VariableGetNode->PostPlacedNewNode();
        VariableGetNode->AllocateDefaultPins();
        
        return VariableGetNode;
    }
    
    return nullptr;
}

UK2Node_VariableSet* FEpicUnrealMCPCommonUtils::CreateVariableSetNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, const FVector2D& Position)
{
    if (!Graph || !Blueprint)
    {
        return nullptr;
    }
    
    UK2Node_VariableSet* VariableSetNode = NewObject<UK2Node_VariableSet>(Graph);
    
    FName VarName(*VariableName);
    FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, VarName);
    
    if (Property)
    {
        VariableSetNode->VariableReference.SetFromField<FProperty>(Property, false);
        VariableSetNode->NodePosX = Position.X;
        VariableSetNode->NodePosY = Position.Y;
        Graph->AddNode(VariableSetNode, true);
        VariableSetNode->PostPlacedNewNode();
        VariableSetNode->AllocateDefaultPins();
        
        return VariableSetNode;
    }
    
    return nullptr;
}

UK2Node_InputAction* FEpicUnrealMCPCommonUtils::CreateInputActionNode(UEdGraph* Graph, const FString& ActionName, const FVector2D& Position)
{
    if (!Graph)
    {
        return nullptr;
    }
    
    UK2Node_InputAction* InputActionNode = NewObject<UK2Node_InputAction>(Graph);
    InputActionNode->InputActionName = FName(*ActionName);
    InputActionNode->NodePosX = Position.X;
    InputActionNode->NodePosY = Position.Y;
    Graph->AddNode(InputActionNode, true);
    InputActionNode->CreateNewGuid();
    InputActionNode->PostPlacedNewNode();
    InputActionNode->AllocateDefaultPins();
    
    return InputActionNode;
}

UK2Node_Self* FEpicUnrealMCPCommonUtils::CreateSelfReferenceNode(UEdGraph* Graph, const FVector2D& Position)
{
    if (!Graph)
    {
        return nullptr;
    }
    
    UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
    SelfNode->NodePosX = Position.X;
    SelfNode->NodePosY = Position.Y;
    Graph->AddNode(SelfNode, true);
    SelfNode->CreateNewGuid();
    SelfNode->PostPlacedNewNode();
    SelfNode->AllocateDefaultPins();
    
    return SelfNode;
}

bool FEpicUnrealMCPCommonUtils::ConnectGraphNodes(UEdGraph* Graph, UEdGraphNode* SourceNode, const FString& SourcePinName, 
                                           UEdGraphNode* TargetNode, const FString& TargetPinName)
{
    if (!Graph || !SourceNode || !TargetNode)
    {
        return false;
    }
    
    UEdGraphPin* SourcePin = FindPin(SourceNode, SourcePinName, EGPD_Output);
    UEdGraphPin* TargetPin = FindPin(TargetNode, TargetPinName, EGPD_Input);
    
    if (SourcePin && TargetPin)
    {
        SourcePin->MakeLinkTo(TargetPin);
        return true;
    }
    
    return false;
}

UEdGraphPin* FEpicUnrealMCPCommonUtils::FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
    if (!Node)
    {
        return nullptr;
    }
    
    // Log all pins for debugging
    UE_LOG(LogTemp, Display, TEXT("FindPin: Looking for pin '%s' (Direction: %d) in node '%s'"), 
           *PinName, (int32)Direction, *Node->GetName());
    
    for (UEdGraphPin* Pin : Node->Pins)
    {
        UE_LOG(LogTemp, Display, TEXT("  - Available pin: '%s', Direction: %d, Category: %s"), 
               *Pin->PinName.ToString(), (int32)Pin->Direction, *Pin->PinType.PinCategory.ToString());
    }
    
    // First try exact match
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->PinName.ToString() == PinName && (Direction == EGPD_MAX || Pin->Direction == Direction))
        {
            UE_LOG(LogTemp, Display, TEXT("  - Found exact matching pin: '%s'"), *Pin->PinName.ToString());
            return Pin;
        }
    }
    
    // If no exact match and we're looking for a component reference, try case-insensitive match
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) && 
            (Direction == EGPD_MAX || Pin->Direction == Direction))
        {
            UE_LOG(LogTemp, Display, TEXT("  - Found case-insensitive matching pin: '%s'"), *Pin->PinName.ToString());
            return Pin;
        }
    }
    
    // If we're looking for a component output and didn't find it by name, try to find the first data output pin
    if (Direction == EGPD_Output && Cast<UK2Node_VariableGet>(Node) != nullptr)
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
            {
                UE_LOG(LogTemp, Display, TEXT("  - Found fallback data output pin: '%s'"), *Pin->PinName.ToString());
                return Pin;
            }
        }
    }
    
    UE_LOG(LogTemp, Warning, TEXT("  - No matching pin found for '%s'"), *PinName);
    return nullptr;
}

// Actor utilities
TSharedPtr<FJsonValue> FEpicUnrealMCPCommonUtils::ActorToJson(AActor* Actor)
{
    if (!Actor)
    {
        return MakeShared<FJsonValueNull>();
    }
    
    TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
    ActorObject->SetStringField(TEXT("name"), Actor->GetName());
    ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
    
    FVector Location = Actor->GetActorLocation();
    TArray<TSharedPtr<FJsonValue>> LocationArray;
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
    ActorObject->SetArrayField(TEXT("location"), LocationArray);
    
    FRotator Rotation = Actor->GetActorRotation();
    TArray<TSharedPtr<FJsonValue>> RotationArray;
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
    ActorObject->SetArrayField(TEXT("rotation"), RotationArray);
    
    FVector Scale = Actor->GetActorScale3D();
    TArray<TSharedPtr<FJsonValue>> ScaleArray;
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.X));
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Y));
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Z));
    ActorObject->SetArrayField(TEXT("scale"), ScaleArray);
    
    return MakeShared<FJsonValueObject>(ActorObject);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPCommonUtils::ActorToJsonObject(AActor* Actor, bool bDetailed)
{
    if (!Actor)
    {
        return nullptr;
    }
    
    TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
    ActorObject->SetStringField(TEXT("name"), Actor->GetName());
    ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
    
    FVector Location = Actor->GetActorLocation();
    TArray<TSharedPtr<FJsonValue>> LocationArray;
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
    ActorObject->SetArrayField(TEXT("location"), LocationArray);
    
    FRotator Rotation = Actor->GetActorRotation();
    TArray<TSharedPtr<FJsonValue>> RotationArray;
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
    ActorObject->SetArrayField(TEXT("rotation"), RotationArray);
    
    FVector Scale = Actor->GetActorScale3D();
    TArray<TSharedPtr<FJsonValue>> ScaleArray;
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.X));
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Y));
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Z));
    ActorObject->SetArrayField(TEXT("scale"), ScaleArray);
    
    return ActorObject;
}

UK2Node_Event* FEpicUnrealMCPCommonUtils::FindExistingEventNode(UEdGraph* Graph, const FString& EventName)
{
    if (!Graph)
    {
        return nullptr;
    }

    // Look for existing event nodes
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
        if (EventNode && EventNode->EventReference.GetMemberName() == FName(*EventName))
        {
            UE_LOG(LogTemp, Display, TEXT("Found existing event node with name: %s"), *EventName);
            return EventNode;
        }
    }

    return nullptr;
}

// Resolve dot/bracket notation ("Actions[0].Variants[3].MaxDistance") to a (leaf property, container address) pair.
// OutContainer is the address of the struct/UObject that directly contains the leaf property;
// pass it through Property->ContainerPtrToValuePtr<void>(OutContainer) to get the value address.
static bool ResolvePathContainer(UObject* Root, const FString& Path, FProperty*& OutLeaf, void*& OutContainer, FString& OutError)
{
    TArray<FString> Segments;
    Path.ParseIntoArray(Segments, TEXT("."), true);
    if (Segments.Num() == 0)
    {
        OutError = TEXT("Empty path");
        return false;
    }

    void* CurrentAddr = Root;
    UStruct* CurrentStruct = Root->GetClass();

    for (int32 i = 0; i < Segments.Num(); ++i)
    {
        const FString& Seg = Segments[i];
        FString PropName;
        int32 ArrayIdx = INDEX_NONE;

        const int32 BracketOpen = Seg.Find(TEXT("["));
        if (BracketOpen != INDEX_NONE)
        {
            PropName = Seg.Left(BracketOpen);
            const int32 BracketClose = Seg.Find(TEXT("]"));
            if (BracketClose == INDEX_NONE || BracketClose <= BracketOpen)
            {
                OutError = FString::Printf(TEXT("Invalid array bracket in segment: %s"), *Seg);
                return false;
            }
            const FString IdxStr = Seg.Mid(BracketOpen + 1, BracketClose - BracketOpen - 1);
            if (!IdxStr.IsNumeric())
            {
                OutError = FString::Printf(TEXT("Non-numeric array index: %s"), *IdxStr);
                return false;
            }
            ArrayIdx = FCString::Atoi(*IdxStr);
        }
        else
        {
            PropName = Seg;
        }

        FProperty* Property = CurrentStruct->FindPropertyByName(*PropName);
        if (!Property)
        {
            OutError = FString::Printf(TEXT("Property not found at segment '%s'"), *PropName);
            return false;
        }

        const bool bIsLast = (i == Segments.Num() - 1);

        if (ArrayIdx != INDEX_NONE)
        {
            FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
            if (!ArrayProp)
            {
                OutError = FString::Printf(TEXT("Property '%s' is not an array"), *PropName);
                return false;
            }

            void* ArrayAddr = ArrayProp->ContainerPtrToValuePtr<void>(CurrentAddr);
            FScriptArrayHelper Helper(ArrayProp, ArrayAddr);
            if (ArrayIdx < 0 || ArrayIdx >= Helper.Num())
            {
                OutError = FString::Printf(TEXT("Index %d out of range for '%s' (size %d)"), ArrayIdx, *PropName, Helper.Num());
                return false;
            }

            void* ElemAddr = Helper.GetRawPtr(ArrayIdx);
            FProperty* InnerProp = ArrayProp->Inner;

            if (bIsLast)
            {
                // Array inner has Offset_Internal = 0, so the container is the element address itself.
                OutContainer = ElemAddr;
                OutLeaf = InnerProp;
                return true;
            }

            FStructProperty* InnerStruct = CastField<FStructProperty>(InnerProp);
            if (!InnerStruct)
            {
                OutError = FString::Printf(TEXT("Cannot traverse into non-struct array inner at '%s'"), *PropName);
                return false;
            }
            CurrentAddr = ElemAddr;
            CurrentStruct = InnerStruct->Struct;
            continue;
        }

        if (bIsLast)
        {
            OutContainer = CurrentAddr;
            OutLeaf = Property;
            return true;
        }

        FStructProperty* StructProp = CastField<FStructProperty>(Property);
        if (!StructProp)
        {
            OutError = FString::Printf(TEXT("Intermediate segment '%s' is not a struct"), *PropName);
            return false;
        }
        CurrentAddr = StructProp->ContainerPtrToValuePtr<void>(CurrentAddr);
        CurrentStruct = StructProp->Struct;
    }

    OutError = TEXT("Path resolution fell through");
    return false;
}

// File-static recursive dispatcher. Invoked by SetObjectProperty once the leaf
// property + address are resolved, and called recursively below for array inner
// elements and struct sub-fields so that TArray<FStruct> and nested structs work.
static bool SetValueAtAddress(FProperty* Property, void* PropertyAddr,
                              const TSharedPtr<FJsonValue>& Value, UObject* Object,
                              const FString& PropertyName, FString& OutErrorMessage);

bool FEpicUnrealMCPCommonUtils::ResolvePropertyPath(UObject* Root, const FString& Path,
                                     FProperty*& OutLeaf, void*& OutContainer, FString& OutError)
{
    if (!Root)
    {
        OutError = TEXT("Null root object");
        return false;
    }
    return ResolvePathContainer(Root, Path, OutLeaf, OutContainer, OutError);
}

bool FEpicUnrealMCPCommonUtils::SetObjectProperty(UObject* Object, const FString& PropertyName,
                                     const TSharedPtr<FJsonValue>& Value, FString& OutErrorMessage)
{
    if (!Object)
    {
        OutErrorMessage = TEXT("Invalid object");
        return false;
    }

    FProperty* Property = nullptr;
    void* ContainerAddr = nullptr;

    // Dot/bracket notation: resolve path to (leaf property, container address).
    if (PropertyName.Contains(TEXT(".")) || PropertyName.Contains(TEXT("[")))
    {
        if (!ResolvePathContainer(Object, PropertyName, Property, ContainerAddr, OutErrorMessage))
        {
            return false;
        }
    }
    else
    {
        Property = Object->GetClass()->FindPropertyByName(*PropertyName);
        if (!Property)
        {
            OutErrorMessage = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
            return false;
        }
        ContainerAddr = Object;
    }

    void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(ContainerAddr);
    return SetValueAtAddress(Property, PropertyAddr, Value, Object, PropertyName, OutErrorMessage);
}

bool FEpicUnrealMCPCommonUtils::SetStructField(UScriptStruct* StructType, void* StructData,
                                               const FString& FieldName,
                                               const TSharedPtr<FJsonValue>& Value,
                                               UObject* OptionalOwner,
                                               FString& OutErrorMessage)
{
    if (!StructType || !StructData)
    {
        OutErrorMessage = TEXT("Invalid struct type or data pointer");
        return false;
    }

    FProperty* Property = StructType->FindPropertyByName(*FieldName);
    if (!Property)
    {
        OutErrorMessage = FString::Printf(TEXT("Struct field not found: %s.%s"),
            *StructType->GetName(), *FieldName);
        return false;
    }

    void* FieldAddr = Property->ContainerPtrToValuePtr<void>(StructData);
    return SetValueAtAddress(Property, FieldAddr, Value, OptionalOwner, FieldName, OutErrorMessage);
}

static bool SetValueAtAddress(FProperty* Property, void* PropertyAddr,
                              const TSharedPtr<FJsonValue>& Value, UObject* Object,
                              const FString& PropertyName, FString& OutErrorMessage)
{
    // Handle different property types
    if (Property->IsA<FBoolProperty>())
    {
        ((FBoolProperty*)Property)->SetPropertyValue(PropertyAddr, Value->AsBool());
        return true;
    }
    else if (Property->IsA<FIntProperty>())
    {
        int32 IntValue = static_cast<int32>(Value->AsNumber());
        FIntProperty* IntProperty = CastField<FIntProperty>(Property);
        if (IntProperty)
        {
            IntProperty->SetPropertyValue(PropertyAddr, IntValue);
            return true;
        }
    }
    else if (Property->IsA<FFloatProperty>())
    {
        ((FFloatProperty*)Property)->SetPropertyValue(PropertyAddr, Value->AsNumber());
        return true;
    }
    else if (Property->IsA<FStrProperty>())
    {
        ((FStrProperty*)Property)->SetPropertyValue(PropertyAddr, Value->AsString());
        return true;
    }
    else if (Property->IsA<FByteProperty>())
    {
        FByteProperty* ByteProp = CastField<FByteProperty>(Property);
        UEnum* EnumDef = ByteProp ? ByteProp->GetIntPropertyEnum() : nullptr;
        
        // If this is a TEnumAsByte property (has associated enum)
        if (EnumDef)
        {
            // Handle numeric value
            if (Value->Type == EJson::Number)
            {
                uint8 ByteValue = static_cast<uint8>(Value->AsNumber());
                ByteProp->SetPropertyValue(PropertyAddr, ByteValue);
                
                UE_LOG(LogTemp, Display, TEXT("Setting enum property %s to numeric value: %d"), 
                      *PropertyName, ByteValue);
                return true;
            }
            // Handle string enum value
            else if (Value->Type == EJson::String)
            {
                FString EnumValueName = Value->AsString();
                
                // Try to convert numeric string to number first
                if (EnumValueName.IsNumeric())
                {
                    uint8 ByteValue = FCString::Atoi(*EnumValueName);
                    ByteProp->SetPropertyValue(PropertyAddr, ByteValue);
                    
                    UE_LOG(LogTemp, Display, TEXT("Setting enum property %s to numeric string value: %s -> %d"), 
                          *PropertyName, *EnumValueName, ByteValue);
                    return true;
                }
                
                // Handle qualified enum names (e.g., "Player0" or "EAutoReceiveInput::Player0")
                if (EnumValueName.Contains(TEXT("::")))
                {
                    EnumValueName.Split(TEXT("::"), nullptr, &EnumValueName);
                }
                
                int64 EnumValue = EnumDef->GetValueByNameString(EnumValueName);
                if (EnumValue == INDEX_NONE)
                {
                    // Try with full name as fallback
                    EnumValue = EnumDef->GetValueByNameString(Value->AsString());
                }
                
                if (EnumValue != INDEX_NONE)
                {
                    ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(EnumValue));
                    
                    UE_LOG(LogTemp, Display, TEXT("Setting enum property %s to name value: %s -> %lld"), 
                          *PropertyName, *EnumValueName, EnumValue);
                    return true;
                }
                else
                {
                    // Log all possible enum values for debugging
                    UE_LOG(LogTemp, Warning, TEXT("Could not find enum value for '%s'. Available options:"), *EnumValueName);
                    for (int32 i = 0; i < EnumDef->NumEnums(); i++)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("  - %s (value: %d)"), 
                               *EnumDef->GetNameStringByIndex(i), EnumDef->GetValueByIndex(i));
                    }
                    
                    OutErrorMessage = FString::Printf(TEXT("Could not find enum value for '%s'"), *EnumValueName);
                    return false;
                }
            }
        }
        else
        {
            // Regular byte property
            uint8 ByteValue = static_cast<uint8>(Value->AsNumber());
            ByteProp->SetPropertyValue(PropertyAddr, ByteValue);
            return true;
        }
    }
    else if (Property->IsA<FEnumProperty>())
    {
        FEnumProperty* EnumProp = CastField<FEnumProperty>(Property);
        UEnum* EnumDef = EnumProp ? EnumProp->GetEnum() : nullptr;
        FNumericProperty* UnderlyingNumericProp = EnumProp ? EnumProp->GetUnderlyingProperty() : nullptr;
        
        if (EnumDef && UnderlyingNumericProp)
        {
            // Handle numeric value
            if (Value->Type == EJson::Number)
            {
                int64 EnumValue = static_cast<int64>(Value->AsNumber());
                UnderlyingNumericProp->SetIntPropertyValue(PropertyAddr, EnumValue);
                
                UE_LOG(LogTemp, Display, TEXT("Setting enum property %s to numeric value: %lld"), 
                      *PropertyName, EnumValue);
                return true;
            }
            // Handle string enum value
            else if (Value->Type == EJson::String)
            {
                FString EnumValueName = Value->AsString();
                
                // Try to convert numeric string to number first
                if (EnumValueName.IsNumeric())
                {
                    int64 EnumValue = FCString::Atoi64(*EnumValueName);
                    UnderlyingNumericProp->SetIntPropertyValue(PropertyAddr, EnumValue);
                    
                    UE_LOG(LogTemp, Display, TEXT("Setting enum property %s to numeric string value: %s -> %lld"), 
                          *PropertyName, *EnumValueName, EnumValue);
                    return true;
                }
                
                // Handle qualified enum names
                if (EnumValueName.Contains(TEXT("::")))
                {
                    EnumValueName.Split(TEXT("::"), nullptr, &EnumValueName);
                }
                
                int64 EnumValue = EnumDef->GetValueByNameString(EnumValueName);
                if (EnumValue == INDEX_NONE)
                {
                    // Try with full name as fallback
                    EnumValue = EnumDef->GetValueByNameString(Value->AsString());
                }
                
                if (EnumValue != INDEX_NONE)
                {
                    UnderlyingNumericProp->SetIntPropertyValue(PropertyAddr, EnumValue);
                    
                    UE_LOG(LogTemp, Display, TEXT("Setting enum property %s to name value: %s -> %lld"), 
                          *PropertyName, *EnumValueName, EnumValue);
                    return true;
                }
                else
                {
                    // Log all possible enum values for debugging
                    UE_LOG(LogTemp, Warning, TEXT("Could not find enum value for '%s'. Available options:"), *EnumValueName);
                    for (int32 i = 0; i < EnumDef->NumEnums(); i++)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("  - %s (value: %d)"), 
                               *EnumDef->GetNameStringByIndex(i), EnumDef->GetValueByIndex(i));
                    }
                    
                    OutErrorMessage = FString::Printf(TEXT("Could not find enum value for '%s'"), *EnumValueName);
                    return false;
                }
            }
        }
    }
    
    // Catch-all for any remaining numeric type (FDoubleProperty, FInt64Property,
    // FInt8/16/64Property, FUInt16/32/64Property). Critical for UE5 LWC — FVector
    // fields are doubles, so without this, setting any vector sub-field fails.
    // Placed after the explicit bool/int/float/byte/enum chain so it does not
    // shadow those.
    if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
    {
        if (Value->Type == EJson::Number || Value->Type == EJson::String)
        {
            const double Num = Value->AsNumber();
            if (NumProp->IsFloatingPoint())
            {
                NumProp->SetFloatingPointPropertyValue(PropertyAddr, Num);
            }
            else
            {
                NumProp->SetIntPropertyValue(PropertyAddr, static_cast<int64>(Num));
            }
            return true;
        }
        if (Value->Type == EJson::Boolean)
        {
            NumProp->SetIntPropertyValue(PropertyAddr, Value->AsBool() ? int64(1) : int64(0));
            return true;
        }
        OutErrorMessage = FString::Printf(TEXT("Numeric property %s requires a number"), *PropertyName);
        return false;
    }

    // Handle FArrayProperty (TArray<T>) — accepts JSON arrays. Each element is
    // dispatched through SetValueAtAddress so arbitrary inner types work,
    // including TArray<FStruct> with FGameplayTag / TSoftObjectPtr fields.
    if (Property->IsA<FArrayProperty>())
    {
        FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
        if (!ArrayProp || Value->Type != EJson::Array)
        {
            OutErrorMessage = FString::Printf(TEXT("Array property %s requires a JSON array"), *PropertyName);
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>& JsonArray = Value->AsArray();
        FScriptArrayHelper ArrayHelper(ArrayProp, PropertyAddr);
        ArrayHelper.EmptyValues();

        FProperty* InnerProp = ArrayProp->Inner;
        int32 SetCount = 0;
        TArray<FString> PerElementErrors;

        for (int32 Idx = 0; Idx < JsonArray.Num(); ++Idx)
        {
            const int32 NewIdx = ArrayHelper.AddValue();
            void* ElementAddr = ArrayHelper.GetRawPtr(NewIdx);

            const FString ElementLabel = FString::Printf(TEXT("%s[%d]"), *PropertyName, Idx);
            FString ElemError;
            if (SetValueAtAddress(InnerProp, ElementAddr, JsonArray[Idx], Object, ElementLabel, ElemError))
            {
                SetCount++;
            }
            else
            {
                PerElementErrors.Add(ElemError.IsEmpty()
                    ? FString::Printf(TEXT("%s: unsupported element"), *ElementLabel)
                    : ElemError);
            }
        }

        UE_LOG(LogTemp, Display, TEXT("Set array property %s: %d/%d elements"), *PropertyName, SetCount, JsonArray.Num());

        // Always surface per-element errors. With the prior contract, partial success
        // (some elements set, others failed) returned true with empty OutErrorMessage,
        // hiding silent data loss inside TArray<FStruct>. Now bool indicates "anything
        // changed"; OutErrorMessage carries warnings even on partial success so the
        // caller (HandleUpdateDataAsset) can route them into property_warnings.
        if (PerElementErrors.Num() > 0)
        {
            OutErrorMessage = FString::Join(PerElementErrors, TEXT("; "));
        }
        if (SetCount == 0 && PerElementErrors.Num() > 0)
        {
            return false;
        }
        return SetCount > 0 || JsonArray.Num() == 0;
    }

    // Handle FSetProperty (TSet<T>) — accepts JSON array. Duplicates are dropped.
    if (Property->IsA<FSetProperty>())
    {
        FSetProperty* SetProp = CastField<FSetProperty>(Property);
        if (!SetProp || Value->Type != EJson::Array)
        {
            OutErrorMessage = FString::Printf(TEXT("Set property %s requires a JSON array"), *PropertyName);
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>& JsonArray = Value->AsArray();
        FScriptSetHelper SetHelper(SetProp, PropertyAddr);
        SetHelper.EmptyElements();

        FProperty* InnerProp = SetProp->ElementProp;
        int32 SetCount = 0;
        TArray<FString> PerElementErrors;

        for (int32 Idx = 0; Idx < JsonArray.Num(); ++Idx)
        {
            const int32 NewIdx = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
            void* ElementAddr = SetHelper.GetElementPtr(NewIdx);

            const FString ElementLabel = FString::Printf(TEXT("%s{%d}"), *PropertyName, Idx);
            FString ElemError;
            if (SetValueAtAddress(InnerProp, ElementAddr, JsonArray[Idx], Object, ElementLabel, ElemError))
            {
                SetCount++;
            }
            else
            {
                PerElementErrors.Add(ElemError.IsEmpty()
                    ? FString::Printf(TEXT("%s: unsupported element"), *ElementLabel)
                    : ElemError);
            }
        }

        SetHelper.Rehash();

        UE_LOG(LogTemp, Display, TEXT("Set set-property %s: %d/%d elements"), *PropertyName, SetCount, JsonArray.Num());

        if (SetCount == 0 && PerElementErrors.Num() > 0)
        {
            OutErrorMessage = FString::Join(PerElementErrors, TEXT("; "));
            return false;
        }
        return SetCount > 0 || JsonArray.Num() == 0;
    }

    // Handle FMapProperty (TMap<K,V>) — accepts JSON object (string-keyed) for scalar/name/enum keys,
    // OR an array of {"key":..., "value":...} objects for arbitrary key types.
    //
    // MERGE SEMANTICS (changed 2026-05-26 per PRK feedback_update_data_asset_tmap_replace):
    // By default, JSON entries are UPSERTED into the existing map — keys present
    // in JSON replace existing values for that key; keys not in JSON are preserved.
    // This is the intuitive behavior and matches partial-update intent.
    //
    // For replace semantics (clear-then-fill, the OLD behavior), pass a sentinel
    // key "__replace": true inside the JSON object. The sentinel is consumed
    // before iteration and triggers MapHelper.EmptyValues() first.
    //
    // Rationale: callers that previously sent the full map state (the documented
    // workaround) are unaffected — sending {A:1,B:2,C:3} as a merge over {A:0,D:9}
    // still produces {A:1,B:2,C:3,D:9}, which is what the workaround intended to
    // achieve (full state) PLUS the bonus of preserving keys the caller didn't
    // touch. Callers that need true wipe-and-replace opt in explicitly.
    if (Property->IsA<FMapProperty>())
    {
        FMapProperty* MapProp = CastField<FMapProperty>(Property);
        if (!MapProp)
        {
            OutErrorMessage = FString::Printf(TEXT("Map property %s: cast failed"), *PropertyName);
            return false;
        }

        FScriptMapHelper MapHelper(MapProp, PropertyAddr);

        // Detect __replace sentinel before touching the map.
        bool bReplaceAll = false;
        if (Value->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject> SentinelObj = Value->AsObject();
            if (SentinelObj.IsValid())
            {
                const TSharedPtr<FJsonValue> SentinelVal = SentinelObj->TryGetField(TEXT("__replace"));
                if (SentinelVal.IsValid() && SentinelVal->Type == EJson::Boolean)
                {
                    bReplaceAll = SentinelVal->AsBool();
                }
            }
        }
        if (bReplaceAll)
        {
            MapHelper.EmptyValues();
        }

        FProperty* KeyProp = MapProp->KeyProp;
        FProperty* ValueProp = MapProp->ValueProp;
        int32 SetCount = 0;
        TArray<FString> PerEntryErrors;

        // Upsert: find existing index for a key (by comparing serialized key bytes
        // via FScriptMapHelper::FindMapIndexWithKey), replace value if found, else add.
        auto UpsertPair = [&](const TSharedPtr<FJsonValue>& InKeyJson, const TSharedPtr<FJsonValue>& InValueJson, const FString& Label) -> bool
        {
            // Stage the key into a temporary buffer so we can probe existing entries.
            TArray<uint8> KeyBuf;
            KeyBuf.SetNumZeroed(KeyProp->GetSize());
            KeyProp->InitializeValue(KeyBuf.GetData());

            FString KeyErr;
            if (!SetValueAtAddress(KeyProp, KeyBuf.GetData(), InKeyJson, Object, Label + TEXT(".key"), KeyErr))
            {
                PerEntryErrors.Add(KeyErr.IsEmpty() ? FString::Printf(TEXT("%s.key: unsupported"), *Label) : KeyErr);
                KeyProp->DestroyValue(KeyBuf.GetData());
                return false;
            }

            // FindMapIndexWithKey hashes the staged key to look up an existing entry.
            const int32 ExistingIdx = MapHelper.FindMapIndexWithKey(KeyBuf.GetData());
            int32 TargetIdx;
            if (ExistingIdx != INDEX_NONE)
            {
                // Upsert: keep the existing key, overwrite the value.
                TargetIdx = ExistingIdx;
            }
            else
            {
                // Insert new entry.
                TargetIdx = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
                void* NewKeyAddr = MapHelper.GetKeyPtr(TargetIdx);
                KeyProp->CopyCompleteValue(NewKeyAddr, KeyBuf.GetData());
            }
            KeyProp->DestroyValue(KeyBuf.GetData());

            void* ValueAddr = MapHelper.GetValuePtr(TargetIdx);
            FString ValErr;
            if (!SetValueAtAddress(ValueProp, ValueAddr, InValueJson, Object, Label + TEXT(".value"), ValErr))
            {
                PerEntryErrors.Add(ValErr.IsEmpty() ? FString::Printf(TEXT("%s.value: unsupported"), *Label) : ValErr);
                return false;
            }
            return true;
        };

        // Compat shim for the array-of-{key,value} input form: always treat as upsert.
        auto AddPair = UpsertPair;

        if (Value->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject> Obj = Value->AsObject();
            if (Obj.IsValid())
            {
                int32 EntryIdx = 0;
                for (const auto& Pair : Obj->Values)
                {
                    // Skip the merge-control sentinel; it has already been consumed.
                    if (Pair.Key == TEXT("__replace")) continue;

                    const FString EntryLabel = FString::Printf(TEXT("%s[%d]"), *PropertyName, EntryIdx++);
                    TSharedPtr<FJsonValue> KeyJson = MakeShared<FJsonValueString>(FString(*Pair.Key));
                    if (AddPair(KeyJson, Pair.Value, EntryLabel))
                    {
                        SetCount++;
                    }
                }
            }
        }
        else if (Value->Type == EJson::Array)
        {
            const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
            for (int32 Idx = 0; Idx < Arr.Num(); ++Idx)
            {
                const FString EntryLabel = FString::Printf(TEXT("%s[%d]"), *PropertyName, Idx);
                if (!Arr[Idx].IsValid() || Arr[Idx]->Type != EJson::Object)
                {
                    PerEntryErrors.Add(FString::Printf(TEXT("%s: entry must be an object with 'key' and 'value'"), *EntryLabel));
                    continue;
                }
                const TSharedPtr<FJsonObject> Entry = Arr[Idx]->AsObject();
                const TSharedPtr<FJsonValue> KeyJson = Entry->TryGetField(TEXT("key"));
                const TSharedPtr<FJsonValue> ValJson = Entry->TryGetField(TEXT("value"));
                if (!KeyJson.IsValid() || !ValJson.IsValid())
                {
                    PerEntryErrors.Add(FString::Printf(TEXT("%s: missing 'key' or 'value'"), *EntryLabel));
                    continue;
                }
                if (AddPair(KeyJson, ValJson, EntryLabel))
                {
                    SetCount++;
                }
            }
        }
        else
        {
            OutErrorMessage = FString::Printf(TEXT("Map property %s requires a JSON object or array of {key,value}"), *PropertyName);
            return false;
        }

        MapHelper.Rehash();

        UE_LOG(LogTemp, Display, TEXT("Set map-property %s: %d entries"), *PropertyName, SetCount);

        if (SetCount == 0 && PerEntryErrors.Num() > 0)
        {
            OutErrorMessage = FString::Join(PerEntryErrors, TEXT("; "));
            return false;
        }
        return true;
    }

    // Handle FSoftClassProperty (TSoftClassPtr<T>) — accepts class string/path. MUST be checked
    // before FSoftObjectProperty because it is a subclass.
    if (Property->IsA<FSoftClassProperty>())
    {
        FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property);
        if (!SoftClassProp)
        {
            OutErrorMessage = FString::Printf(TEXT("Soft class property %s: cast failed"), *PropertyName);
            return false;
        }

        if (Value->Type == EJson::Null)
        {
            SoftClassProp->SetPropertyValue(PropertyAddr, FSoftObjectPtr());
            return true;
        }

        if (Value->Type != EJson::String)
        {
            OutErrorMessage = FString::Printf(TEXT("Soft class property %s requires a class path string"), *PropertyName);
            return false;
        }

        const FString ClassStr = Value->AsString();
        if (ClassStr.IsEmpty() || ClassStr == TEXT("None") || ClassStr == TEXT("null"))
        {
            SoftClassProp->SetPropertyValue(PropertyAddr, FSoftObjectPtr());
            return true;
        }

        // If caller gave a /Script/... path, use directly. Otherwise resolve via module-wide lookup.
        FString ResolvedPath = ClassStr;
        if (!ClassStr.StartsWith(TEXT("/")))
        {
            UClass* Resolved = FEpicUnrealMCPCommonUtils::ResolveClass(ClassStr);
            if (!Resolved)
            {
                OutErrorMessage = FString::Printf(TEXT("Could not resolve class '%s' for soft class property %s"), *ClassStr, *PropertyName);
                return false;
            }
            ResolvedPath = Resolved->GetPathName();
        }

        SoftClassProp->SetPropertyValue(PropertyAddr, FSoftObjectPtr(FSoftObjectPath(ResolvedPath)));
        return true;
    }

    // Handle FSoftObjectProperty (TSoftObjectPtr<T>) — accepts string asset paths.
    //
    // Past failure mode (the silent-null-write that destroyed
    // DA_Sword_OneHand_Moveset's TArray<FPRKAttackVariant>::Montage refs):
    //   1. Caller passes a JSON object form like {"AssetPathName": ...} or a
    //      plain asset name without /Game/ prefix.
    //   2. Old branch only handled EJson::String → FSoftObjectPath(string), or
    //      else returned false with no diagnostic. Worse, the SetPropertyValue
    //      path didn't trip ImportText's path-fixup, so a malformed input was
    //      silently coerced to FSoftObjectPath() (null) via constructor.
    //   3. properties_set still incremented because bOk=true was returned.
    //
    // The hardened path:
    //   - Accept Null / Object {AssetPath/AssetPathName} / String input.
    //   - For string input, use Property->ImportText_Direct (the engine's
    //     blessed text-to-property API used by the property editor) so
    //     redirect collection and path normalization run.
    //   - Read back after write and compare against the requested path. If
    //     the engine silently dropped the write (invalid path, unsupported
    //     class match, etc.) we fail explicitly rather than reporting success.
    if (Property->IsA<FSoftObjectProperty>())
    {
        FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Property);
        if (!SoftProp)
        {
            OutErrorMessage = FString::Printf(TEXT("Soft object property %s: cast failed"), *PropertyName);
            return false;
        }

        // Null / empty string / "None" → write empty soft pointer.
        const auto WriteNull = [&]()
        {
            SoftProp->SetPropertyValue(PropertyAddr, FSoftObjectPtr());
        };

        if (Value->Type == EJson::Null)
        {
            WriteNull();
            return true;
        }

        // Resolve the desired path from supported JSON shapes. We accept three
        // input forms because update_data_asset and write_data_table_row
        // serialize soft refs as strings, but read_data_asset / serialization
        // round-tripping in callers (and JSON authors) sometimes produce the
        // structured form. Without this, JSON-object input silently fell
        // through to "requires a string asset path" and returned false.
        FString DesiredPathStr;
        if (Value->Type == EJson::String)
        {
            DesiredPathStr = Value->AsString();
        }
        else if (Value->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject> Obj = Value->AsObject();
            if (Obj.IsValid())
            {
                if (!Obj->TryGetStringField(TEXT("AssetPath"), DesiredPathStr))
                {
                    Obj->TryGetStringField(TEXT("AssetPathName"), DesiredPathStr);
                }
                FString SubPath;
                if (Obj->TryGetStringField(TEXT("SubPath"), SubPath) && !SubPath.IsEmpty())
                {
                    DesiredPathStr += FString::Printf(TEXT(":%s"), *SubPath);
                }
            }
        }
        else
        {
            OutErrorMessage = FString::Printf(
                TEXT("Soft object property %s requires a string asset path or null (got JSON type %d)"),
                *PropertyName, static_cast<int32>(Value->Type));
            return false;
        }

        if (DesiredPathStr.IsEmpty() || DesiredPathStr == TEXT("None") || DesiredPathStr == TEXT("null"))
        {
            WriteNull();
            return true;
        }

        // Pre-load the target. Empirical: ImportText_Direct on a soft-pointer
        // property silently writes a null path when the target package isn't
        // already in memory — the redirect collector / asset registry doesn't
        // canonicalize an unregistered path, so the write succeeds structurally
        // but the value is empty. Repro: writing FactionAsset on freshly-created
        // FactionDefinitions where the NinjaFactions target uasset hadn't yet
        // been touched in the session; the first three writes silently nulled,
        // a fourth succeeded only because another op had loaded that target.
        //
        // Best-effort load. If the target doesn't exist (bad path), the load
        // returns nullptr and the post-write behavioral check below surfaces
        // the failure with the existing diagnostic. If the load succeeds, the
        // package is registered and ImportText canonicalizes correctly.
        UClass* PreloadClass = SoftProp->PropertyClass ? SoftProp->PropertyClass.Get() : UObject::StaticClass();
        StaticLoadObject(PreloadClass, nullptr, *DesiredPathStr, nullptr, LOAD_NoWarn);

        // Primary write: ImportText_Direct. This is the engine's text-to-property
        // API and runs the redirect collector / path canonicalization that
        // SetPropertyValue does not. Form expected by FSoftObjectProperty
        // ImportText is the bare asset path string ("/Game/Foo/Bar.Bar") with
        // optional sub-path after a colon.
        const TCHAR* ImportResult = Property->ImportText_Direct(*DesiredPathStr, PropertyAddr, Object, PPF_None);

        // Fallback: direct SetPropertyValue if ImportText didn't consume input.
        if (ImportResult == nullptr)
        {
            FSoftObjectPath FallbackPath(DesiredPathStr);
            SoftProp->SetPropertyValue(PropertyAddr, FSoftObjectPtr(FallbackPath));
        }

        // Behavioral verification (NOT just structural).
        //
        // A naive read-back at PropertyAddr only confirms "the bytes I wrote
        // are still at the address I wrote them to" — useless when the bug
        // is that PropertyAddr was the wrong address in the first place
        // (e.g., bad ResolvePathContainer arithmetic on TArray<FStruct>
        // nested fields). A previous fix attempted this and reported success
        // while the actual asset field stayed null, because the read was
        // hitting the same wrong address as the write.
        //
        // The behavioral test: re-resolve the path from the root via a
        // FRESH ResolvePathContainer pass and ExportText from that address.
        // If the two exports disagree, the address arithmetic is broken
        // (write went somewhere ResolvePathContainer-from-root doesn't
        // see). If both agree but are empty when the input wasn't, the
        // engine accepted ImportText then dropped the value — typically a
        // PostEditChangeProperty handler on the asset class.
        FString WrittenAtWriteAddr;
        Property->ExportText_Direct(WrittenAtWriteAddr, PropertyAddr, PropertyAddr, Object, PPF_None);

        // Re-resolve the path from the root for the behavioral check.
        // PropertyName is the original full path (e.g.
        // "Actions[0].Variants[0].Montage") — same path the caller used.
        // Object is the asset root.
        bool bAddressDisagreement = false;
        bool bReResolveFailed = false;
        FString WrittenAtFreshResolve;
        if (Object != nullptr
            && (PropertyName.Contains(TEXT(".")) || PropertyName.Contains(TEXT("["))))
        {
            FProperty* FreshLeaf = nullptr;
            void* FreshContainer = nullptr;
            FString FreshErr;
            if (ResolvePathContainer(Object, PropertyName, FreshLeaf, FreshContainer, FreshErr)
                && FreshLeaf != nullptr
                && FreshContainer != nullptr)
            {
                void* FreshAddr = FreshLeaf->ContainerPtrToValuePtr<void>(FreshContainer);
                FreshLeaf->ExportText_Direct(WrittenAtFreshResolve, FreshAddr, FreshAddr, Object, PPF_None);
                if (FreshAddr != PropertyAddr || WrittenAtFreshResolve != WrittenAtWriteAddr)
                {
                    bAddressDisagreement = true;
                }
            }
            else
            {
                bReResolveFailed = true;
            }
        }
        else
        {
            // Non-pathed write — write address IS the address ResolvePathContainer
            // would return; behavioral check collapses to structural.
            WrittenAtFreshResolve = WrittenAtWriteAddr;
        }

        const FSoftObjectPath DesiredPath(DesiredPathStr);
        const FString CanonicalDesired = DesiredPath.ToString();

        if (bAddressDisagreement)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Soft object property %s: address arithmetic disagreement. ")
                TEXT("Write went to PropertyAddr=%p (read-back '%s') but a fresh root-relative resolve ")
                TEXT("returns FreshAddr (read-back '%s'). The write landed somewhere the path resolver ")
                TEXT("does not reach — likely a TArray<FStruct> inner-element offset bug in ")
                TEXT("ResolvePathContainer. Requested: '%s'."),
                *PropertyName, PropertyAddr, *WrittenAtWriteAddr, *WrittenAtFreshResolve, *DesiredPathStr);
            return false;
        }
        if (bReResolveFailed)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Soft object property %s: write completed but root-relative re-resolve failed. ")
                TEXT("Cannot verify persistence; treating as failure to be safe. Requested: '%s'."),
                *PropertyName, *DesiredPathStr);
            return false;
        }
        if (WrittenAtFreshResolve.IsEmpty() && !CanonicalDesired.IsEmpty())
        {
            OutErrorMessage = FString::Printf(
                TEXT("Soft object property %s: write accepted by ImportText but value did not persist ")
                TEXT("(fresh-resolve read-back is empty). Requested '%s'. Most likely cause: a ")
                TEXT("PostEditChangeProperty handler on the asset class clears this field. Check the ")
                TEXT("asset's UClass::PostEditChangeProperty for cleanup logic touching this property."),
                *PropertyName, *DesiredPathStr);
            return false;
        }

        UE_LOG(LogTemp, Display, TEXT("Setting soft object property %s to: %s (verified: '%s')"),
               *PropertyName, *DesiredPathStr, *WrittenAtFreshResolve);
        return true;
    }

    // Handle FClassProperty (TSubclassOf<T>) — accepts class path or short name.
    // MUST be checked before FObjectPropertyBase because it is a subclass.
    if (Property->IsA<FClassProperty>())
    {
        FClassProperty* ClassProp = CastField<FClassProperty>(Property);
        if (!ClassProp)
        {
            OutErrorMessage = FString::Printf(TEXT("Class property %s: cast failed"), *PropertyName);
            return false;
        }

        if (Value->Type == EJson::Null)
        {
            ClassProp->SetObjectPropertyValue(PropertyAddr, nullptr);
            return true;
        }

        if (Value->Type != EJson::String)
        {
            OutErrorMessage = FString::Printf(TEXT("Class property %s requires a class path string"), *PropertyName);
            return false;
        }

        const FString ClassStr = Value->AsString();
        if (ClassStr.IsEmpty() || ClassStr == TEXT("None") || ClassStr == TEXT("null"))
        {
            ClassProp->SetObjectPropertyValue(PropertyAddr, nullptr);
            return true;
        }

        UClass* Resolved = nullptr;
        if (ClassStr.StartsWith(TEXT("/")))
        {
            Resolved = LoadObject<UClass>(nullptr, *ClassStr);
        }
        if (!Resolved)
        {
            Resolved = FEpicUnrealMCPCommonUtils::ResolveClass(ClassStr);
        }
        if (!Resolved)
        {
            OutErrorMessage = FString::Printf(TEXT("Could not resolve class '%s' for class property %s"), *ClassStr, *PropertyName);
            return false;
        }

        // Enforce MetaClass constraint if present
        if (ClassProp->MetaClass && !Resolved->IsChildOf(ClassProp->MetaClass))
        {
            OutErrorMessage = FString::Printf(TEXT("Class '%s' is not a subclass of required '%s' for property %s"),
                *Resolved->GetName(), *ClassProp->MetaClass->GetName(), *PropertyName);
            return false;
        }

        ClassProp->SetObjectPropertyValue(PropertyAddr, Resolved);
        return true;
    }

    // FName property
    if (Property->IsA<FNameProperty>())
    {
        CastField<FNameProperty>(Property)->SetPropertyValue(PropertyAddr, FName(*Value->AsString()));
        return true;
    }
    // FText property
    if (Property->IsA<FTextProperty>())
    {
        CastField<FTextProperty>(Property)->SetPropertyValue(PropertyAddr, FText::FromString(Value->AsString()));
        return true;
    }
    // Object reference (TObjectPtr, UObject*). Three accepted JSON shapes:
    //   - null                                → clear
    //   - "/Game/.../Asset.Asset"             → load + reference (legacy asset-path form)
    //   - { "class": "...", "properties":{} } → instanced subobject (Instanced UPROPERTY only)
    // Cross-asset reuse of an instanced subobject is impossible by design (instanced
    // subobjects are owned by their outer), so when the asset shows a "_C_0" suffix
    // the only legal write is to construct a fresh instance via the object form.
    if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
    {
        const bool bInstanced = Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance);

        if (Value->Type == EJson::Null)
        {
            ObjProp->SetObjectPropertyValue(PropertyAddr, nullptr);
            return true;
        }

        if (Value->Type == EJson::Object)
        {
            if (!bInstanced)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Object property %s is not Instanced — JSON object syntax is reserved for ")
                    TEXT("UPROPERTY(Instanced) fields. Pass an asset path string instead."),
                    *PropertyName);
                return false;
            }

            const TSharedPtr<FJsonObject> SpecObj = Value->AsObject();
            if (!SpecObj.IsValid())
            {
                OutErrorMessage = FString::Printf(TEXT("Instanced property %s: malformed JSON object"), *PropertyName);
                return false;
            }

            UObject* Existing = ObjProp->GetObjectPropertyValue(PropertyAddr);
            UObject* TargetSub = nullptr;

            FString ClassName;
            // Accept both "class" (canonical write shape) and "_class" (the
            // shape SerializeObjectProperties emits, so read→edit→write
            // round-trips without manual key rename).
            if (!SpecObj->TryGetStringField(TEXT("class"), ClassName))
            {
                SpecObj->TryGetStringField(TEXT("_class"), ClassName);
            }

            if (!ClassName.IsEmpty())
            {
                UClass* Resolved = FEpicUnrealMCPCommonUtils::ResolveClass(ClassName);
                if (!Resolved)
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Could not resolve class '%s' for instanced property %s"),
                        *ClassName, *PropertyName);
                    return false;
                }
                if (!ObjProp->PropertyClass || !Resolved->IsChildOf(ObjProp->PropertyClass))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Class '%s' is not a subclass of required '%s' for instanced property %s"),
                        *Resolved->GetName(),
                        ObjProp->PropertyClass ? *ObjProp->PropertyClass->GetName() : TEXT("<none>"),
                        *PropertyName);
                    return false;
                }
                if (Resolved->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Class '%s' is abstract/deprecated and cannot be instanced for property %s"),
                        *Resolved->GetName(), *PropertyName);
                    return false;
                }

                // Reuse the existing instance when its exact class matches; this preserves
                // any sibling field the JSON didn't mention. Otherwise build a fresh one.
                // Outer is the immediate UPROPERTY container so package serialization picks
                // up the subobject inline (CPF_InstancedReference).
                if (Existing && Existing->GetClass() == Resolved)
                {
                    TargetSub = Existing;
                }
                else
                {
                    UObject* Outer = Object ? Object : GetTransientPackage();
                    TargetSub = NewObject<UObject>(Outer, Resolved, NAME_None, RF_Transactional);
                    ObjProp->SetObjectPropertyValue(PropertyAddr, TargetSub);
                }
            }
            else
            {
                if (!Existing)
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Instanced property %s has no current instance — provide a 'class' field to construct one"),
                        *PropertyName);
                    return false;
                }
                TargetSub = Existing;
            }

            // Apply nested fields. "properties" is the canonical container; iterate it
            // through SetObjectProperty so dotted paths and array indices keep working.
            // Object becomes TargetSub for this recursion so any further-nested instanced
            // subobjects are outered correctly.
            const TSharedPtr<FJsonObject>* InnerPtr = nullptr;
            if (SpecObj->TryGetObjectField(TEXT("properties"), InnerPtr) && InnerPtr && InnerPtr->IsValid())
            {
                TArray<FString> SubErrors;
                for (const auto& FieldPair : (*InnerPtr)->Values)
                {
                    FString SubError;
                    const bool bSubOk = FEpicUnrealMCPCommonUtils::SetObjectProperty(
                        TargetSub, FString(*FieldPair.Key), FieldPair.Value, SubError);
                    if (!bSubOk)
                    {
                        SubErrors.Add(FString::Printf(TEXT("%s.%s: %s"),
                            *PropertyName, *FieldPair.Key, *SubError));
                    }
                    else if (!SubError.IsEmpty())
                    {
                        SubErrors.Add(FString::Printf(TEXT("%s.%s [partial]: %s"),
                            *PropertyName, *FieldPair.Key, *SubError));
                    }
                }
                if (SubErrors.Num() > 0)
                {
                    OutErrorMessage = FString::Join(SubErrors, TEXT("; "));
                }
            }

            return true;
        }

        if (Value->Type == EJson::String)
        {
            FString AssetPath = Value->AsString();
            if (AssetPath.IsEmpty() || AssetPath == TEXT("None") || AssetPath == TEXT("null"))
            {
                ObjProp->SetObjectPropertyValue(PropertyAddr, nullptr);
                return true;
            }
            UObject* LoadedObj = StaticLoadObject(ObjProp->PropertyClass, nullptr, *AssetPath);
            if (LoadedObj)
            {
                ObjProp->SetObjectPropertyValue(PropertyAddr, LoadedObj);
                return true;
            }
            OutErrorMessage = FString::Printf(TEXT("Failed to load object: %s"), *AssetPath);
            return false;
        }

        OutErrorMessage = FString::Printf(
            TEXT("Object property %s: unsupported JSON value type — expected null, asset-path string, or {\"class\":..., \"properties\":...}"),
            *PropertyName);
        return false;
    }

    // Handle FStructProperty — special-case common types, then recurse, then ImportText fallback
    if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
    {
        const FName StructName = StructProp->Struct->GetFName();

        // FSoftObjectPath — accepts string path
        static const FName NAME_SoftObjectPath(TEXT("SoftObjectPath"));
        if (StructName == NAME_SoftObjectPath && Value->Type == EJson::String)
        {
            *static_cast<FSoftObjectPath*>(PropertyAddr) = FSoftObjectPath(Value->AsString());
            return true;
        }

        // FGameplayTag — accepts string tag name (or null/empty/"None" → empty tag)
        static const FName NAME_GameplayTag(TEXT("GameplayTag"));
        if (StructName == NAME_GameplayTag)
        {
            if (Value->Type == EJson::Null)
            {
                *static_cast<FGameplayTag*>(PropertyAddr) = FGameplayTag();
                return true;
            }
            if (Value->Type == EJson::String)
            {
                const FString TagStr = Value->AsString();
                if (TagStr.IsEmpty() || TagStr == TEXT("None"))
                {
                    *static_cast<FGameplayTag*>(PropertyAddr) = FGameplayTag();
                    return true;
                }
                // Validate the tag is registered. RequestGameplayTag with
                // bErrorIfNotFound=false silently returns FGameplayTag() for
                // unknown tags — this is exactly how Action.Push got persisted
                // as ActionTag=None when the tag was added to the ini but the
                // GameplayTagsManager hadn't reloaded yet.
                const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), /*ErrorIfNotFound=*/false);
                if (!Tag.IsValid())
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("GameplayTag '%s' for property %s is not registered with the GameplayTagsManager. ")
                        TEXT("Add it to DefaultGameplayTags.ini and restart the editor (or use a runtime reload) before referencing it."),
                        *TagStr, *PropertyName);
                    return false;
                }
                *static_cast<FGameplayTag*>(PropertyAddr) = Tag;
                return true;
            }
            OutErrorMessage = FString::Printf(TEXT("GameplayTag property %s requires a string tag name or null"), *PropertyName);
            return false;
        }

        // FGameplayTagContainer — accepts array of strings, or object with "Tags" array
        static const FName NAME_GameplayTagContainer(TEXT("GameplayTagContainer"));
        if (StructName == NAME_GameplayTagContainer)
        {
            const TArray<TSharedPtr<FJsonValue>>* TagArray = nullptr;

            if (Value->Type == EJson::Array)
            {
                TagArray = &Value->AsArray();
            }
            else if (Value->Type == EJson::Object)
            {
                const TSharedPtr<FJsonObject> Obj = Value->AsObject();
                if (Obj.IsValid())
                {
                    Obj->TryGetArrayField(TEXT("Tags"), TagArray);
                }
            }

            if (TagArray)
            {
                FGameplayTagContainer* Container = static_cast<FGameplayTagContainer*>(PropertyAddr);
                Container->Reset();
                TArray<FString> UnknownTags;
                for (const TSharedPtr<FJsonValue>& Entry : *TagArray)
                {
                    if (!Entry.IsValid() || Entry->Type != EJson::String)
                    {
                        continue;
                    }
                    const FString TagStr = Entry->AsString();
                    if (TagStr.IsEmpty() || TagStr == TEXT("None"))
                    {
                        continue;
                    }
                    const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), /*ErrorIfNotFound=*/false);
                    if (Tag.IsValid())
                    {
                        Container->AddTag(Tag);
                    }
                    else
                    {
                        UnknownTags.Add(TagStr);
                    }
                }
                if (UnknownTags.Num() > 0)
                {
                    // Surface as warning even though tags that resolved were applied.
                    // Caller will treat non-empty OutErrorMessage with bool=true as
                    // a partial-success warning.
                    OutErrorMessage = FString::Printf(
                        TEXT("GameplayTagContainer %s: %d unknown tag(s) skipped: %s"),
                        *PropertyName, UnknownTags.Num(), *FString::Join(UnknownTags, TEXT(", ")));
                }
                return true;
            }

            OutErrorMessage = FString::Printf(TEXT("GameplayTagContainer %s requires an array of tag strings"), *PropertyName);
            return false;
        }

        // JSON object: recurse into struct fields via SetValueAtAddress
        if (Value->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject> StructObj = Value->AsObject();
            if (StructObj.IsValid())
            {
                bool bAnySet = false;
                TArray<FString> StructErrors;
                for (const auto& FieldPair : StructObj->Values)
                {
                    FProperty* SubProperty = StructProp->Struct->FindPropertyByName(*FieldPair.Key);
                    if (!SubProperty)
                    {
                        StructErrors.Add(FString::Printf(TEXT("Sub-property not found: %s.%s"), *PropertyName, *FieldPair.Key));
                        continue;
                    }

                    void* SubPropertyAddr = SubProperty->ContainerPtrToValuePtr<void>(PropertyAddr);
                    const FString SubLabel = FString::Printf(TEXT("%s.%s"), *PropertyName, *FieldPair.Key);

                    FString SubError;
                    if (SetValueAtAddress(SubProperty, SubPropertyAddr, FieldPair.Value, Object, SubLabel, SubError))
                    {
                        bAnySet = true;
                        // Capture nested partial-failure warnings even when the field
                        // itself reports success — the array/struct dispatchers use
                        // OutErrorMessage to surface non-fatal warnings.
                        if (!SubError.IsEmpty())
                        {
                            StructErrors.Add(SubError);
                        }
                    }
                    else
                    {
                        StructErrors.Add(SubError.IsEmpty()
                            ? FString::Printf(TEXT("Unsupported sub-property: %s"), *SubLabel)
                            : SubError);
                    }
                }
                // Always surface struct-level errors. Prior code returned true on
                // any-field-set and discarded sibling errors, masking the silent
                // corruption pattern that wiped 9/10 TSoftObjectPtr fields on
                // DA_Sword_OneHand_Moveset (the JSON omitted later array entries
                // and the failures had no place to be reported).
                if (StructErrors.Num() > 0)
                {
                    OutErrorMessage = FString::Join(StructErrors, TEXT("; "));
                }
                if (bAnySet) return true;
                if (StructErrors.Num() > 0)
                {
                    return false;
                }
            }
        }

        // ImportText fallback for structs
        if (Value->Type == EJson::String)
        {
            const TCHAR* ImportResult = Property->ImportText_Direct(*Value->AsString(), PropertyAddr, Object, PPF_None);
            if (ImportResult)
            {
                return true;
            }
        }

        OutErrorMessage = FString::Printf(TEXT("Struct property %s: could not set value"), *PropertyName);
        return false;
    }

    // Universal fallback: ImportText
    if (Value->Type == EJson::String)
    {
        const TCHAR* ImportResult = Property->ImportText_Direct(*Value->AsString(), PropertyAddr, Object, PPF_None);
        if (ImportResult)
        {
            return true;
        }
    }

    OutErrorMessage = FString::Printf(TEXT("Unsupported property type: %s for property %s"),
                                    *Property->GetClass()->GetName(), *PropertyName);
    return false;
}

UClass* FEpicUnrealMCPCommonUtils::ResolveClass(const FString& Name)
{
    if (Name.IsEmpty())
    {
        return nullptr;
    }

    // Full script path form — load directly.
    if (Name.StartsWith(TEXT("/")))
    {
        return LoadObject<UClass>(nullptr, *Name);
    }

    // FindFirstObject probes every loaded class across all modules.
    if (UClass* Found = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::EnsureIfAmbiguous))
    {
        return Found;
    }

    // Try with a leading 'U' (UObjects) or 'A' (Actors).
    if (!Name.StartsWith(TEXT("U")))
    {
        const FString UName = FString(TEXT("U")) + Name;
        if (UClass* Found = FindFirstObject<UClass>(*UName, EFindFirstObjectOptions::EnsureIfAmbiguous))
        {
            return Found;
        }
    }
    if (!Name.StartsWith(TEXT("A")))
    {
        const FString AName = FString(TEXT("A")) + Name;
        if (UClass* Found = FindFirstObject<UClass>(*AName, EFindFirstObjectOptions::EnsureIfAmbiguous))
        {
            return Found;
        }
    }

    // Fallback — enumerate loaded modules and try /Script/<Module>.<Name>.
    TArray<FModuleStatus> ModuleStatuses;
    FModuleManager::Get().QueryModules(ModuleStatuses);
    for (const FModuleStatus& Status : ModuleStatuses)
    {
        if (!Status.bIsLoaded)
        {
            continue;
        }
        const FString ScriptPath = FString::Printf(TEXT("/Script/%s.%s"), *Status.Name, *Name);
        if (UClass* Found = LoadObject<UClass>(nullptr, *ScriptPath))
        {
            return Found;
        }
    }
    return nullptr;
}

// ----------------------------------------------------------------------------
// Registry-truth asset resolution (see header for the false-negative rationale)
// ----------------------------------------------------------------------------

FString FEpicUnrealMCPCommonUtils::NormalizeObjectPath(const FString& AssetPath)
{
    FString P = AssetPath.TrimStartAndEnd();
    while (P.EndsWith(TEXT("/")))
    {
        P.LeftChopInline(1);
    }
    if (P.IsEmpty())
    {
        return P;
    }

    // Last path segment after the final '/'.
    FString LastSegment = P;
    int32 SlashIdx;
    if (P.FindLastChar(TEXT('/'), SlashIdx))
    {
        LastSegment = P.RightChop(SlashIdx + 1);
    }

    // Already an object path ("Pkg.Object", incl. "_C" class paths) — leave it.
    if (LastSegment.Contains(TEXT(".")))
    {
        return P;
    }

    // Package-only path: append ".AssetName" so the object lookup resolves.
    return FString::Printf(TEXT("%s.%s"), *P, *LastSegment);
}

bool FEpicUnrealMCPCommonUtils::ResolveAssetData(const FString& AssetPath, FAssetData& OutData)
{
    if (AssetPath.IsEmpty())
    {
        return false;
    }

    IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FString ObjPath = NormalizeObjectPath(AssetPath);

    // 1) Direct object-path lookup (canonical key).
    FAssetData Data = AR.GetAssetByObjectPath(FSoftObjectPath(ObjPath), /*bIncludeOnlyOnDiskAssets*/ false);
    if (Data.IsValid())
    {
        OutData = Data;
        return true;
    }

    // 2) Package-name fallback — pick the asset in the package whose name matches,
    //    else the first asset in the package (handles package-only paths and a
    //    package whose primary asset name differs from the file name).
    FString PackageName = ObjPath;
    int32 DotIdx;
    if (ObjPath.FindChar(TEXT('.'), DotIdx))
    {
        PackageName = ObjPath.Left(DotIdx);
    }
    const FString WantName = ObjPath.RightChop(ObjPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1);

    TArray<FAssetData> PackageAssets;
    AR.GetAssetsByPackageName(FName(*PackageName), PackageAssets);
    if (PackageAssets.Num() > 0)
    {
        for (const FAssetData& Candidate : PackageAssets)
        {
            if (Candidate.AssetName.ToString() == WantName)
            {
                OutData = Candidate;
                return true;
            }
        }
        OutData = PackageAssets[0];
        return true;
    }

    // 3) Last resort: the registry may not have scanned this path yet (just
    //    created / renamed this session). Scan the containing directory once and
    //    retry the object lookup.
    FString DirPath = PackageName;
    int32 DirSlashIdx;
    if (PackageName.FindLastChar(TEXT('/'), DirSlashIdx) && DirSlashIdx > 0)
    {
        DirPath = PackageName.Left(DirSlashIdx);
    }
    AR.ScanPathsSynchronous({ DirPath }, /*bForceRescan*/ true);

    Data = AR.GetAssetByObjectPath(FSoftObjectPath(ObjPath), /*bIncludeOnlyOnDiskAssets*/ false);
    if (Data.IsValid())
    {
        OutData = Data;
        return true;
    }
    return false;
}

bool FEpicUnrealMCPCommonUtils::AssetExists(const FString& AssetPath)
{
    FAssetData Data;
    return ResolveAssetData(AssetPath, Data);
}

UObject* FEpicUnrealMCPCommonUtils::ResolveAsset(const FString& AssetPath)
{
    if (AssetPath.IsEmpty())
    {
        return nullptr;
    }

    FAssetData Data;
    if (ResolveAssetData(AssetPath, Data))
    {
        if (UObject* Obj = Data.GetAsset())
        {
            return Obj;
        }
        // AssetData resolved but GetAsset() declined — load from its canonical path.
        if (UObject* Obj = LoadObject<UObject>(nullptr, *Data.GetSoftObjectPath().ToString()))
        {
            return Obj;
        }
    }

    // Fall back to the normalized object path, then the raw string, so this is a
    // strict superset of UEditorAssetLibrary::LoadAsset.
    if (UObject* Obj = UEditorAssetLibrary::LoadAsset(NormalizeObjectPath(AssetPath)))
    {
        return Obj;
    }
    return UEditorAssetLibrary::LoadAsset(AssetPath);
}