#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Static Mesh MCP commands.
 * Mirrors the skeletal-socket surface in EpicUnrealMCPAnimationCommands
 * but operates on UStaticMesh::Sockets (UStaticMeshSocket).
 */
class UNREALMCP_API FEpicUnrealMCPStaticMeshCommands
{
public:
	FEpicUnrealMCPStaticMeshCommands();
	~FEpicUnrealMCPStaticMeshCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleGetStaticMeshInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMeshFootprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMeshCollisionInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGenerateConvexCollision(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMeshVertexColors(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteMeshGeometryInBox(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddSocketToStaticMesh(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleModifyStaticMeshSocket(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveStaticMeshSocket(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCaptureStaticMeshSocketPreview(const TSharedPtr<FJsonObject>& Params);
};
