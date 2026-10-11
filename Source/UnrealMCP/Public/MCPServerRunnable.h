#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include "Sockets.h"
#include "Interfaces/IPv4/IPv4Address.h"

class UEpicUnrealMCPBridge;
class FMCPClientWorker;

/**
 * Acceptor thread: hands each accepted client off to a dedicated worker thread
 * so multiple Claude Code sessions can talk to the bridge concurrently.
 * ExecuteCommand already serializes onto the game thread, so worker fan-out is safe.
 */
class FMCPServerRunnable : public FRunnable
{
public:
	FMCPServerRunnable(UEpicUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InListenerSocket);
	virtual ~FMCPServerRunnable();

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

private:
	void ReapFinishedWorkers();
	void StopAllWorkers();

	UEpicUnrealMCPBridge* Bridge;
	TSharedPtr<FSocket> ListenerSocket;
	bool bRunning;

	FCriticalSection WorkersLock;
	TArray<TSharedPtr<FMCPClientWorker>> Workers;
};

/**
 * Per-client worker thread. Owns its socket and runs the newline-framed read loop
 * until the client disconnects or the acceptor signals shutdown.
 */
class FMCPClientWorker : public FRunnable
{
public:
	FMCPClientWorker(UEpicUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InClientSocket, int32 InClientId);
	virtual ~FMCPClientWorker();

	virtual uint32 Run() override;
	virtual void Stop() override;

	void StartThread();
	bool IsFinished() const { return bFinished; }
	void WaitAndDestroy();

private:
	UEpicUnrealMCPBridge* Bridge;
	TSharedPtr<FSocket> ClientSocket;
	int32 ClientId;
	FRunnableThread* Thread;
	bool bRunning;
	bool bFinished;
};
