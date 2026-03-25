#include "Commands/EpicUnrealMCPPropertyUtils.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "GameplayTagContainer.h"

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
