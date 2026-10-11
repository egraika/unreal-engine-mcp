#include "Commands/EpicUnrealMCPEnumTagCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#include "GameplayTagsManager.h"
#include "GameplayTagsModule.h"
#include "GameplayTagsEditorModule.h"

FEpicUnrealMCPEnumTagCommands::FEpicUnrealMCPEnumTagCommands() {}
FEpicUnrealMCPEnumTagCommands::~FEpicUnrealMCPEnumTagCommands() {}

TSharedPtr<FJsonObject> FEpicUnrealMCPEnumTagCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("create_enum"))         return HandleCreateEnum(Params);
	if (CommandType == TEXT("set_enum_entries"))    return HandleSetEnumEntries(Params);
	if (CommandType == TEXT("read_enum"))           return HandleReadEnum(Params);
	if (CommandType == TEXT("create_gameplay_tag")) return HandleCreateGameplayTag(Params);
	if (CommandType == TEXT("list_gameplay_tags"))  return HandleListGameplayTags(Params);

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown EnumTag command: %s"), *CommandType));
}

// ============================================================================
// Enum helpers
// ============================================================================

namespace
{
	// Apply a list of {name, display_name?, description?} entries to the enum,
	// replacing whatever is currently there. UE 5.7's UUserDefinedEnum API only
	// exposes display-name and metadata mutation — the *internal* enumerator names
	// stay auto-generated ("MyEnum::NewEnumerator0") and aren't user-facing in any
	// dropdown the editor renders. We therefore prefer `display_name`, fall back
	// to `name`, and route tooltips through UEnum::SetMetaData("ToolTip").
	bool ApplyEntries(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>& Entries, TArray<FString>& OutWarnings)
	{
		if (!Enum) return false;

		// Strip existing entries down to zero. RemoveEnumeratorFromUserDefinedEnum
		// re-notifies dependent property pins on every call — iterating from the
		// back keeps the remaining indices stable.
		while (Enum->NumEnums() > 1) // last entry is the auto-_MAX sentinel
		{
			FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, 0);
		}

		int32 EntryIndex = 0;
		for (const TSharedPtr<FJsonValue>& V : Entries)
		{
			const TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
			FString PrimaryName;
			FString DisplayName;
			FString Description;

			if (Obj.IsValid())
			{
				Obj->TryGetStringField(TEXT("name"), PrimaryName);
				Obj->TryGetStringField(TEXT("display_name"), DisplayName);
				Obj->TryGetStringField(TEXT("description"), Description);
			}
			else if (V.IsValid())
			{
				PrimaryName = V->AsString();
			}

			if (DisplayName.IsEmpty()) DisplayName = PrimaryName;
			if (DisplayName.IsEmpty())
			{
				OutWarnings.Add(FString::Printf(TEXT("Entry %d: missing name/display_name, skipped"), EntryIndex));
				++EntryIndex;
				continue;
			}

			FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
			const int32 NewIdx = Enum->NumEnums() - 2; // _MAX sentinel sits at NumEnums()-1
			if (NewIdx < 0)
			{
				OutWarnings.Add(FString::Printf(TEXT("Entry %d: AddNewEnumerator failed"), EntryIndex));
				++EntryIndex;
				continue;
			}

			const FText DisplayText = FText::FromString(DisplayName);
			if (!FEnumEditorUtils::IsEnumeratorDisplayNameValid(Enum, NewIdx, DisplayText))
			{
				OutWarnings.Add(FString::Printf(TEXT("Entry '%s': display name invalid/duplicate"), *DisplayName));
			}
			else
			{
				FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIdx, DisplayText);
			}

			if (!Description.IsEmpty())
			{
				Enum->SetMetaData(TEXT("ToolTip"), *Description, NewIdx);
			}

			++EntryIndex;
		}

		Enum->MarkPackageDirty();
		return true;
	}
}

// ============================================================================
// create_enum
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPEnumTagCommands::HandleCreateEnum(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name'"));
	}
	FString PackagePath = Params->HasField(TEXT("package_path"))
		? Params->GetStringField(TEXT("package_path"))
		: FString(TEXT("/Game/Data/"));
	if (!PackagePath.EndsWith(TEXT("/"))) PackagePath += TEXT("/");

	const FString FullPath = PackagePath + Name;
	if (FEpicUnrealMCPCommonUtils::AssetExists(FullPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset already exists: %s"), *FullPath));
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	UEnum* CreatedEnum = FEnumEditorUtils::CreateUserDefinedEnum(Package, *Name, RF_Public | RF_Standalone);
	UUserDefinedEnum* NewEnum = Cast<UUserDefinedEnum>(CreatedEnum);
	if (!NewEnum)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("CreateUserDefinedEnum returned null"));
	}

	FAssetRegistryModule::AssetCreated(NewEnum);
	Package->MarkPackageDirty();

	TArray<FString> Warnings;
	const TArray<TSharedPtr<FJsonValue>>* EntriesJson = nullptr;
	if (Params->TryGetArrayField(TEXT("entries"), EntriesJson) && EntriesJson)
	{
		ApplyEntries(NewEnum, *EntriesJson, Warnings);
	}

	const bool bSave = Params->HasField(TEXT("save")) && Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave) bSaved = UEditorAssetLibrary::SaveLoadedAsset(NewEnum, /*bOnlyIfIsDirty*/ false);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), FullPath);
	Result->SetStringField(TEXT("class_path"), NewEnum->GetClass()->GetPathName());
	Result->SetNumberField(TEXT("entry_count"), NewEnum->NumEnums() - 1); // excl. _MAX sentinel
	if (bSave) Result->SetBoolField(TEXT("saved"), bSaved);
	if (Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnJson;
		for (const FString& W : Warnings) WarnJson.Add(MakeShareable(new FJsonValueString(W)));
		Result->SetArrayField(TEXT("warnings"), WarnJson);
	}
	return Result;
}

// ============================================================================
// set_enum_entries — replace the enumerator list on an existing UUserDefinedEnum
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPEnumTagCommands::HandleSetEnumEntries(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
	}
	UObject* Loaded = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(Loaded);
	if (!Enum)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UUserDefinedEnum: %s"), *AssetPath));
	}

	const TArray<TSharedPtr<FJsonValue>>* EntriesJson = nullptr;
	if (!Params->TryGetArrayField(TEXT("entries"), EntriesJson) || !EntriesJson)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'entries' array"));
	}

	TArray<FString> Warnings;
	ApplyEntries(Enum, *EntriesJson, Warnings);

	const bool bSave = !Params->HasField(TEXT("save")) || Params->GetBoolField(TEXT("save"));
	bool bSaved = false;
	if (bSave) bSaved = UEditorAssetLibrary::SaveLoadedAsset(Enum, /*bOnlyIfIsDirty*/ false);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("entry_count"), Enum->NumEnums() - 1);
	if (bSave) Result->SetBoolField(TEXT("saved"), bSaved);
	if (Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnJson;
		for (const FString& W : Warnings) WarnJson.Add(MakeShareable(new FJsonValueString(W)));
		Result->SetArrayField(TEXT("warnings"), WarnJson);
	}
	return Result;
}

// ============================================================================
// read_enum
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPEnumTagCommands::HandleReadEnum(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
	}
	UObject* Loaded = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UEnum* Enum = Cast<UEnum>(Loaded);
	if (!Enum)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UEnum: %s"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> Entries;
	const int32 LastIndex = Enum->NumEnums() - 1; // _MAX sentinel
	for (int32 Idx = 0; Idx < LastIndex; ++Idx)
	{
		TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
		E->SetNumberField(TEXT("index"), Idx);
		E->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(Idx));
		E->SetStringField(TEXT("display_name"), Enum->GetDisplayNameTextByIndex(Idx).ToString());
		E->SetNumberField(TEXT("value"), Enum->GetValueByIndex(Idx));
		Entries.Add(MakeShareable(new FJsonValueObject(E)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("class_path"), Enum->GetClass()->GetPathName());
	Result->SetNumberField(TEXT("entry_count"), Entries.Num());
	Result->SetArrayField(TEXT("entries"), Entries);
	return Result;
}

// ============================================================================
// create_gameplay_tag
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPEnumTagCommands::HandleCreateGameplayTag(const TSharedPtr<FJsonObject>& Params)
{
	FString TagName;
	if (!Params->TryGetStringField(TEXT("tag"), TagName) || TagName.IsEmpty())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'tag' string"));
	}
	const FString Comment = Params->HasField(TEXT("comment"))
		? Params->GetStringField(TEXT("comment"))
		: FString();

	// Default ini source matches the editor's "Add to Default tags" UI.
	FName Source = NAME_None;
	FString SourceStr;
	if (Params->TryGetStringField(TEXT("source"), SourceStr) && !SourceStr.IsEmpty())
	{
		Source = *SourceStr;
	}
	else
	{
		Source = FName(TEXT("DefaultGameplayTags.ini"));
	}

	// AddNewGameplayTagToINI does the heavy lifting: writes the ini, registers the tag
	// with UGameplayTagsManager, and notifies listeners. Returns false if the tag already
	// exists or if the source is missing — we surface that as success=false with reason.
	IGameplayTagsEditorModule& Module = IGameplayTagsEditorModule::Get();
	const bool bAdded = Module.AddNewGameplayTagToINI(TagName, Comment, Source);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bAdded);
	Result->SetStringField(TEXT("tag"), TagName);
	Result->SetStringField(TEXT("source"), Source.ToString());
	if (!Comment.IsEmpty()) Result->SetStringField(TEXT("comment"), Comment);
	if (!bAdded)
	{
		Result->SetStringField(TEXT("error"),
			TEXT("AddNewGameplayTagToINI returned false — likely the tag already exists, the source is missing, or the editor blocked the write."));
	}
	return Result;
}

// ============================================================================
// list_gameplay_tags
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPEnumTagCommands::HandleListGameplayTags(const TSharedPtr<FJsonObject>& Params)
{
	const FString Filter = Params->HasField(TEXT("filter"))
		? Params->GetStringField(TEXT("filter"))
		: FString();
	const int32 Limit = Params->HasField(TEXT("limit"))
		? FMath::Max(1, static_cast<int32>(Params->GetNumberField(TEXT("limit"))))
		: 500;

	UGameplayTagsManager& Mgr = UGameplayTagsManager::Get();
	FGameplayTagContainer All;
	Mgr.RequestAllGameplayTags(All, /*bIncludeNonExplicit*/ true);

	TArray<TSharedPtr<FJsonValue>> Tags;
	int32 Total = 0;
	for (const FGameplayTag& Tag : All)
	{
		const FString TagStr = Tag.ToString();
		if (!Filter.IsEmpty() && !TagStr.Contains(Filter, ESearchCase::IgnoreCase)) continue;
		++Total;
		if (Tags.Num() >= Limit) continue;
		Tags.Add(MakeShareable(new FJsonValueString(TagStr)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("total"), Total);
	Result->SetNumberField(TEXT("returned"), Tags.Num());
	Result->SetArrayField(TEXT("tags"), Tags);
	return Result;
}
