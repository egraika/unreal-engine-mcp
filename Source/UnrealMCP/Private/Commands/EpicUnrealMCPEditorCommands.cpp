// EpicUnrealMCPEditorCommands.cpp — Actor management stub implementations
// Full implementations were deleted by P4 sync. These stubs return error responses.
// Restore from git history when available.

#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "EditorAssetLibrary.h"

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_actors_in_level"))           return HandleGetActorsInLevel(Params);
	else if (CommandType == TEXT("find_actors_by_name"))      return HandleFindActorsByName(Params);
	else if (CommandType == TEXT("spawn_actor"))              return HandleSpawnActor(Params);
	else if (CommandType == TEXT("delete_actor"))             return HandleDeleteActor(Params);
	else if (CommandType == TEXT("set_actor_transform"))      return HandleSetActorTransform(Params);
	else if (CommandType == TEXT("get_rvt_volumes"))          return HandleGetRVTVolumes(Params);
	else if (CommandType == TEXT("get_landscape_info"))       return HandleGetLandscapeInfo(Params);
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown Editor command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world"));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> ActorArray;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject);
		ActorObj->SetStringField(TEXT("name"), Actor->GetName());
		ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());

		FVector Loc = Actor->GetActorLocation();
		TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject);
		LocObj->SetNumberField(TEXT("x"), Loc.X);
		LocObj->SetNumberField(TEXT("y"), Loc.Y);
		LocObj->SetNumberField(TEXT("z"), Loc.Z);
		ActorObj->SetObjectField(TEXT("location"), LocObj);

		ActorArray.Add(MakeShareable(new FJsonValueObject(ActorObj)));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("actors"), ActorArray);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("pattern")))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: pattern"));

	const FString Pattern = Params->GetStringField(TEXT("pattern"));
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world"));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> ActorArray;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		if (Actor->GetName().Contains(Pattern) || Actor->GetActorLabel().Contains(Pattern))
		{
			TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject);
			ActorObj->SetStringField(TEXT("name"), Actor->GetName());
			ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
			ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
			ActorArray.Add(MakeShareable(new FJsonValueObject(ActorObj)));
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("actors"), ActorArray);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("spawn_actor: Implementation pending restoration from git history"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("delete_actor: Implementation pending restoration from git history"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params)
{
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("set_actor_transform: Implementation pending restoration from git history"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetRVTVolumes(const TSharedPtr<FJsonObject>& Params)
{
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("get_rvt_volumes: Implementation pending restoration from git history"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetLandscapeInfo(const TSharedPtr<FJsonObject>& Params)
{
	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("get_landscape_info: Implementation pending restoration from git history"));
}
