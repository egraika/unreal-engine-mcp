#include "Commands/EpicUnrealMCPPythonCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	EPythonCommandExecutionMode ParseMode(const FString& ModeStr, EPythonCommandExecutionMode Default)
	{
		EPythonCommandExecutionMode Out = Default;
		LexFromString(Out, *ModeStr);
		return Out;
	}

	EPythonFileExecutionScope ParseScope(const FString& ScopeStr, EPythonFileExecutionScope Default)
	{
		if (ScopeStr.Equals(TEXT("Private"), ESearchCase::IgnoreCase))
		{
			return EPythonFileExecutionScope::Private;
		}
		if (ScopeStr.Equals(TEXT("Public"), ESearchCase::IgnoreCase))
		{
			return EPythonFileExecutionScope::Public;
		}
		return Default;
	}

	// A Python wrapper keeps its UObject alive. When a map closes, wrappers of its world and actors stop holding them
	// (those objects are marked garbage), but a UPackage is not, so a Public-scope variable holding a map's package
	// survives into the next map load and UEditorEngine::CheckForWorldGCLeaks kills the editor ("World Memory Leaks",
	// EditorServer.cpp:1951; 2026-10-08, a probe's global kept NewMap's package). After every call, drop Public-scope
	// variables that hold a package, directly or inside a list / tuple / set / dict.
	void DropPublicPackageReferences(IPythonScriptPlugin& Py)
	{
		FPythonCommandEx Cleanup;
		Cleanup.Command = TEXT(
			"def __mcp_drop_package_refs(g):\n"
			"    import unreal\n"
			"    def holds(v):\n"
			"        if isinstance(v, unreal.Package):\n"
			"            return True\n"
			"        if isinstance(v, dict):\n"
			"            v = list(v.values())\n"
			"        if isinstance(v, (list, tuple, set, frozenset)):\n"
			"            return any(isinstance(x, unreal.Package) for x in v)\n"
			"        return False\n"
			"    for k in [k for k, v in list(g.items()) if not k.startswith('__') and holds(v)]:\n"
			"        del g[k]\n"
			"__mcp_drop_package_refs(globals())\n"
			"del __mcp_drop_package_refs\n");
		Cleanup.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Cleanup.FileExecutionScope = EPythonFileExecutionScope::Public;  // the console dict the Public scope persists in
		Cleanup.Flags |= EPythonCommandFlags::Unattended;
		Py.ExecPythonCommandEx(Cleanup);
	}
}

FEpicUnrealMCPPythonCommands::FEpicUnrealMCPPythonCommands()
{
}

FEpicUnrealMCPPythonCommands::~FEpicUnrealMCPPythonCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPPythonCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("run_python"))
	{
		return HandleRunPython(Params);
	}
	if (CommandType == TEXT("run_python_file"))
	{
		return HandleRunPythonFile(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown Python command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPPythonCommands::HandleRunPython(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("code")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: code"));
	}

	IPythonScriptPlugin* Py = IPythonScriptPlugin::Get();
	if (!Py)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("PythonScriptPlugin module is not loaded — enable it in the project."));
	}
	if (!Py->IsPythonAvailable())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Python is not available in this editor build (PythonScriptPlugin reports IsPythonAvailable=false)."));
	}

	FPythonCommandEx Cmd;
	Cmd.Command = Params->GetStringField(TEXT("code"));

	// Default ExecuteFile so multi-statement payloads work; override via "mode".
	const FString ModeStr = Params->HasField(TEXT("mode")) ? Params->GetStringField(TEXT("mode")) : FString();
	Cmd.ExecutionMode = ParseMode(ModeStr, EPythonCommandExecutionMode::ExecuteFile);

	// Default Public scope so a sequence of run_python calls share globals
	// (matches the "live editor REPL" use case the user is asking for).
	const FString ScopeStr = Params->HasField(TEXT("scope")) ? Params->GetStringField(TEXT("scope")) : FString();
	Cmd.FileExecutionScope = ParseScope(ScopeStr, EPythonFileExecutionScope::Public);

	const bool bUnattended = Params->HasField(TEXT("unattended"))
		? Params->GetBoolField(TEXT("unattended"))
		: true;
	if (bUnattended)
	{
		Cmd.Flags |= EPythonCommandFlags::Unattended;
	}

	const bool bOk = Py->ExecPythonCommandEx(Cmd);
	TSharedPtr<FJsonObject> Result = BuildResponse(Cmd, bOk);
	DropPublicPackageReferences(*Py);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPPythonCommands::HandleRunPythonFile(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("file_path")))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required field: file_path"));
	}

	IPythonScriptPlugin* Py = IPythonScriptPlugin::Get();
	if (!Py)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("PythonScriptPlugin module is not loaded — enable it in the project."));
	}
	if (!Py->IsPythonAvailable())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Python is not available in this editor build."));
	}

	FString FilePath = Params->GetStringField(TEXT("file_path"));
	FPaths::NormalizeFilename(FilePath);

	if (!IFileManager::Get().FileExists(*FilePath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Python file not found: %s"), *FilePath));
	}

	const FString Args = Params->HasField(TEXT("args")) ? Params->GetStringField(TEXT("args")) : FString();

	FPythonCommandEx Cmd;
	// IPythonScriptPlugin treats the Command field as either source or "<file> <args>".
	// Quote the path so spaces survive.
	Cmd.Command = Args.IsEmpty()
		? FString::Printf(TEXT("\"%s\""), *FilePath)
		: FString::Printf(TEXT("\"%s\" %s"), *FilePath, *Args);
	Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;

	const FString ScopeStr = Params->HasField(TEXT("scope")) ? Params->GetStringField(TEXT("scope")) : FString();
	Cmd.FileExecutionScope = ParseScope(ScopeStr, EPythonFileExecutionScope::Public);

	const bool bUnattended = Params->HasField(TEXT("unattended"))
		? Params->GetBoolField(TEXT("unattended"))
		: true;
	if (bUnattended)
	{
		Cmd.Flags |= EPythonCommandFlags::Unattended;
	}

	const bool bOk = Py->ExecPythonCommandEx(Cmd);
	TSharedPtr<FJsonObject> Result = BuildResponse(Cmd, bOk);
	DropPublicPackageReferences(*Py);
	Result->SetStringField(TEXT("file_path"), FilePath);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPPythonCommands::BuildResponse(const FPythonCommandEx& CommandEx, bool bRanSuccessfully)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bRanSuccessfully);

	// CommandResult holds the eval value on success or the Python exception trace on failure.
	Result->SetStringField(TEXT("result"), CommandEx.CommandResult);

	// Flatten LogOutput into stdout/stderr buckets so callers don't have to inspect entry types.
	TArray<TSharedPtr<FJsonValue>> EntriesArray;
	FString StdoutBuffer;
	FString StderrBuffer;
	for (const FPythonLogOutputEntry& Entry : CommandEx.LogOutput)
	{
		TSharedPtr<FJsonObject> EntryObj = MakeShareable(new FJsonObject);
		EntryObj->SetStringField(TEXT("type"), LexToString(Entry.Type));
		EntryObj->SetStringField(TEXT("text"), Entry.Output);
		EntriesArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));

		if (Entry.Type == EPythonLogOutputType::Error)
		{
			StderrBuffer.Append(Entry.Output);
		}
		else
		{
			StdoutBuffer.Append(Entry.Output);
		}
	}
	Result->SetArrayField(TEXT("log"), EntriesArray);
	Result->SetStringField(TEXT("stdout"), StdoutBuffer);
	Result->SetStringField(TEXT("stderr"), StderrBuffer);

	if (!bRanSuccessfully)
	{
		// Surface a single-line error for the bridge's success/error envelope.
		const FString ErrorLine = CommandEx.CommandResult.IsEmpty()
			? (StderrBuffer.IsEmpty() ? FString(TEXT("Python execution failed")) : StderrBuffer)
			: CommandEx.CommandResult;
		Result->SetStringField(TEXT("error"), ErrorLine);
	}

	return Result;
}
