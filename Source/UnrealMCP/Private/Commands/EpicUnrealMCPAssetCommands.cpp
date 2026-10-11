#include "Commands/EpicUnrealMCPAssetCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "ObjectTools.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TopLevelAssetPath.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "NavigationSystem.h"
#include "SourceControlHelpers.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "Editor.h"

FEpicUnrealMCPAssetCommands::FEpicUnrealMCPAssetCommands()
{
}

FEpicUnrealMCPAssetCommands::~FEpicUnrealMCPAssetCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("fixup_redirectors"))
	{
		return HandleFixupRedirectors(Params);
	}
	if (CommandType == TEXT("delete_blueprint"))
	{
		return HandleDeleteBlueprint(Params);
	}
	if (CommandType == TEXT("find_asset_referencers"))
	{
		return HandleFindAssetReferencers(Params);
	}
	if (CommandType == TEXT("replace_asset_references"))
	{
		return HandleReplaceAssetReferences(Params);
	}
	if (CommandType == TEXT("search_assets_fts"))
	{
		return HandleSearchAssetsFTS(Params);
	}
	if (CommandType == TEXT("wait_for_asset_compile"))
	{
		return HandleWaitForAssetCompile(Params);
	}
	if (CommandType == TEXT("wait_for_shaders"))
	{
		return HandleWaitForShaders(Params);
	}
	if (CommandType == TEXT("get_navmesh_build_status"))
	{
		return HandleGetNavmeshBuildStatus(Params);
	}
	if (CommandType == TEXT("save_asset_checked"))
	{
		return HandleSaveAssetChecked(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown Asset command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetCommands::HandleFixupRedirectors(const TSharedPtr<FJsonObject>& Params)
{
	const FString PackagePath = Params->HasField(TEXT("path"))
		? Params->GetStringField(TEXT("path"))
		: FString(TEXT("/Game"));
	const bool bRecursive = Params->HasField(TEXT("recursive"))
		? Params->GetBoolField(TEXT("recursive"))
		: true;
	const bool bDeleteRedirectors = Params->HasField(TEXT("delete_redirectors"))
		? Params->GetBoolField(TEXT("delete_redirectors"))
		: true;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Asset registry must be done scanning before we trust the results.
	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.SearchAllAssets(/*bSynchronousSearch*/ true);
	}

	FARFilter Filter;
	Filter.bRecursivePaths = bRecursive;
	Filter.bRecursiveClasses = false;
	Filter.PackagePaths.Add(*PackagePath);
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/CoreUObject"), TEXT("ObjectRedirector")));

	TArray<FAssetData> RedirectorAssets;
	AssetRegistry.GetAssets(Filter, RedirectorAssets);

	TArray<UObjectRedirector*> Redirectors;
	Redirectors.Reserve(RedirectorAssets.Num());
	for (const FAssetData& AssetData : RedirectorAssets)
	{
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(AssetData.GetAsset()))
		{
			Redirectors.Add(Redirector);
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("path"), PackagePath);
	Result->SetBoolField(TEXT("recursive"), bRecursive);
	Result->SetNumberField(TEXT("redirectors_found"), Redirectors.Num());

	if (Redirectors.Num() == 0)
	{
		Result->SetBoolField(TEXT("ran_fixup"), false);
		return Result;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	const ERedirectFixupMode Mode = bDeleteRedirectors
		? ERedirectFixupMode::DeleteFixedUpRedirectors
		: ERedirectFixupMode::LeaveFixedUpRedirectors;

	// bCheckoutDialogPrompt = false → silent checkout, no modal in unattended runs.
	AssetTools.FixupReferencers(Redirectors, /*bCheckoutDialogPrompt*/ false, Mode);

	Result->SetBoolField(TEXT("ran_fixup"), true);
	Result->SetBoolField(TEXT("delete_redirectors"), bDeleteRedirectors);
	return Result;
}

// ============================================================================
// delete_blueprint
// ============================================================================
//
// Deletes a generic UBlueprint asset. delete_widget_blueprint is the typed
// path for UWidgetBlueprint and is preferred for those — keeping the routes
// separate makes log/audit trails clearer. Setting allow_widget=true unblocks
// callers that want a single delete entry point regardless of BP type.

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetCommands::HandleDeleteBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	if (!FEpicUnrealMCPCommonUtils::AssetExists(AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset does not exist: %s"), *AssetPath));
	}

	UObject* Loaded = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Loaded);
	if (!BP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a Blueprint: %s"), *AssetPath));
	}

	const bool bAllowWidget = Params->HasField(TEXT("allow_widget"))
		? Params->GetBoolField(TEXT("allow_widget"))
		: false;
	if (!bAllowWidget && BP->IsA<UWidgetBlueprint>())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is a UWidgetBlueprint — use delete_widget_blueprint, or pass allow_widget=true: %s"), *AssetPath));
	}

	const bool bDeleted = UEditorAssetLibrary::DeleteAsset(AssetPath);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bDeleted);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetBoolField(TEXT("deleted"), bDeleted);
	Result->SetStringField(TEXT("blueprint_class"), BP->GetClass()->GetName());
	if (!bDeleted)
	{
		Result->SetStringField(TEXT("error"),
			TEXT("DeleteAsset returned false; check output log for references or read-only state. ")
			TEXT("Use find_asset_referencers to identify holders, or replace_asset_references to redirect them first."));
	}
	return Result;
}

// ============================================================================
// find_asset_referencers
// ============================================================================
//
// Read-only. Wraps IAssetRegistry::GetReferencers — same data the Reference
// Viewer's "Show Referencers" panel reads. Default depth is direct only;
// transitive=true walks the whole graph (BFS, deduped). Use before delete or
// replace operations to scope the blast radius.

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetCommands::HandleFindAssetReferencers(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}

	const bool bTransitive = Params->HasField(TEXT("transitive"))
		? Params->GetBoolField(TEXT("transitive"))
		: false;
	const int32 MaxResults = Params->HasField(TEXT("max_results"))
		? FMath::Max(1, (int32)Params->GetIntegerField(TEXT("max_results")))
		: 1024;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.SearchAllAssets(/*bSynchronousSearch*/ true);
	}

	// Convert "/Game/Foo/Bar.Bar" or "/Game/Foo/Bar" to a package FName.
	FString PackageName = AssetPath;
	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindChar('.', DotIndex))
	{
		PackageName.LeftInline(DotIndex, EAllowShrinking::No);
	}

	TArray<FName> Visited;
	TArray<FName> Frontier;
	Frontier.Add(FName(*PackageName));
	Visited.Add(FName(*PackageName));

	TArray<FString> Out;
	Out.Reserve(64);

	while (Frontier.Num() > 0 && Out.Num() < MaxResults)
	{
		const FName Current = Frontier.Pop(EAllowShrinking::No);

		TArray<FName> Refs;
		AssetRegistry.GetReferencers(Current, Refs);

		for (const FName& Ref : Refs)
		{
			if (Visited.Contains(Ref))
			{
				continue;
			}
			Visited.Add(Ref);
			Out.Add(Ref.ToString());
			if (Out.Num() >= MaxResults)
			{
				break;
			}
			if (bTransitive)
			{
				Frontier.Add(Ref);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> JsonRefs;
	JsonRefs.Reserve(Out.Num());
	for (const FString& Path : Out)
	{
		JsonRefs.Add(MakeShared<FJsonValueString>(Path));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetBoolField(TEXT("transitive"), bTransitive);
	Result->SetNumberField(TEXT("count"), Out.Num());
	Result->SetBoolField(TEXT("truncated"), Out.Num() >= MaxResults);
	Result->SetArrayField(TEXT("referencers"), JsonRefs);
	return Result;
}

// ============================================================================
// replace_asset_references
// ============================================================================
//
// Wraps ObjectTools::ConsolidateObjects — same operation as Content Browser
// right-click "Replace References". Every loaded reference to OldAsset is
// rewritten to NewAsset; OldAsset is then deleted (default) and a redirector
// is left in its place for any unloaded consumers. Pair with fixup_redirectors
// to scrub the redirectors after consumer packages are loaded/saved.
//
// Type checking: ObjectTools::ConsolidateObjects performs NO type checks by
// design. We do a minimal sanity check (both must be UObject and at least
// share a common base class branch) but reject only obvious mismatches —
// callers asking to swap a Blueprint for a different-class Blueprint know
// what they're doing (e.g. AHUD subclass swap).

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetCommands::HandleReplaceAssetReferences(const TSharedPtr<FJsonObject>& Params)
{
	FString OldPath;
	if (!Params->TryGetStringField(TEXT("old_asset_path"), OldPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: old_asset_path"));
	}
	FString NewPath;
	if (!Params->TryGetStringField(TEXT("new_asset_path"), NewPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: new_asset_path"));
	}
	if (OldPath == NewPath)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("old_asset_path and new_asset_path are identical"));
	}
	if (!FEpicUnrealMCPCommonUtils::AssetExists(OldPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("old_asset does not exist: %s"), *OldPath));
	}
	if (!FEpicUnrealMCPCommonUtils::AssetExists(NewPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("new_asset does not exist: %s"), *NewPath));
	}

	UObject* OldObj = FEpicUnrealMCPCommonUtils::ResolveAsset(OldPath);
	UObject* NewObj = FEpicUnrealMCPCommonUtils::ResolveAsset(NewPath);
	if (!OldObj || !NewObj)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to load one of the assets"));
	}

	const bool bAllowDifferentClass = Params->HasField(TEXT("allow_different_class"))
		? Params->GetBoolField(TEXT("allow_different_class"))
		: false;
	if (!bAllowDifferentClass && OldObj->GetClass() != NewObj->GetClass())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Class mismatch (%s vs %s); pass allow_different_class=true to override"),
				*OldObj->GetClass()->GetName(), *NewObj->GetClass()->GetName()));
	}

	TArray<UObject*> ToConsolidate;
	ToConsolidate.Add(OldObj);

	const ObjectTools::FConsolidationResults Results =
		ObjectTools::ConsolidateObjects(NewObj, ToConsolidate, /*bShowDeleteConfirmation*/ false);

	const int32 DirtiedCount = Results.DirtiedPackages.Num();
	const int32 InvalidCount = Results.InvalidConsolidationObjs.Num();
	const int32 FailedCount  = Results.FailedConsolidationObjs.Num();

	TArray<TSharedPtr<FJsonValue>> DirtiedJson;
	DirtiedJson.Reserve(DirtiedCount);
	for (UPackage* Pkg : Results.DirtiedPackages)
	{
		if (Pkg) { DirtiedJson.Add(MakeShared<FJsonValueString>(Pkg->GetName())); }
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), FailedCount == 0);
	Result->SetStringField(TEXT("old_asset_path"), OldPath);
	Result->SetStringField(TEXT("new_asset_path"), NewPath);
	Result->SetNumberField(TEXT("dirtied_packages"), DirtiedCount);
	Result->SetNumberField(TEXT("invalid_count"), InvalidCount);
	Result->SetNumberField(TEXT("failed_count"), FailedCount);
	Result->SetArrayField(TEXT("dirtied_package_names"), DirtiedJson);
	Result->SetStringField(TEXT("note"),
		TEXT("Old asset deleted; an ObjectRedirector was left for unloaded consumers. ")
		TEXT("Run fixup_redirectors after consumer packages are touched to scrub them."));
	return Result;
}

// ============================================================================
// search_assets_fts — substring search across asset names + AssetRegistry tag
// values. The AssetRegistry tags are the same set the Content Browser exposes,
// so this transparently matches serialized FName/FString UPROPERTY values that
// the editor has surfaced (description, display name, tag containers, mesh
// material slots, weapon data fields with AssetRegistrySearchable, etc.) without
// loading any packages. Heavy packages stay unloaded.
//
// For matches that require deeper-than-registry inspection, callers should pair
// this with read_data_asset / read_blueprint_content on the returned paths.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetCommands::HandleSearchAssetsFTS(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (!Params->TryGetStringField(TEXT("query"), Query) || Query.IsEmpty())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'query' string"));
	}

	const FString Directory = Params->HasField(TEXT("directory"))
		? Params->GetStringField(TEXT("directory"))
		: FString(TEXT("/Game"));
	const int32 Limit = Params->HasField(TEXT("limit"))
		? FMath::Max(1, static_cast<int32>(Params->GetNumberField(TEXT("limit"))))
		: 50;
	const bool bCaseSensitive = Params->HasField(TEXT("case_sensitive"))
		&& Params->GetBoolField(TEXT("case_sensitive"));
	const bool bMatchNamesOnly = Params->HasField(TEXT("names_only"))
		&& Params->GetBoolField(TEXT("names_only"));
	const int32 SnippetRadius = Params->HasField(TEXT("snippet_radius"))
		? FMath::Clamp(static_cast<int32>(Params->GetNumberField(TEXT("snippet_radius"))), 0, 200)
		: 48;

	const FString ClassFilter = Params->HasField(TEXT("class_filter"))
		? Params->GetStringField(TEXT("class_filter"))
		: FString();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.SearchAllAssets(/*bSynchronousSearch*/ true);
	}

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add(FName(*Directory));

	if (!ClassFilter.IsEmpty())
	{
		UClass* FilterClass = FEpicUnrealMCPCommonUtils::ResolveClass(ClassFilter);
		if (FilterClass)
		{
			Filter.ClassPaths.Add(FilterClass->GetClassPathName());
		}
		else
		{
			// Allow callers to pass "/Script/Module.Class" forms verbatim.
			Filter.ClassPaths.Add(FTopLevelAssetPath(*ClassFilter));
		}
	}

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	const ESearchCase::Type Case = bCaseSensitive ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase;

	TArray<TSharedPtr<FJsonValue>> Matches;
	int32 Scanned = 0;
	int32 Truncated = 0;

	for (const FAssetData& Asset : AssetList)
	{
		++Scanned;
		const FString AssetName = Asset.AssetName.ToString();
		const FString PackageName = Asset.PackageName.ToString();

		TArray<TSharedPtr<FJsonValue>> HitFields;
		bool bNameHit = AssetName.Contains(Query, Case) || PackageName.Contains(Query, Case);
		if (bNameHit)
		{
			TSharedPtr<FJsonObject> Hit = MakeShareable(new FJsonObject);
			Hit->SetStringField(TEXT("field"), TEXT("__name__"));
			Hit->SetStringField(TEXT("value"), AssetName);
			HitFields.Add(MakeShareable(new FJsonValueObject(Hit)));
		}

		if (!bMatchNamesOnly)
		{
			// Walk every registry tag — covers description fields, display names, ARSearchable
			// UPROPERTY values, gameplay tag string snapshots, etc. without loading the asset.
			for (const TPair<FName, FAssetTagValueRef>& Tag : Asset.TagsAndValues)
			{
				const FString Value = Tag.Value.AsString();
				const int32 Idx = Value.Find(Query, Case);
				if (Idx == INDEX_NONE) continue;

				const int32 Start = FMath::Max(0, Idx - SnippetRadius);
				const int32 End = FMath::Min(Value.Len(), Idx + Query.Len() + SnippetRadius);
				const FString Snippet = Value.Mid(Start, End - Start);

				TSharedPtr<FJsonObject> Hit = MakeShareable(new FJsonObject);
				Hit->SetStringField(TEXT("field"), Tag.Key.ToString());
				Hit->SetStringField(TEXT("snippet"), Snippet);
				HitFields.Add(MakeShareable(new FJsonValueObject(Hit)));

				// Cap per-asset hits so a single mega-tag field doesn't push the response over size limits.
				if (HitFields.Num() >= 6) break;
			}
		}

		if (HitFields.Num() == 0) continue;

		if (Matches.Num() >= Limit)
		{
			++Truncated;
			continue;
		}

		TSharedPtr<FJsonObject> MatchObj = MakeShareable(new FJsonObject);
		MatchObj->SetStringField(TEXT("asset_name"), AssetName);
		MatchObj->SetStringField(TEXT("asset_path"), Asset.GetObjectPathString());
		MatchObj->SetStringField(TEXT("package_name"), PackageName);
		MatchObj->SetStringField(TEXT("class_path"), Asset.AssetClassPath.ToString());
		MatchObj->SetArrayField(TEXT("hits"), HitFields);
		Matches.Add(MakeShareable(new FJsonValueObject(MatchObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("query"), Query);
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetNumberField(TEXT("scanned"), Scanned);
	Result->SetNumberField(TEXT("matched"), Matches.Num());
	if (Truncated > 0)
	{
		Result->SetNumberField(TEXT("truncated_additional_matches"), Truncated);
	}
	Result->SetArrayField(TEXT("matches"), Matches);
	return Result;
}

// ============================================================================
// wait_for_asset_compile / wait_for_shaders / get_navmesh_build_status —
// async-completion truth. Asset/shader/navmesh work is fire-and-forget: the call
// that kicks it returns success on DISPATCH, not completion. These convert
// "dispatched" into "actually done" (the compile waiters block until drained and
// report the real remaining count; navmesh is non-forceable under invoker-gen so
// it returns honest STATUS rather than faking completion).
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetCommands::HandleWaitForAssetCompile(const TSharedPtr<FJsonObject>& Params)
{
	FAssetCompilingManager& Mgr = FAssetCompilingManager::Get();
	const int32 Before = Mgr.GetNumRemainingAssets();

	// FinishAllCompilation pumps the async tasks on the game thread until drained.
	Mgr.FinishAllCompilation();

	const int32 After = Mgr.GetNumRemainingAssets();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), After == 0);
	Result->SetNumberField(TEXT("remaining_before"), Before);
	Result->SetNumberField(TEXT("remaining"), After);
	Result->SetBoolField(TEXT("complete"), After == 0);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetCommands::HandleWaitForShaders(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	if (!GShaderCompilingManager)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetNumberField(TEXT("remaining_before"), 0);
		Result->SetNumberField(TEXT("remaining"), 0);
		Result->SetBoolField(TEXT("complete"), true);
		Result->SetStringField(TEXT("note"), TEXT("No shader compiling manager (cooked/no-RHI)"));
		return Result;
	}

	const int32 Before = GShaderCompilingManager->GetNumRemainingJobs();
	GShaderCompilingManager->FinishAllCompilation();
	const int32 After = GShaderCompilingManager->GetNumRemainingJobs();

	Result->SetBoolField(TEXT("success"), After == 0);
	Result->SetNumberField(TEXT("remaining_before"), Before);
	Result->SetNumberField(TEXT("remaining"), After);
	Result->SetBoolField(TEXT("complete"), After == 0);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetCommands::HandleGetNavmeshBuildStatus(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world"));
	}

	UNavigationSystemV1* NavSys = Cast<UNavigationSystemV1>(World->GetNavigationSystem());
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);

	if (!NavSys)
	{
		Result->SetBoolField(TEXT("has_navigation_system"), false);
		Result->SetBoolField(TEXT("complete"), false);
		Result->SetStringField(TEXT("note"), TEXT("World has no UNavigationSystemV1"));
		return Result;
	}

	const bool bInProgress = NavSys->IsNavigationBuildInProgress();
	const int32 Remaining = NavSys->GetNumRemainingBuildTasks();
	const int32 Running = NavSys->GetNumRunningBuildTasks();

	Result->SetBoolField(TEXT("has_navigation_system"), true);
	Result->SetBoolField(TEXT("in_progress"), bInProgress);
	Result->SetNumberField(TEXT("remaining_tasks"), Remaining);
	Result->SetNumberField(TEXT("running_tasks"), Running);
	// Honest completion: not building and nothing queued. Note: under invoker-only
	// dynamic generation, tiles only build around invokers — "complete" means the
	// build queue is drained, NOT that the whole world is covered (verify in PIE).
	Result->SetBoolField(TEXT("complete"), !bInProgress && Remaining == 0 && Running == 0);
	return Result;
}

// ============================================================================
// save_asset_checked — save with a writability guard. A read-only .uasset (P4
// "not checked out") makes the save path hit a FATAL assert, not a soft failure.
// This checks out under source control (best-effort) AND clears the read-only
// file attribute before saving, then reports what it had to do.
// ============================================================================

TSharedPtr<FJsonObject> FEpicUnrealMCPAssetCommands::HandleSaveAssetChecked(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: asset_path"));
	}
	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const bool bUseSourceControl = !Params->HasField(TEXT("use_source_control"))
		|| Params->GetBoolField(TEXT("use_source_control"));

	UObject* Asset = FEpicUnrealMCPCommonUtils::ResolveAsset(AssetPath);
	if (!Asset)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	UPackage* Package = Asset->GetPackage();
	FString Filename;
	const bool bHasFile = FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), Filename, FPackageName::GetAssetPackageExtension());

	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	const bool bFileExists = bHasFile && PF.FileExists(*Filename);
	const bool bWasReadOnly = bFileExists && PF.IsReadOnly(*Filename);

	bool bSourceControlCheckedOut = false;
	if (bUseSourceControl && bFileExists)
	{
		// Returns false (silently) if source control is disabled — harmless.
		bSourceControlCheckedOut = USourceControlHelpers::CheckOutOrAddFile(Filename, /*bSilent=*/true);
	}

	// Belt-and-suspenders: clear the read-only attribute even when SCC is off, since
	// a read-only save is the fatal-assert footgun this wrapper exists to prevent.
	if (bFileExists && PF.IsReadOnly(*Filename))
	{
		PF.SetReadOnly(*Filename, false);
	}

	const bool bSaved = UEditorAssetLibrary::SaveLoadedAsset(Asset, /*bOnlyIfIsDirty=*/false);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bSaved);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("filename"), Filename);
	Result->SetBoolField(TEXT("was_read_only"), bWasReadOnly);
	Result->SetBoolField(TEXT("source_control_checked_out"), bSourceControlCheckedOut);
	Result->SetBoolField(TEXT("saved"), bSaved);
	if (!bSaved)
	{
		Result->SetStringField(TEXT("error"), TEXT("SaveLoadedAsset returned false; check output log"));
	}
	return Result;
}
