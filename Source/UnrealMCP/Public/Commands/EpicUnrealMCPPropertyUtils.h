#pragma once

#include "CoreMinimal.h"
#include "Json.h"
#include "UObject/UnrealType.h"
#include "GameplayTagContainer.h"

/**
 * Shared UPROPERTY reflection utilities for serializing arbitrary UObject/UStruct properties to JSON.
 * Used by MCP commands that need to inspect or export object state.
 */
class UNREALMCP_API FEpicUnrealMCPPropertyUtils
{
public:
	/**
	 * Serialize a single FProperty value to a JSON value.
	 * Handles bool, numeric, string, name, text, enum, struct (with special-case handling for
	 * common engine types), object references, arrays, sets, and maps.
	 *
	 * @param Property  The property descriptor (type metadata).
	 * @param ValuePtr  Pointer to the raw property value in memory.
	 * @return A JSON value representing the property, or a string "(unsupported type)" for unhandled types.
	 */
	static TSharedPtr<FJsonValue> SerializePropertyValue(FProperty* Property, const void* ValuePtr);

	/**
	 * Serialize all UPROPERTY fields on a UObject to a JSON object.
	 * Includes a "_class" field with the full class path and a "_class_hierarchy" array
	 * walking the super-class chain. Skips deprecated properties.
	 *
	 * @param Object  The UObject to serialize.
	 * @return A JSON object with one field per UPROPERTY, or nullptr if Object is null.
	 */
	static TSharedPtr<FJsonObject> SerializeObjectProperties(UObject* Object);

	/**
	 * Serialize all fields of a UScriptStruct instance to a JSON object.
	 *
	 * @param StructType  The struct's type descriptor.
	 * @param StructData  Pointer to the raw struct data in memory.
	 * @return A JSON object with one field per struct property.
	 */
	static TSharedPtr<FJsonObject> SerializeStructProperties(UScriptStruct* StructType, const void* StructData);
};
