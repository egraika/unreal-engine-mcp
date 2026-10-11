#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for executing Python in the live editor.
 * Wraps IPythonScriptPlugin::ExecPythonCommandEx so MCP clients can drive the
 * editor with arbitrary Python while the editor process is running.
 *
 * Commands:
 *   run_python       — execute literal Python source (file/statement/eval mode)
 *   run_python_file  — execute a .py file on disk (with optional argument string)
 *
 * All execution happens on the game thread (the bridge already marshals there).
 */
class UNREALMCP_API FEpicUnrealMCPPythonCommands
{
public:
	FEpicUnrealMCPPythonCommands();
	~FEpicUnrealMCPPythonCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleRunPython(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRunPythonFile(const TSharedPtr<FJsonObject>& Params);

	/** Build a JSON response from a completed FPythonCommandEx (success path or error path). */
	TSharedPtr<FJsonObject> BuildResponse(const struct FPythonCommandEx& CommandEx, bool bRanSuccessfully);
};
