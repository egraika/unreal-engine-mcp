#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include "EditorSubsystem.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Http.h"
#include "Json.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPBlueprintGraphCommands.h"
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
#include "Commands/EpicUnrealMCPAnimBlueprintCommands.h"
#include "Commands/EpicUnrealMCPStateTreeCommands.h"
#include "Commands/EpicUnrealMCPStaticMeshCommands.h"
#include "Commands/EpicUnrealMCPPythonCommands.h"
#include "Commands/EpicUnrealMCPAssetCommands.h"
#include "Commands/EpicUnrealMCPTextureImportCommands.h"
#include "Commands/EpicUnrealMCPEnumTagCommands.h"
#include "Commands/EpicUnrealMCPPCGCommands.h"
#include "EpicUnrealMCPBridge.generated.h"

class FMCPServerRunnable;

/**
 * Editor subsystem for MCP Bridge
 * Handles communication between external tools and the Unreal Editor
 * through a TCP socket connection. Commands are received as JSON and
 * routed to appropriate command handlers.
 */
UCLASS()
class UNREALMCP_API UEpicUnrealMCPBridge : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UEpicUnrealMCPBridge();
	virtual ~UEpicUnrealMCPBridge();

	// UEditorSubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Server functions
	void StartServer();
	void StopServer();
	bool IsRunning() const { return bIsRunning; }

	// Command execution
	FString ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Build command dispatch table once at subsystem init (O(1) command routing).
	void BuildDispatchTable();

	using FCommandHandler = TFunction<TSharedPtr<FJsonObject>(const FString&, const TSharedPtr<FJsonObject>&)>;
	TMap<FName, FCommandHandler> Dispatch;

	// Server state. Atomic: client worker threads poll it while waiting on the
	// game thread, so a shutdown can release them (see ExecuteCommand).
	std::atomic<bool> bIsRunning{false};
	TSharedPtr<FSocket> ListenerSocket;
	TSharedPtr<FSocket> ConnectionSocket;
	FRunnableThread* ServerThread;

	// Server configuration
	FIPv4Address ServerAddress;
	uint16 Port;

	// Command handler instances
	TSharedPtr<FEpicUnrealMCPEditorCommands> EditorCommands;
	TSharedPtr<FEpicUnrealMCPBlueprintCommands> BlueprintCommands;
	TSharedPtr<FEpicUnrealMCPBlueprintGraphCommands> BlueprintGraphCommands;
	TSharedPtr<FEpicUnrealMCPThumbnailCommands> ThumbnailCommands;
	TSharedPtr<FEpicUnrealMCPMaterialCommands> MaterialCommands;
	TSharedPtr<FEpicUnrealMCPRVTCommands> RVTCommands;
	TSharedPtr<FEpicUnrealMCPAnimationCommands> AnimationCommands;
	TSharedPtr<FEpicUnrealMCPDataAssetCommands> DataAssetCommands;
	TSharedPtr<FEpicUnrealMCPDataTableCommands> DataTableCommands;
	TSharedPtr<FEpicUnrealMCPCurveTableCommands> CurveTableCommands;
	TSharedPtr<FEpicUnrealMCPCurveAssetCommands> CurveAssetCommands;
	TSharedPtr<FEpicUnrealMCPMPCCommands> MPCCommands;
	TSharedPtr<FEpicUnrealMCPSoundCommands> SoundCommands;
	TSharedPtr<FEpicUnrealMCPWidgetCommands> WidgetCommands;
	TSharedPtr<FEpicUnrealMCPWidgetPreviewCommands> WidgetPreviewCommands;
	TSharedPtr<FEpicUnrealMCPWorldCommands> WorldCommands;
	TSharedPtr<FEpicUnrealMCPAnimBlueprintCommands> AnimBlueprintCommands;
	TSharedPtr<FEpicUnrealMCPStateTreeCommands> StateTreeCommands;
	TSharedPtr<FEpicUnrealMCPStaticMeshCommands> StaticMeshCommands;
	TSharedPtr<FEpicUnrealMCPPythonCommands> PythonCommands;
	TSharedPtr<FEpicUnrealMCPAssetCommands> AssetCommands;
	TSharedPtr<FEpicUnrealMCPTextureImportCommands> TextureImportCommands;
	TSharedPtr<FEpicUnrealMCPEnumTagCommands> EnumTagCommands;
	TSharedPtr<FEpicUnrealMCPPCGCommands> PCGCommands;
};