#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for UEnum and GameplayTag authoring commands.
 *
 * Commands:
 *   create_enum            — create a UUserDefinedEnum asset with initial entries.
 *   set_enum_entries       — replace the enumerator list on an existing UUserDefinedEnum.
 *   read_enum              — list current enumerator names + display names.
 *   create_gameplay_tag    — append a tag to a tag ini source (default DefaultGameplayTags.ini)
 *                            via the editor's AddNewGameplayTagToINI path.
 *   list_gameplay_tags     — query the GameplayTagsManager for tags matching a filter.
 */
class UNREALMCP_API FEpicUnrealMCPEnumTagCommands
{
public:
	FEpicUnrealMCPEnumTagCommands();
	~FEpicUnrealMCPEnumTagCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleCreateEnum(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetEnumEntries(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadEnum(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateGameplayTag(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListGameplayTags(const TSharedPtr<FJsonObject>& Params);
};
