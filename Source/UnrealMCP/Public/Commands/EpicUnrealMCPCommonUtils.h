// EpicUnrealMCPCommonUtils.h — Shared response helpers for MCP command handlers
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UNREALMCP_API FEpicUnrealMCPCommonUtils
{
public:
	static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& Message)
	{
		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("message"), Message);
		return Response;
	}

	static TSharedPtr<FJsonObject> CreateSuccessResponse(const TSharedPtr<FJsonObject>& Data = nullptr)
	{
		if (Data)
		{
			Data->SetBoolField(TEXT("success"), true);
			return Data;
		}
		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetBoolField(TEXT("success"), true);
		return Response;
	}

	static FVector GetVectorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
	{
		FVector Result = FVector::ZeroVector;
		const TSharedPtr<FJsonObject>* VecObj;
		if (JsonObject->TryGetObjectField(FieldName, VecObj))
		{
			(*VecObj)->TryGetNumberField(TEXT("x"), Result.X);
			(*VecObj)->TryGetNumberField(TEXT("y"), Result.Y);
			(*VecObj)->TryGetNumberField(TEXT("z"), Result.Z);
		}
		return Result;
	}

	static FRotator GetRotatorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
	{
		FRotator Result = FRotator::ZeroRotator;
		const TSharedPtr<FJsonObject>* RotObj;
		if (JsonObject->TryGetObjectField(FieldName, RotObj))
		{
			(*RotObj)->TryGetNumberField(TEXT("pitch"), Result.Pitch);
			(*RotObj)->TryGetNumberField(TEXT("yaw"), Result.Yaw);
			(*RotObj)->TryGetNumberField(TEXT("roll"), Result.Roll);
		}
		return Result;
	}
};
