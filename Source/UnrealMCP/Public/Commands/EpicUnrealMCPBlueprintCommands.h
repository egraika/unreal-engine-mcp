// EpicUnrealMCPBlueprintCommands.h — Blueprint creation, materials, component management
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNREALMCP_API FEpicUnrealMCPBlueprintCommands
{
public:
	FEpicUnrealMCPBlueprintCommands() = default;
	~FEpicUnrealMCPBlueprintCommands() = default;

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleCreateBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddComponentToBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetPhysicsProperties(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetStaticMeshProperties(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMeshMaterialColor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetAvailableMaterials(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleApplyMaterialToActor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleApplyMaterialToBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetActorMaterialInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetBlueprintMaterialInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadBlueprintContent(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAnalyzeBlueprintGraph(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetBlueprintVariableDetails(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetBlueprintFunctionDetails(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialInstanceParameters(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMaterialInstanceParameters(const TSharedPtr<FJsonObject>& Params);
};
