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

	// ---- Deserialization (write path) ----

	/**
	 * Deserialize a JSON value into a single FProperty value (inverse of SerializePropertyValue).
	 * Handles all types that SerializePropertyValue handles, including instanced subobjects.
	 *
	 * For instanced object properties (CPF_PersistentInstance), pass a JSON object with a "_class"
	 * field to create a new inline subobject via NewObject. The Outer parameter determines the
	 * owning object for NewObject calls.
	 *
	 * @param Property   The property descriptor (type metadata).
	 * @param ValuePtr   Pointer to the raw property value in memory (will be written).
	 * @param JsonValue  The JSON value to deserialize from.
	 * @param OutError   Filled with a human-readable error message on failure.
	 * @param Outer      The owning UObject for creating instanced subobjects (passed to NewObject).
	 * @return true if the value was successfully written.
	 */
	static bool DeserializePropertyValue(FProperty* Property, void* ValuePtr,
		const TSharedPtr<FJsonValue>& JsonValue, FString& OutError, UObject* Outer = nullptr);

	/**
	 * Deserialize a JSON object into a UObject's UPROPERTY fields (partial update).
	 * Only properties present in the JSON object are modified; others are untouched.
	 *
	 * @param Object     The UObject to write into.
	 * @param JsonObject The JSON object with property_name -> value pairs.
	 * @param OutErrors  Collects per-property error messages.
	 * @return true if all properties were successfully written (OutErrors is empty).
	 */
	static bool DeserializeObjectProperties(UObject* Object,
		const TSharedPtr<FJsonObject>& JsonObject, TArray<FString>& OutErrors);

	/**
	 * Deserialize a JSON object into a UScriptStruct's fields (partial update).
	 *
	 * @param StructType  The struct's type descriptor.
	 * @param StructData  Pointer to the raw struct data in memory (will be written).
	 * @param JsonObject  The JSON object with field_name -> value pairs.
	 * @param OutErrors   Collects per-field error messages.
	 * @return true if all fields were successfully written.
	 */
	static bool DeserializeStructProperties(UScriptStruct* StructType, void* StructData,
		const TSharedPtr<FJsonObject>& JsonObject, TArray<FString>& OutErrors);
};
