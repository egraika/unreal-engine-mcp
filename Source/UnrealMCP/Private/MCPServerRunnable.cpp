#include "MCPServerRunnable.h"
#include "EpicUnrealMCPBridge.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "JsonObjectConverter.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPServer, Log, All);

namespace
{
    constexpr int32 RecvBufferSize = 65536;
    constexpr int32 SocketBufferSize = 262144; // 256 KB send/recv kernel buffers
    const FTimespan AcceptWait = FTimespan::FromMilliseconds(500);
    const FTimespan ReadWait = FTimespan::FromMilliseconds(500);

    bool SendAll(FSocket& Socket, const uint8* Data, int32 Size)
    {
        int32 Sent = 0;
        while (Sent < Size)
        {
            int32 ThisSend = 0;
            if (!Socket.Send(Data + Sent, Size - Sent, ThisSend) || ThisSend <= 0)
            {
                return false;
            }
            Sent += ThisSend;
        }
        return true;
    }

    void ProcessOneMessage(UEpicUnrealMCPBridge* Bridge, FSocket& Client, const FString& Message)
    {
        TSharedPtr<FJsonObject> JsonObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);

        FString Response;
        if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
        {
            FString CommandType;
            if (JsonObject->TryGetStringField(TEXT("type"), CommandType))
            {
                TSharedPtr<FJsonObject> Params;
                if (JsonObject->Values.Find(TEXT("params")) && JsonObject->HasTypedField<EJson::Object>(TEXT("params")))
                {
                    Params = JsonObject->GetObjectField(TEXT("params"));
                }
                else
                {
                    Params = MakeShared<FJsonObject>();
                }

                Response = Bridge->ExecuteCommand(CommandType, Params);
            }
            else
            {
                Response = TEXT("{\"status\":\"error\",\"error\":\"Missing 'type' field\"}");
            }
        }
        else
        {
            Response = TEXT("{\"status\":\"error\",\"error\":\"Invalid JSON\"}");
        }

        Response.AppendChar(TEXT('\n'));

        FTCHARToUTF8 Utf8(*Response);
        if (!SendAll(Client, reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()))
        {
            UE_LOG(LogMCPServer, Error, TEXT("Failed to send response (%d bytes)"), Utf8.Length());
        }
    }
}

// ===== Acceptor =====

FMCPServerRunnable::FMCPServerRunnable(UEpicUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InListenerSocket)
    : Bridge(InBridge)
    , ListenerSocket(InListenerSocket)
    , bRunning(true)
{
}

FMCPServerRunnable::~FMCPServerRunnable()
{
}

bool FMCPServerRunnable::Init()
{
    return true;
}

uint32 FMCPServerRunnable::Run()
{
    UE_LOG(LogMCPServer, Log, TEXT("MCP server thread started (multi-client)"));

    static int32 NextClientId = 0;

    while (bRunning)
    {
        if (!ListenerSocket.IsValid())
        {
            break;
        }

        ReapFinishedWorkers();

        if (!ListenerSocket->Wait(ESocketWaitConditions::WaitForRead, AcceptWait))
        {
            continue;
        }

        bool bPending = false;
        if (!ListenerSocket->HasPendingConnection(bPending) || !bPending)
        {
            continue;
        }

        TSharedPtr<FSocket> Accepted = MakeShareable(ListenerSocket->Accept(TEXT("MCPClient")));
        if (!Accepted.IsValid())
        {
            UE_LOG(LogMCPServer, Warning, TEXT("Accept failed"));
            continue;
        }

        Accepted->SetNoDelay(true);
        int32 Tmp = 0;
        Accepted->SetSendBufferSize(SocketBufferSize, Tmp);
        Accepted->SetReceiveBufferSize(SocketBufferSize, Tmp);

        const int32 ClientId = ++NextClientId;
        UE_LOG(LogMCPServer, Log, TEXT("Client %d connected"), ClientId);

        TSharedPtr<FMCPClientWorker> Worker = MakeShared<FMCPClientWorker>(Bridge, Accepted, ClientId);
        Worker->StartThread();

        {
            FScopeLock Lock(&WorkersLock);
            Workers.Add(Worker);
        }
    }

    StopAllWorkers();

    UE_LOG(LogMCPServer, Log, TEXT("MCP server thread stopping"));
    return 0;
}

void FMCPServerRunnable::Stop()
{
    bRunning = false;
}

void FMCPServerRunnable::Exit()
{
}

void FMCPServerRunnable::ReapFinishedWorkers()
{
    FScopeLock Lock(&WorkersLock);
    for (int32 i = Workers.Num() - 1; i >= 0; --i)
    {
        if (Workers[i].IsValid() && Workers[i]->IsFinished())
        {
            Workers[i]->WaitAndDestroy();
            Workers.RemoveAt(i);
        }
    }
}

void FMCPServerRunnable::StopAllWorkers()
{
    TArray<TSharedPtr<FMCPClientWorker>> Local;
    {
        FScopeLock Lock(&WorkersLock);
        Local = Workers;
        Workers.Reset();
    }

    for (const TSharedPtr<FMCPClientWorker>& W : Local)
    {
        if (W.IsValid())
        {
            W->Stop();
        }
    }
    for (const TSharedPtr<FMCPClientWorker>& W : Local)
    {
        if (W.IsValid())
        {
            W->WaitAndDestroy();
        }
    }
}

// ===== Per-client worker =====

FMCPClientWorker::FMCPClientWorker(UEpicUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InClientSocket, int32 InClientId)
    : Bridge(InBridge)
    , ClientSocket(InClientSocket)
    , ClientId(InClientId)
    , Thread(nullptr)
    , bRunning(true)
    , bFinished(false)
{
}

FMCPClientWorker::~FMCPClientWorker()
{
    // Graceful shutdown — Kill(true) is unsafe on UE threads. Stop signals the
    // run loop, then we wait for the thread to actually exit.
    if (Thread)
    {
        bRunning = false;
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }

    if (ClientSocket.IsValid())
    {
        ClientSocket->Close();
        ClientSocket.Reset();
    }
}

void FMCPClientWorker::StartThread()
{
    const FString Name = FString::Printf(TEXT("UnrealMCPClient_%d"), ClientId);
    Thread = FRunnableThread::Create(this, *Name, 0, TPri_Normal);
}

uint32 FMCPClientWorker::Run()
{
    TArray<uint8> RecvBuffer;
    RecvBuffer.SetNumUninitialized(RecvBufferSize);
    FString MessageBuffer;

    while (bRunning && ClientSocket.IsValid())
    {
        if (!ClientSocket->Wait(ESocketWaitConditions::WaitForRead, ReadWait))
        {
            if (ClientSocket->GetConnectionState() == SCS_NotConnected)
            {
                break;
            }
            continue;
        }

        int32 BytesRead = 0;
        if (!ClientSocket->Recv(RecvBuffer.GetData(), RecvBuffer.Num() - 1, BytesRead) || BytesRead == 0)
        {
            const int32 LastError = (int32)ISocketSubsystem::Get()->GetLastErrorCode();
            if (LastError == SE_EWOULDBLOCK || LastError == SE_EINTR)
            {
                continue;
            }
            break;
        }

        RecvBuffer[BytesRead] = 0;
        MessageBuffer.Append(UTF8_TO_TCHAR(RecvBuffer.GetData()));

        int32 NewlineIdx = INDEX_NONE;
        while (MessageBuffer.FindChar(TEXT('\n'), NewlineIdx))
        {
            FString Message = MessageBuffer.Left(NewlineIdx);
            MessageBuffer.RightChopInline(NewlineIdx + 1, EAllowShrinking::No);

            if (!Message.IsEmpty())
            {
                ProcessOneMessage(Bridge, *ClientSocket, Message);
            }
        }
    }

    if (ClientSocket.IsValid())
    {
        ClientSocket->Close();
        ClientSocket.Reset();
    }

    UE_LOG(LogMCPServer, Log, TEXT("Client %d disconnected"), ClientId);

    bFinished = true;
    return 0;
}

void FMCPClientWorker::Stop()
{
    bRunning = false;
}

void FMCPClientWorker::WaitAndDestroy()
{
    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
}
