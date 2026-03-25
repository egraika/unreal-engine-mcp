#include "Commands/EpicUnrealMCPPropertyUtils.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "GameplayTagContainer.h"
#include "EditorAssetLibrary.h"

TSharedPtr<FJsonValue> FEpicUnrealMCPPropertyUtils::SerializePropertyValue(FProperty* Property, const void* ValuePtr)
{
	if (!Property || !ValuePtr)
	{
		return MakeShared<FJsonValueNull>();
	}

	// 1. Bool
	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
	}

	// 2. Numeric (int / float / double)
	if (const FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		// Check for enum-backed byte first (handled in FByteProperty section below)
		if (!CastField<FByteProperty>(Property) || !CastField<FByteProperty>(Property)->GetIntPropertyEnum())
		{
			if (NumericProp->IsInteger())
			{
				return MakeShared<FJsonValueNumber>(static_cast<double>(NumericProp->GetSignedIntPropertyValue(ValuePtr)));
			}
			else
			{
				return MakeShared<FJsonValueNumber>(NumericProp->GetFloatingPointPropertyValue(ValuePtr));
			}
		}
	}

	// 3. FString
	if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
	}

	// 4. FName
	if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(ValuePtr).ToString());
	}

	// 5. FText
	if (const FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		return MakeShared<FJsonValueString>(TextProp->GetPropertyValue(ValuePtr).ToString());
	}

	// 6. Enum property (UE5 UENUM with underlying type)
	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		const FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
		const UEnum* EnumDef = EnumProp->GetEnum();
		if (EnumDef && UnderlyingProp)
		{
			const int64 EnumValue = UnderlyingProp->GetSignedIntPropertyValue(ValuePtr);
			const FString EnumName = EnumDef->GetNameStringByValue(EnumValue);
			return MakeShared<FJsonValueString>(EnumName.IsEmpty()
				? FString::Printf(TEXT("%lld"), EnumValue)
				: EnumName);
		}
		return MakeShared<FJsonValueString>(TEXT("(unknown enum)"));
	}

	// 7. Byte property (may be enum-backed TEnumAsByte)
	if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (const UEnum* EnumDef = ByteProp->GetIntPropertyEnum())
		{
			const int64 EnumValue = static_cast<int64>(ByteProp->GetPropertyValue(ValuePtr));
			const FString EnumName = EnumDef->GetNameStringByValue(EnumValue);
			return MakeShared<FJsonValueString>(EnumName.IsEmpty()
				? FString::Printf(TEXT("%lld"), EnumValue)
				: EnumName);
		}
		return MakeShared<FJsonValueNumber>(static_cast<double>(ByteProp->GetPropertyValue(ValuePtr)));
	}

	// 8. Struct property - special-case common engine types, then fall back to recursive
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		UScriptStruct* Struct = StructProp->Struct;
		if (!Struct)
		{
			return MakeShared<FJsonValueNull>();
		}

		const FName StructName = Struct->GetFName();

		// FVector
		if (StructName == NAME_Vector)
		{
			const FVector* Vec = static_cast<const FVector*>(ValuePtr);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("X"), Vec->X);
			Obj->SetNumberField(TEXT("Y"), Vec->Y);
			Obj->SetNumberField(TEXT("Z"), Vec->Z);
			return MakeShared<FJsonValueObject>(Obj);
		}

		// FRotator
		if (StructName == NAME_Rotator)
		{
			const FRotator* Rot = static_cast<const FRotator*>(ValuePtr);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("Pitch"), Rot->Pitch);
			Obj->SetNumberField(TEXT("Yaw"), Rot->Yaw);
			Obj->SetNumberField(TEXT("Roll"), Rot->Roll);
			return MakeShared<FJsonValueObject>(Obj);
		}

		// FLinearColor
		if (StructName == NAME_LinearColor)
		{
			const FLinearColor* Color = static_cast<const FLinearColor*>(ValuePtr);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("R"), Color->R);
			Obj->SetNumberField(TEXT("G"), Color->G);
			Obj->SetNumberField(TEXT("B"), Color->B);
			Obj->SetNumberField(TEXT("A"), Color->A);
			return MakeShared<FJsonValueObject>(Obj);
		}

		// FColor
		if (StructName == NAME_Color)
		{
			const FColor* Color = static_cast<const FColor*>(ValuePtr);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("R"), Color->R);
			Obj->SetNumberField(TEXT("G"), Color->G);
			Obj->SetNumberField(TEXT("B"), Color->B);
			Obj->SetNumberField(TEXT("A"), Color->A);
			return MakeShared<FJsonValueObject>(Obj);
		}

		// FGameplayTag
		static const FName NAME_GameplayTag(TEXT("GameplayTag"));
		if (StructName == NAME_GameplayTag)
		{
			const FGameplayTag* Tag = static_cast<const FGameplayTag*>(ValuePtr);
			return MakeShared<FJsonValueString>(Tag->ToString());
		}

		// FGameplayTagContainer
		static const FName NAME_GameplayTagContainer(TEXT("GameplayTagContainer"));
		if (StructName == NAME_GameplayTagContainer)
		{
			const FGameplayTagContainer* Container = static_cast<const FGameplayTagContainer*>(ValuePtr);
			TArray<TSharedPtr<FJsonValue>> TagArray;
			for (const FGameplayTag& Tag : *Container)
			{
				TagArray.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			}
			return MakeShared<FJsonValueArray>(TagArray);
		}

		// FSoftObjectPath
		static const FName NAME_SoftObjectPath(TEXT("SoftObjectPath"));
		if (StructName == NAME_SoftObjectPath)
		{
			const FSoftObjectPath* Path = static_cast<const FSoftObjectPath*>(ValuePtr);
			return MakeShared<FJsonValueString>(Path->ToString());
		}

		// FSoftClassPath
		static const FName NAME_SoftClassPath(TEXT("SoftClassPath"));
		if (StructName == NAME_SoftClassPath)
		{
			const FSoftClassPath* Path = static_cast<const FSoftClassPath*>(ValuePtr);
			return MakeShared<FJsonValueString>(Path->ToString());
		}

		// FVector2D
		if (StructName == NAME_Vector2D)
		{
			const FVector2D* Vec = static_cast<const FVector2D*>(ValuePtr);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("X"), Vec->X);
			Obj->SetNumberField(TEXT("Y"), Vec->Y);
			return MakeShared<FJsonValueObject>(Obj);
		}

		// FIntPoint
		static const FName NAME_IntPointStruct(TEXT("IntPoint"));
		if (StructName == NAME_IntPointStruct)
		{
			const FIntPoint* Pt = static_cast<const FIntPoint*>(ValuePtr);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("X"), Pt->X);
			Obj->SetNumberField(TEXT("Y"), Pt->Y);
			return MakeShared<FJsonValueObject>(Obj);
		}

		// FGuid
		static const FName NAME_Guid(TEXT("Guid"));
		if (StructName == NAME_Guid)
		{
			const FGuid* Guid = static_cast<const FGuid*>(ValuePtr);
			return MakeShared<FJsonValueString>(Guid->ToString());
		}

		// Default: recursive struct serialization
		TSharedPtr<FJsonObject> StructObj = SerializeStructProperties(Struct, ValuePtr);
		return MakeShared<FJsonValueObject>(StructObj);
	}

	// 9. Object reference (UObject*, TObjectPtr, TWeakObjectPtr via FObjectPropertyBase)
	if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		UObject* ObjValue = ObjProp->GetObjectPropertyValue(ValuePtr);
		if (ObjValue)
		{
			// For instanced/inline subobjects, serialize as full JSON object with _class + properties
			const bool bIsInstanced = Property->HasAnyPropertyFlags(CPF_PersistentInstance | CPF_InstancedReference);
			if (bIsInstanced)
			{
				TSharedPtr<FJsonObject> InlineObj = SerializeObjectProperties(ObjValue);
				if (InlineObj.IsValid())
				{
					return MakeShared<FJsonValueObject>(InlineObj);
				}
			}
			return MakeShared<FJsonValueString>(ObjValue->GetPathName());
		}
		return MakeShared<FJsonValueNull>();
	}

	// 10. Array
	if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		JsonArray.Reserve(ArrayHelper.Num());

		for (int32 i = 0; i < ArrayHelper.Num(); ++i)
		{
			JsonArray.Add(SerializePropertyValue(ArrayProp->Inner, ArrayHelper.GetRawPtr(i)));
		}
		return MakeShared<FJsonValueArray>(JsonArray);
	}

	// 11. Set
	if (const FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		FScriptSetHelper SetHelper(SetProp, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		JsonArray.Reserve(SetHelper.Num());

		for (int32 i = 0; i < SetHelper.GetMaxIndex(); ++i)
		{
			if (SetHelper.IsValidIndex(i))
			{
				JsonArray.Add(SerializePropertyValue(SetProp->ElementProp, SetHelper.GetElementPtr(i)));
			}
		}
		return MakeShared<FJsonValueArray>(JsonArray);
	}

	// 12. Map
	if (const FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		FScriptMapHelper MapHelper(MapProp, ValuePtr);
		TSharedPtr<FJsonObject> MapObj = MakeShared<FJsonObject>();

		for (int32 i = 0; i < MapHelper.GetMaxIndex(); ++i)
		{
			if (MapHelper.IsValidIndex(i))
			{
				// Serialize key as string for the JSON field name
				TSharedPtr<FJsonValue> KeyJson = SerializePropertyValue(MapProp->KeyProp, MapHelper.GetKeyPtr(i));
				FString KeyString;
				if (KeyJson.IsValid())
				{
					if (KeyJson->Type == EJson::String)
					{
						KeyString = KeyJson->AsString();
					}
					else if (KeyJson->Type == EJson::Number)
					{
						KeyString = FString::SanitizeFloat(KeyJson->AsNumber());
					}
					else if (KeyJson->Type == EJson::Boolean)
					{
						KeyString = KeyJson->AsBool() ? TEXT("true") : TEXT("false");
					}
					else
					{
						// Fallback: serialize to compact JSON string
						FString TempString;
						TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
							TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&TempString);
						FJsonSerializer::Serialize(KeyJson.ToSharedRef(), TEXT(""), Writer);
						KeyString = TempString;
					}
				}
				else
				{
					KeyString = FString::Printf(TEXT("key_%d"), i);
				}

				TSharedPtr<FJsonValue> ValJson = SerializePropertyValue(MapProp->ValueProp, MapHelper.GetValuePtr(i));
				MapObj->SetField(KeyString, ValJson);
			}
		}
		return MakeShared<FJsonValueObject>(MapObj);
	}

	// 13. Soft object reference (TSoftObjectPtr)
	if (const FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPtr* SoftPtr = static_cast<const FSoftObjectPtr*>(ValuePtr);
		return MakeShared<FJsonValueString>(SoftPtr->ToString());
	}

	// 14. Soft class reference (TSoftClassPtr)
	if (const FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
	{
		const FSoftObjectPtr* SoftPtr = static_cast<const FSoftObjectPtr*>(ValuePtr);
		return MakeShared<FJsonValueString>(SoftPtr->ToString());
	}

	// 15. Fallback
	return MakeShared<FJsonValueString>(TEXT("(unsupported type)"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPPropertyUtils::SerializeObjectProperties(UObject* Object)
{
	if (!Object)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Add class metadata
	Result->SetStringField(TEXT("_class"), Object->GetClass()->GetPathName());

	// Build class hierarchy array
	TArray<TSharedPtr<FJsonValue>> HierarchyArray;
	for (UClass* Super = Object->GetClass(); Super != nullptr; Super = Super->GetSuperClass())
	{
		HierarchyArray.Add(MakeShared<FJsonValueString>(Super->GetPathName()));
	}
	Result->SetArrayField(TEXT("_class_hierarchy"), HierarchyArray);

	// Iterate all UPROPERTY fields including inherited ones
	for (TFieldIterator<FProperty> PropIt(Object->GetClass(), EFieldIterationFlags::IncludeSuper); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Skip deprecated properties
		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		TSharedPtr<FJsonValue> JsonValue = SerializePropertyValue(Property, ValuePtr);
		Result->SetField(Property->GetName(), JsonValue);
	}

	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPPropertyUtils::SerializeStructProperties(UScriptStruct* StructType, const void* StructData)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!StructType || !StructData)
	{
		return Result;
	}

	for (TFieldIterator<FProperty> PropIt(StructType); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Skip deprecated properties
		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(StructData);
		TSharedPtr<FJsonValue> JsonValue = SerializePropertyValue(Property, ValuePtr);
		Result->SetField(Property->GetName(), JsonValue);
	}

	return Result;
}

// ============================================================================
// Deserialization (write path) — inverse of SerializePropertyValue
// ============================================================================

bool FEpicUnrealMCPPropertyUtils::DeserializePropertyValue(FProperty* Property, void* ValuePtr,
	const TSharedPtr<FJsonValue>& JsonValue, FString& OutError, UObject* Outer)
{
	if (!Property || !ValuePtr)
	{
		OutError = TEXT("Null property or value pointer");
		return false;
	}

	if (!JsonValue.IsValid() || JsonValue->IsNull())
	{
		// Null JSON → zero-initialize the property
		Property->ClearValue(ValuePtr);
		return true;
	}

	// 1. Bool
	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		if (JsonValue->Type != EJson::Boolean && JsonValue->Type != EJson::Number)
		{
			OutError = FString::Printf(TEXT("Property '%s': expected bool, got JSON type %d"), *Property->GetName(), static_cast<int32>(JsonValue->Type));
			return false;
		}
		BoolProp->SetPropertyValue(ValuePtr, JsonValue->AsBool());
		return true;
	}

	// 2. Numeric (int / float / double) — but not enum-backed byte
	if (const FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		if (!CastField<FByteProperty>(Property) || !CastField<FByteProperty>(Property)->GetIntPropertyEnum())
		{
			if (JsonValue->Type != EJson::Number && JsonValue->Type != EJson::String)
			{
				OutError = FString::Printf(TEXT("Property '%s': expected number, got JSON type %d"), *Property->GetName(), static_cast<int32>(JsonValue->Type));
				return false;
			}
			const double NumVal = JsonValue->AsNumber();
			if (NumericProp->IsInteger())
			{
				NumericProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(NumVal));
			}
			else
			{
				NumericProp->SetFloatingPointPropertyValue(ValuePtr, NumVal);
			}
			return true;
		}
	}

	// 3. FString
	if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		StrProp->SetPropertyValue(ValuePtr, JsonValue->AsString());
		return true;
	}

	// 4. FName
	if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		NameProp->SetPropertyValue(ValuePtr, FName(*JsonValue->AsString()));
		return true;
	}

	// 5. FText
	if (const FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		TextProp->SetPropertyValue(ValuePtr, FText::FromString(JsonValue->AsString()));
		return true;
	}

	// 6. Enum property (UE5 UENUM with underlying type)
	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		const FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
		const UEnum* EnumDef = EnumProp->GetEnum();
		if (EnumDef && UnderlyingProp)
		{
			const FString EnumStr = JsonValue->AsString();
			const int64 EnumValue = EnumDef->GetValueByNameString(EnumStr);
			if (EnumValue == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Property '%s': unknown enum value '%s'"), *Property->GetName(), *EnumStr);
				return false;
			}
			UnderlyingProp->SetIntPropertyValue(ValuePtr, EnumValue);
			return true;
		}
		OutError = FString::Printf(TEXT("Property '%s': enum property has no enum definition"), *Property->GetName());
		return false;
	}

	// 7. Byte property (may be enum-backed TEnumAsByte)
	if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (const UEnum* EnumDef = ByteProp->GetIntPropertyEnum())
		{
			const FString EnumStr = JsonValue->AsString();
			const int64 EnumValue = EnumDef->GetValueByNameString(EnumStr);
			if (EnumValue == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Property '%s': unknown enum value '%s'"), *Property->GetName(), *EnumStr);
				return false;
			}
			ByteProp->SetIntPropertyValue(ValuePtr, EnumValue);
			return true;
		}
		// Plain byte
		ByteProp->SetPropertyValue(ValuePtr, static_cast<uint8>(JsonValue->AsNumber()));
		return true;
	}

	// 8. Struct property
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		UScriptStruct* Struct = StructProp->Struct;
		if (!Struct)
		{
			OutError = FString::Printf(TEXT("Property '%s': null struct type"), *Property->GetName());
			return false;
		}

		const FName StructName = Struct->GetFName();

		// FVector
		if (StructName == NAME_Vector)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!JsonValue->TryGetObject(ObjPtr) || !ObjPtr)
			{
				OutError = FString::Printf(TEXT("Property '%s': expected JSON object for FVector"), *Property->GetName());
				return false;
			}
			FVector* Vec = static_cast<FVector*>(ValuePtr);
			const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
			if (Obj->HasField(TEXT("X"))) Vec->X = Obj->GetNumberField(TEXT("X"));
			if (Obj->HasField(TEXT("Y"))) Vec->Y = Obj->GetNumberField(TEXT("Y"));
			if (Obj->HasField(TEXT("Z"))) Vec->Z = Obj->GetNumberField(TEXT("Z"));
			return true;
		}

		// FRotator
		if (StructName == NAME_Rotator)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!JsonValue->TryGetObject(ObjPtr) || !ObjPtr)
			{
				OutError = FString::Printf(TEXT("Property '%s': expected JSON object for FRotator"), *Property->GetName());
				return false;
			}
			FRotator* Rot = static_cast<FRotator*>(ValuePtr);
			const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
			if (Obj->HasField(TEXT("Pitch"))) Rot->Pitch = Obj->GetNumberField(TEXT("Pitch"));
			if (Obj->HasField(TEXT("Yaw"))) Rot->Yaw = Obj->GetNumberField(TEXT("Yaw"));
			if (Obj->HasField(TEXT("Roll"))) Rot->Roll = Obj->GetNumberField(TEXT("Roll"));
			return true;
		}

		// FLinearColor
		if (StructName == NAME_LinearColor)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!JsonValue->TryGetObject(ObjPtr) || !ObjPtr)
			{
				OutError = FString::Printf(TEXT("Property '%s': expected JSON object for FLinearColor"), *Property->GetName());
				return false;
			}
			FLinearColor* Color = static_cast<FLinearColor*>(ValuePtr);
			const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
			if (Obj->HasField(TEXT("R"))) Color->R = static_cast<float>(Obj->GetNumberField(TEXT("R")));
			if (Obj->HasField(TEXT("G"))) Color->G = static_cast<float>(Obj->GetNumberField(TEXT("G")));
			if (Obj->HasField(TEXT("B"))) Color->B = static_cast<float>(Obj->GetNumberField(TEXT("B")));
			if (Obj->HasField(TEXT("A"))) Color->A = static_cast<float>(Obj->GetNumberField(TEXT("A")));
			return true;
		}

		// FColor
		if (StructName == NAME_Color)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!JsonValue->TryGetObject(ObjPtr) || !ObjPtr)
			{
				OutError = FString::Printf(TEXT("Property '%s': expected JSON object for FColor"), *Property->GetName());
				return false;
			}
			FColor* Color = static_cast<FColor*>(ValuePtr);
			const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
			if (Obj->HasField(TEXT("R"))) Color->R = static_cast<uint8>(Obj->GetNumberField(TEXT("R")));
			if (Obj->HasField(TEXT("G"))) Color->G = static_cast<uint8>(Obj->GetNumberField(TEXT("G")));
			if (Obj->HasField(TEXT("B"))) Color->B = static_cast<uint8>(Obj->GetNumberField(TEXT("B")));
			if (Obj->HasField(TEXT("A"))) Color->A = static_cast<uint8>(Obj->GetNumberField(TEXT("A")));
			return true;
		}

		// FGameplayTag
		static const FName NAME_GameplayTag(TEXT("GameplayTag"));
		if (StructName == NAME_GameplayTag)
		{
			const FString TagStr = JsonValue->AsString();
			FGameplayTag* Tag = static_cast<FGameplayTag*>(ValuePtr);
			if (TagStr.IsEmpty())
			{
				*Tag = FGameplayTag();
			}
			else
			{
				*Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), /*bErrorIfNotFound=*/false);
				if (!Tag->IsValid())
				{
					OutError = FString::Printf(TEXT("Property '%s': unknown gameplay tag '%s'"), *Property->GetName(), *TagStr);
					return false;
				}
			}
			return true;
		}

		// FGameplayTagContainer
		static const FName NAME_GameplayTagContainer(TEXT("GameplayTagContainer"));
		if (StructName == NAME_GameplayTagContainer)
		{
			const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
			if (!JsonValue->TryGetArray(ArrPtr) || !ArrPtr)
			{
				OutError = FString::Printf(TEXT("Property '%s': expected JSON array for FGameplayTagContainer"), *Property->GetName());
				return false;
			}
			FGameplayTagContainer* Container = static_cast<FGameplayTagContainer*>(ValuePtr);
			Container->Reset();
			for (const TSharedPtr<FJsonValue>& Elem : *ArrPtr)
			{
				const FString TagStr = Elem->AsString();
				FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), /*bErrorIfNotFound=*/false);
				if (Tag.IsValid())
				{
					Container->AddTag(Tag);
				}
				else
				{
					OutError = FString::Printf(TEXT("Property '%s': unknown gameplay tag '%s'"), *Property->GetName(), *TagStr);
					return false;
				}
			}
			return true;
		}

		// FSoftObjectPath
		static const FName NAME_SoftObjectPath(TEXT("SoftObjectPath"));
		if (StructName == NAME_SoftObjectPath)
		{
			FSoftObjectPath* Path = static_cast<FSoftObjectPath*>(ValuePtr);
			Path->SetPath(JsonValue->AsString());
			return true;
		}

		// FSoftClassPath
		static const FName NAME_SoftClassPath(TEXT("SoftClassPath"));
		if (StructName == NAME_SoftClassPath)
		{
			FSoftClassPath* Path = static_cast<FSoftClassPath*>(ValuePtr);
			Path->SetPath(JsonValue->AsString());
			return true;
		}

		// FVector2D
		if (StructName == NAME_Vector2D)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!JsonValue->TryGetObject(ObjPtr) || !ObjPtr)
			{
				OutError = FString::Printf(TEXT("Property '%s': expected JSON object for FVector2D"), *Property->GetName());
				return false;
			}
			FVector2D* Vec = static_cast<FVector2D*>(ValuePtr);
			const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
			if (Obj->HasField(TEXT("X"))) Vec->X = Obj->GetNumberField(TEXT("X"));
			if (Obj->HasField(TEXT("Y"))) Vec->Y = Obj->GetNumberField(TEXT("Y"));
			return true;
		}

		// FIntPoint
		static const FName NAME_IntPointStruct(TEXT("IntPoint"));
		if (StructName == NAME_IntPointStruct)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!JsonValue->TryGetObject(ObjPtr) || !ObjPtr)
			{
				OutError = FString::Printf(TEXT("Property '%s': expected JSON object for FIntPoint"), *Property->GetName());
				return false;
			}
			FIntPoint* Pt = static_cast<FIntPoint*>(ValuePtr);
			const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
			if (Obj->HasField(TEXT("X"))) Pt->X = static_cast<int32>(Obj->GetNumberField(TEXT("X")));
			if (Obj->HasField(TEXT("Y"))) Pt->Y = static_cast<int32>(Obj->GetNumberField(TEXT("Y")));
			return true;
		}

		// FGuid
		static const FName NAME_Guid(TEXT("Guid"));
		if (StructName == NAME_Guid)
		{
			FGuid* Guid = static_cast<FGuid*>(ValuePtr);
			if (!FGuid::Parse(JsonValue->AsString(), *Guid))
			{
				OutError = FString::Printf(TEXT("Property '%s': invalid GUID string '%s'"), *Property->GetName(), *JsonValue->AsString());
				return false;
			}
			return true;
		}

		// Default: recursive struct deserialization
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!JsonValue->TryGetObject(ObjPtr) || !ObjPtr)
		{
			OutError = FString::Printf(TEXT("Property '%s': expected JSON object for struct '%s'"), *Property->GetName(), *Struct->GetName());
			return false;
		}
		TArray<FString> StructErrors;
		if (!DeserializeStructProperties(Struct, ValuePtr, *ObjPtr, StructErrors))
		{
			OutError = FString::Printf(TEXT("Property '%s': struct errors: %s"), *Property->GetName(), *FString::Join(StructErrors, TEXT("; ")));
			return false;
		}
		return true;
	}

	// 9. Object reference (UObject*, TObjectPtr, TWeakObjectPtr via FObjectPropertyBase)
	if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		if (JsonValue->IsNull())
		{
			ObjProp->SetObjectPropertyValue(ValuePtr, nullptr);
			return true;
		}

		// Check if this is an instanced/inline subobject (e.g. UPROPERTY(Instanced))
		const bool bIsInstanced = Property->HasAnyPropertyFlags(CPF_PersistentInstance | CPF_InstancedReference);

		// If JSON is an object → instanced subobject creation or inline property update
		const TSharedPtr<FJsonObject>* InlineObjPtr = nullptr;
		if (JsonValue->TryGetObject(InlineObjPtr) && InlineObjPtr && InlineObjPtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& InlineObj = *InlineObjPtr;

			// Must have _class to know what to instantiate
			if (!InlineObj->HasField(TEXT("_class")))
			{
				OutError = FString::Printf(TEXT("Property '%s': JSON object for instanced subobject missing '_class' field"), *Property->GetName());
				return false;
			}

			const FString ClassName = InlineObj->GetStringField(TEXT("_class"));

			// Resolve the UClass
			UClass* SubObjClass = FindObject<UClass>(nullptr, *ClassName);
			if (!SubObjClass)
			{
				SubObjClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
			}
			if (!SubObjClass)
			{
				// Try loading as an asset path (e.g. "/Script/PRK.PRKAthenaTask_HealSelf")
				SubObjClass = LoadObject<UClass>(nullptr, *ClassName);
			}
			if (!SubObjClass)
			{
				OutError = FString::Printf(TEXT("Property '%s': could not find class '%s'"), *Property->GetName(), *ClassName);
				return false;
			}

			// Verify the class is compatible with the property's expected class
			if (ObjProp->PropertyClass && !SubObjClass->IsChildOf(ObjProp->PropertyClass))
			{
				OutError = FString::Printf(TEXT("Property '%s': class '%s' is not a subclass of '%s'"),
					*Property->GetName(), *ClassName, *ObjProp->PropertyClass->GetName());
				return false;
			}

			// Create the new subobject
			UObject* EffectiveOuter = Outer ? Outer : GetTransientPackage();
			UObject* NewSubObj = NewObject<UObject>(EffectiveOuter, SubObjClass);
			if (!NewSubObj)
			{
				OutError = FString::Printf(TEXT("Property '%s': NewObject failed for class '%s'"), *Property->GetName(), *ClassName);
				return false;
			}

			// Recursively deserialize properties onto the new object
			TArray<FString> SubErrors;
			DeserializeObjectProperties(NewSubObj, InlineObj, SubErrors);
			if (SubErrors.Num() > 0)
			{
				OutError = FString::Printf(TEXT("Property '%s': subobject errors: %s"),
					*Property->GetName(), *FString::Join(SubErrors, TEXT("; ")));
				// Still set the object — partial success is useful
			}

			ObjProp->SetObjectPropertyValue(ValuePtr, NewSubObj);
			return SubErrors.Num() == 0;
		}

		// JSON is a string → load existing object by path (normal reference)
		const FString PathStr = JsonValue->AsString();
		if (PathStr.IsEmpty())
		{
			ObjProp->SetObjectPropertyValue(ValuePtr, nullptr);
			return true;
		}
		UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(PathStr);
		if (!LoadedObj)
		{
			LoadedObj = StaticFindObject(UObject::StaticClass(), nullptr, *PathStr);
		}
		if (!LoadedObj)
		{
			OutError = FString::Printf(TEXT("Property '%s': failed to load object '%s'"), *Property->GetName(), *PathStr);
			return false;
		}
		ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
		return true;
	}

	// 10. Array
	if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
		if (!JsonValue->TryGetArray(ArrPtr) || !ArrPtr)
		{
			OutError = FString::Printf(TEXT("Property '%s': expected JSON array"), *Property->GetName());
			return false;
		}
		FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
		ArrayHelper.Resize(ArrPtr->Num());
		for (int32 i = 0; i < ArrPtr->Num(); ++i)
		{
			FString ElemError;
			if (!DeserializePropertyValue(ArrayProp->Inner, ArrayHelper.GetRawPtr(i), (*ArrPtr)[i], ElemError, Outer))
			{
				OutError = FString::Printf(TEXT("Property '%s[%d]': %s"), *Property->GetName(), i, *ElemError);
				return false;
			}
		}
		return true;
	}

	// 11. Set
	if (const FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
		if (!JsonValue->TryGetArray(ArrPtr) || !ArrPtr)
		{
			OutError = FString::Printf(TEXT("Property '%s': expected JSON array for set"), *Property->GetName());
			return false;
		}
		FScriptSetHelper SetHelper(SetProp, ValuePtr);
		SetHelper.EmptyElements();
		for (int32 i = 0; i < ArrPtr->Num(); ++i)
		{
			const int32 NewIndex = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
			FString ElemError;
			if (!DeserializePropertyValue(SetProp->ElementProp, SetHelper.GetElementPtr(NewIndex), (*ArrPtr)[i], ElemError, Outer))
			{
				OutError = FString::Printf(TEXT("Property '%s' set element %d: %s"), *Property->GetName(), i, *ElemError);
				return false;
			}
		}
		SetHelper.Rehash();
		return true;
	}

	// 12. Map
	if (const FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!JsonValue->TryGetObject(ObjPtr) || !ObjPtr)
		{
			OutError = FString::Printf(TEXT("Property '%s': expected JSON object for map"), *Property->GetName());
			return false;
		}
		FScriptMapHelper MapHelper(MapProp, ValuePtr);
		MapHelper.EmptyValues();
		for (const auto& Pair : (*ObjPtr)->Values)
		{
			const int32 NewIndex = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
			// Deserialize key from the string key
			FString KeyError;
			TSharedPtr<FJsonValue> KeyJsonValue = MakeShared<FJsonValueString>(Pair.Key);
			if (!DeserializePropertyValue(MapProp->KeyProp, MapHelper.GetKeyPtr(NewIndex), KeyJsonValue, KeyError, Outer))
			{
				OutError = FString::Printf(TEXT("Property '%s' map key '%s': %s"), *Property->GetName(), *Pair.Key, *KeyError);
				return false;
			}
			// Deserialize value
			FString ValError;
			if (!DeserializePropertyValue(MapProp->ValueProp, MapHelper.GetValuePtr(NewIndex), Pair.Value, ValError, Outer))
			{
				OutError = FString::Printf(TEXT("Property '%s' map value for key '%s': %s"), *Property->GetName(), *Pair.Key, *ValError);
				return false;
			}
		}
		MapHelper.Rehash();
		return true;
	}

	// 13. Soft object reference (TSoftObjectPtr)
	if (const FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(ValuePtr);
		*SoftPtr = FSoftObjectPath(JsonValue->AsString());
		return true;
	}

	// 14. Soft class reference (TSoftClassPtr)
	if (const FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
	{
		FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(ValuePtr);
		*SoftPtr = FSoftObjectPath(JsonValue->AsString());
		return true;
	}

	// 15. Fallback
	OutError = FString::Printf(TEXT("Property '%s': unsupported type '%s'"), *Property->GetName(), *Property->GetClass()->GetName());
	return false;
}

bool FEpicUnrealMCPPropertyUtils::DeserializeObjectProperties(UObject* Object,
	const TSharedPtr<FJsonObject>& JsonObject, TArray<FString>& OutErrors)
{
	if (!Object || !JsonObject.IsValid())
	{
		OutErrors.Add(TEXT("Null object or JSON"));
		return false;
	}

	for (const auto& Pair : JsonObject->Values)
	{
		const FString& FieldName = Pair.Key;

		// Skip metadata fields
		if (FieldName.StartsWith(TEXT("_")))
		{
			continue;
		}

		// Find the property by name
		FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), *FieldName);
		if (!Property)
		{
			OutErrors.Add(FString::Printf(TEXT("No property named '%s' on class '%s'"), *FieldName, *Object->GetClass()->GetName()));
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		FString Error;
		if (!DeserializePropertyValue(Property, ValuePtr, Pair.Value, Error, /*Outer=*/Object))
		{
			OutErrors.Add(Error);
		}
	}

	return OutErrors.Num() == 0;
}

bool FEpicUnrealMCPPropertyUtils::DeserializeStructProperties(UScriptStruct* StructType, void* StructData,
	const TSharedPtr<FJsonObject>& JsonObject, TArray<FString>& OutErrors)
{
	if (!StructType || !StructData || !JsonObject.IsValid())
	{
		OutErrors.Add(TEXT("Null struct type, data, or JSON"));
		return false;
	}

	for (const auto& Pair : JsonObject->Values)
	{
		const FString& FieldName = Pair.Key;

		FProperty* Property = StructType->FindPropertyByName(*FieldName);
		if (!Property)
		{
			OutErrors.Add(FString::Printf(TEXT("No field named '%s' on struct '%s'"), *FieldName, *StructType->GetName()));
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(StructData);
		FString Error;
		if (!DeserializePropertyValue(Property, ValuePtr, Pair.Value, Error))
		{
			OutErrors.Add(Error);
		}
	}

	return OutErrors.Num() == 0;
}
