#include "Commands/EpicUnrealMCPWorldCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"

FEpicUnrealMCPWorldCommands::FEpicUnrealMCPWorldCommands()
{
}

FEpicUnrealMCPWorldCommands::~FEpicUnrealMCPWorldCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPWorldCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_world_info"))
	{
		return HandleGetWorldInfo(Params);
	}
	else if (CommandType == TEXT("get_level_details"))
	{
		return HandleGetLevelDetails(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown World command: %s"), *CommandType));
}

// ============================================================================
// get_world_info
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWorldCommands::HandleGetWorldInfo(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world available"));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);

	// Persistent level info
	Result->SetStringField(TEXT("persistent_level_name"), World->PersistentLevel->GetOuter()->GetName());
	Result->SetNumberField(TEXT("persistent_level_actor_count"), World->PersistentLevel->Actors.Num());
	Result->SetStringField(TEXT("map_name"), World->GetMapName());

	// World settings
	{
		TSharedPtr<FJsonObject> WorldSettingsObj = MakeShareable(new FJsonObject);
		WorldSettingsObj->SetNumberField(TEXT("world_gravity"), World->GetGravityZ());

		AWorldSettings* WS = World->GetWorldSettings();
		if (WS)
		{
			WorldSettingsObj->SetNumberField(TEXT("kill_z"), WS->KillZ);
			WorldSettingsObj->SetStringField(TEXT("default_game_mode"),
				WS->DefaultGameMode ? WS->DefaultGameMode->GetName() : TEXT("None"));
		}

		Result->SetObjectField(TEXT("world_settings"), WorldSettingsObj);
	}

	// Streaming levels
	{
		const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();

		TArray<TSharedPtr<FJsonValue>> StreamingArray;
		for (ULevelStreaming* StreamingLevel : StreamingLevels)
		{
			if (!StreamingLevel)
			{
				continue;
			}

			TSharedPtr<FJsonObject> LevelObj = MakeShareable(new FJsonObject);

			const FString PackageName = StreamingLevel->GetWorldAssetPackageName();
			LevelObj->SetStringField(TEXT("package_name"), PackageName);

			// Short name: everything after the last /
			int32 LastSlashIdx = INDEX_NONE;
			PackageName.FindLastChar(TEXT('/'), LastSlashIdx);
			const FString ShortName = (LastSlashIdx != INDEX_NONE)
				? PackageName.RightChop(LastSlashIdx + 1)
				: PackageName;
			LevelObj->SetStringField(TEXT("level_name"), ShortName);

			LevelObj->SetBoolField(TEXT("is_loaded"), StreamingLevel->IsLevelLoaded());
			LevelObj->SetBoolField(TEXT("is_visible"), StreamingLevel->IsLevelVisible());
			LevelObj->SetBoolField(TEXT("should_be_loaded"),
				StreamingLevel->HasLoadRequestPending() || StreamingLevel->IsLevelLoaded());
			LevelObj->SetStringField(TEXT("streaming_class"), StreamingLevel->GetClass()->GetName());

			// Level transform offset
			const FTransform LevelTransform = StreamingLevel->LevelTransform;
			if (!LevelTransform.GetTranslation().IsNearlyZero())
			{
				TSharedPtr<FJsonObject> TransformObj = MakeShareable(new FJsonObject);
				TransformObj->SetNumberField(TEXT("x"), LevelTransform.GetTranslation().X);
				TransformObj->SetNumberField(TEXT("y"), LevelTransform.GetTranslation().Y);
				TransformObj->SetNumberField(TEXT("z"), LevelTransform.GetTranslation().Z);
				LevelObj->SetObjectField(TEXT("level_transform"), TransformObj);
			}

			StreamingArray.Add(MakeShareable(new FJsonValueObject(LevelObj)));
		}

		Result->SetArrayField(TEXT("streaming_levels"), StreamingArray);
		Result->SetNumberField(TEXT("streaming_level_count"), StreamingLevels.Num());
	}

	return Result;
}

// ============================================================================
// get_level_details
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPWorldCommands::HandleGetLevelDetails(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("level_name")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: level_name"));
	}

	const FString LevelName = Params->GetStringField(TEXT("level_name"));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world available"));
	}

	// Resolve the ULevel* from the name
	ULevel* FoundLevel = nullptr;
	FString ResolvedName;

	// Check persistent level first
	const FString PersistentName = World->PersistentLevel->GetOuter()->GetName();
	if (PersistentName == LevelName || PersistentName.Contains(LevelName))
	{
		FoundLevel = World->PersistentLevel;
		ResolvedName = PersistentName;
	}

	// Search streaming levels by package name (full or contains)
	if (!FoundLevel)
	{
		for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if (!StreamingLevel)
			{
				continue;
			}

			const FString PackageName = StreamingLevel->GetWorldAssetPackageName();
			if (PackageName == LevelName || PackageName.Contains(LevelName))
			{
				if (StreamingLevel->IsLevelLoaded() && StreamingLevel->GetLoadedLevel())
				{
					FoundLevel = StreamingLevel->GetLoadedLevel();
					ResolvedName = PackageName;
				}
				else
				{
					return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
						FString::Printf(TEXT("Level found but not loaded: %s"), *PackageName));
				}
				break;
			}
		}
	}

	// Try matching by short name (after last /)
	if (!FoundLevel)
	{
		for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if (!StreamingLevel)
			{
				continue;
			}

			const FString PackageName = StreamingLevel->GetWorldAssetPackageName();
			int32 LastSlashIdx = INDEX_NONE;
			PackageName.FindLastChar(TEXT('/'), LastSlashIdx);
			const FString ShortName = (LastSlashIdx != INDEX_NONE)
				? PackageName.RightChop(LastSlashIdx + 1)
				: PackageName;

			if (ShortName == LevelName || ShortName.Contains(LevelName))
			{
				if (StreamingLevel->IsLevelLoaded() && StreamingLevel->GetLoadedLevel())
				{
					FoundLevel = StreamingLevel->GetLoadedLevel();
					ResolvedName = PackageName;
				}
				else
				{
					return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
						FString::Printf(TEXT("Level found but not loaded: %s"), *PackageName));
				}
				break;
			}
		}
	}

	if (!FoundLevel)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Level not found: %s"), *LevelName));
	}

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("level_name"), ResolvedName);
	Result->SetNumberField(TEXT("actor_count"), FoundLevel->Actors.Num());

	// Actor class breakdown
	{
		TMap<FString, int32> ClassCounts;
		for (AActor* Actor : FoundLevel->Actors)
		{
			if (Actor)
			{
				ClassCounts.FindOrAdd(Actor->GetClass()->GetName())++;
			}
		}

		// Sort by count descending
		ClassCounts.ValueSort([](const int32& A, const int32& B)
		{
			return A > B;
		});

		TArray<TSharedPtr<FJsonValue>> BreakdownArray;
		int32 Index = 0;
		for (const auto& Pair : ClassCounts)
		{
			if (Index >= 20)
			{
				break;
			}

			TSharedPtr<FJsonObject> EntryObj = MakeShareable(new FJsonObject);
			EntryObj->SetStringField(TEXT("class_name"), Pair.Key);
			EntryObj->SetNumberField(TEXT("count"), Pair.Value);
			BreakdownArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
			++Index;
		}

		Result->SetArrayField(TEXT("actor_class_breakdown"), BreakdownArray);
	}

	// Bounds (limited to first 1000 actors for performance)
	{
		FBox TotalBounds(ForceInit);
		int32 ActorsProcessed = 0;

		for (AActor* Actor : FoundLevel->Actors)
		{
			if (!Actor)
			{
				continue;
			}

			if (ActorsProcessed >= 1000)
			{
				break;
			}

			FBox ActorBounds = Actor->GetComponentsBoundingBox();
			if (ActorBounds.IsValid)
			{
				if (TotalBounds.IsValid)
				{
					TotalBounds += ActorBounds;
				}
				else
				{
					TotalBounds = ActorBounds;
				}
			}

			++ActorsProcessed;
		}

		if (TotalBounds.IsValid)
		{
			TSharedPtr<FJsonObject> BoundsObj = MakeShareable(new FJsonObject);

			TSharedPtr<FJsonObject> MinObj = MakeShareable(new FJsonObject);
			MinObj->SetNumberField(TEXT("x"), TotalBounds.Min.X);
			MinObj->SetNumberField(TEXT("y"), TotalBounds.Min.Y);
			MinObj->SetNumberField(TEXT("z"), TotalBounds.Min.Z);
			BoundsObj->SetObjectField(TEXT("min"), MinObj);

			TSharedPtr<FJsonObject> MaxObj = MakeShareable(new FJsonObject);
			MaxObj->SetNumberField(TEXT("x"), TotalBounds.Max.X);
			MaxObj->SetNumberField(TEXT("y"), TotalBounds.Max.Y);
			MaxObj->SetNumberField(TEXT("z"), TotalBounds.Max.Z);
			BoundsObj->SetObjectField(TEXT("max"), MaxObj);

			BoundsObj->SetNumberField(TEXT("actors_sampled"), ActorsProcessed);

			Result->SetObjectField(TEXT("bounds"), BoundsObj);
		}
	}

	return Result;
}
