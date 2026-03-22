// Copyright ReapAndRuin Dev. All Rights Reserved.

#include "BerniSceneOps.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/PropertyAccessUtil.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Engine/Blueprint.h"

FBerniSceneOps::FBerniSceneOps()
{
}

// ===========================================================================
// List actors
// ===========================================================================

TSharedPtr<FJsonObject> FBerniSceneOps::ListActors(const FString& ClassFilter, FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return nullptr;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ActorsArray;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		// Apply class filter if specified
		if (!ClassFilter.IsEmpty())
		{
			FString ActorClassName = Actor->GetClass()->GetName();
			if (!ActorClassName.Contains(ClassFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		ActorsArray.Add(MakeShared<FJsonValueObject>(SerializeActor(Actor)));
	}

	Result->SetArrayField(TEXT("actors"), ActorsArray);
	Result->SetNumberField(TEXT("count"), ActorsArray.Num());

	return Result;
}

// ===========================================================================
// Find actors
// ===========================================================================

TSharedPtr<FJsonObject> FBerniSceneOps::FindActors(const FString& NamePattern, const FString& ClassFilter, FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return nullptr;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ActorsArray;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		// Name pattern match
		FString Label = Actor->GetActorNameOrLabel();
		FString Name = Actor->GetName();
		if (!NamePattern.IsEmpty())
		{
			if (!Label.Contains(NamePattern, ESearchCase::IgnoreCase) &&
				!Name.Contains(NamePattern, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		// Class filter
		if (!ClassFilter.IsEmpty())
		{
			FString ActorClassName = Actor->GetClass()->GetName();
			if (!ActorClassName.Contains(ClassFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		ActorsArray.Add(MakeShared<FJsonValueObject>(SerializeActor(Actor)));
	}

	Result->SetArrayField(TEXT("actors"), ActorsArray);
	Result->SetNumberField(TEXT("count"), ActorsArray.Num());

	return Result;
}

// ===========================================================================
// Spawn actor
// ===========================================================================

TSharedPtr<FJsonObject> FBerniSceneOps::SpawnActor(
	const FString& ClassName,
	const FString& AssetPath,
	const FString& ActorLabel,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Scale,
	FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return nullptr;

	AActor* NewActor = nullptr;

	// If an assetPath is provided, try to spawn from a Blueprint asset
	if (!AssetPath.IsEmpty())
	{
		FString ObjectPath = AssetPath;
		FString AssetName = FPackageName::GetShortName(AssetPath);
		if (!ObjectPath.Contains(TEXT(".")))
		{
			ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
		}

		UObject* Loaded = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ObjectPath);
		if (!Loaded)
		{
			Loaded = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath);
		}

		UBlueprint* BP = Cast<UBlueprint>(Loaded);
		if (BP && BP->GeneratedClass)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			NewActor = World->SpawnActor<AActor>(BP->GeneratedClass, Location, Rotation, SpawnParams);
		}
		else
		{
			OutError = FString::Printf(TEXT("Failed to load Blueprint at '%s'."), *AssetPath);
			return nullptr;
		}
	}
	// Otherwise, spawn by class name
	else if (!ClassName.IsEmpty())
	{
		UClass* ActorClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
		if (!ActorClass)
		{
			ActorClass = FindFirstObject<UClass>(*(TEXT("A") + ClassName), EFindFirstObjectOptions::None);
		}
		if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
		{
			OutError = FString::Printf(TEXT("Actor class '%s' not found."), *ClassName);
			return nullptr;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		NewActor = World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
	}
	else
	{
		OutError = TEXT("Must provide either 'className' or 'assetPath'.");
		return nullptr;
	}

	if (!NewActor)
	{
		OutError = TEXT("Failed to spawn actor.");
		return nullptr;
	}

	// Apply scale
	if (!Scale.IsNearlyZero())
	{
		NewActor->SetActorScale3D(Scale);
	}

	// Apply label
	if (!ActorLabel.IsEmpty())
	{
		NewActor->SetActorLabel(ActorLabel);
	}

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Spawned actor '%s' (%s) at (%s)"),
		*NewActor->GetActorNameOrLabel(), *NewActor->GetClass()->GetName(), *Location.ToString());

	return SerializeActor(NewActor);
}

// ===========================================================================
// Delete actor
// ===========================================================================

TSharedPtr<FJsonObject> FBerniSceneOps::DeleteActor(const FString& ActorName, FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return nullptr;

	AActor* Actor = FindActorByLabel(World, ActorName, OutError);
	if (!Actor) return nullptr;

	FString Label = Actor->GetActorNameOrLabel();
	FString Class = Actor->GetClass()->GetName();

	bool bDestroyed = World->DestroyActor(Actor);
	if (!bDestroyed)
	{
		OutError = FString::Printf(TEXT("Failed to destroy actor '%s'."), *ActorName);
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("deleted"), Label);
	Result->SetStringField(TEXT("class"), Class);

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Deleted actor '%s'"), *Label);

	return Result;
}

// ===========================================================================
// Transform
// ===========================================================================

TSharedPtr<FJsonObject> FBerniSceneOps::GetTransform(const FString& ActorName, FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return nullptr;

	AActor* Actor = FindActorByLabel(World, ActorName, OutError);
	if (!Actor) return nullptr;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actor"), Actor->GetActorNameOrLabel());
	Result->SetObjectField(TEXT("transform"), SerializeTransform(Actor->GetActorTransform()));

	return Result;
}

TSharedPtr<FJsonObject> FBerniSceneOps::SetTransform(
	const FString& ActorName,
	const TSharedPtr<FJsonObject>& LocationObj,
	const TSharedPtr<FJsonObject>& RotationObj,
	const TSharedPtr<FJsonObject>& ScaleObj,
	FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return nullptr;

	AActor* Actor = FindActorByLabel(World, ActorName, OutError);
	if (!Actor) return nullptr;

	if (LocationObj)
	{
		double X = 0, Y = 0, Z = 0;
		LocationObj->TryGetNumberField(TEXT("x"), X);
		LocationObj->TryGetNumberField(TEXT("y"), Y);
		LocationObj->TryGetNumberField(TEXT("z"), Z);
		Actor->SetActorLocation(FVector(X, Y, Z));
	}

	if (RotationObj)
	{
		double Pitch = 0, Yaw = 0, Roll = 0;
		RotationObj->TryGetNumberField(TEXT("pitch"), Pitch);
		RotationObj->TryGetNumberField(TEXT("yaw"), Yaw);
		RotationObj->TryGetNumberField(TEXT("roll"), Roll);
		Actor->SetActorRotation(FRotator(Pitch, Yaw, Roll));
	}

	if (ScaleObj)
	{
		double X = 1, Y = 1, Z = 1;
		ScaleObj->TryGetNumberField(TEXT("x"), X);
		ScaleObj->TryGetNumberField(TEXT("y"), Y);
		ScaleObj->TryGetNumberField(TEXT("z"), Z);
		Actor->SetActorScale3D(FVector(X, Y, Z));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetActorNameOrLabel());
	Result->SetObjectField(TEXT("transform"), SerializeTransform(Actor->GetActorTransform()));

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Set transform on '%s'"), *Actor->GetActorNameOrLabel());

	return Result;
}

// ===========================================================================
// Properties
// ===========================================================================

TSharedPtr<FJsonObject> FBerniSceneOps::GetProperties(const FString& ActorName, FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return nullptr;

	AActor* Actor = FindActorByLabel(World, ActorName, OutError);
	if (!Actor) return nullptr;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actor"), Actor->GetActorNameOrLabel());
	Result->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

	TArray<TSharedPtr<FJsonValue>> PropsArray;
	for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;
		if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) continue;

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Prop->GetName());
		PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());

		// Try to get value as string
		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
		if (ValuePtr)
		{
			Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Actor, PPF_None);
		}
		PropObj->SetStringField(TEXT("value"), ValueStr);

		PropsArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	Result->SetArrayField(TEXT("properties"), PropsArray);

	return Result;
}

TSharedPtr<FJsonObject> FBerniSceneOps::SetProperty(const FString& ActorName, const FString& PropertyName, const FString& Value, FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return nullptr;

	AActor* Actor = FindActorByLabel(World, ActorName, OutError);
	if (!Actor) return nullptr;

	FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on actor '%s'."), *PropertyName, *ActorName);
		return nullptr;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
	if (!ValuePtr)
	{
		OutError = FString::Printf(TEXT("Cannot access property '%s' on actor '%s'."), *PropertyName, *ActorName);
		return nullptr;
	}

	// Try to import the value from string
	const TCHAR* ImportResult = Prop->ImportText_Direct(*Value, ValuePtr, Actor, PPF_None);
	if (!ImportResult)
	{
		OutError = FString::Printf(TEXT("Failed to set property '%s' to '%s'."), *PropertyName, *Value);
		return nullptr;
	}

	// Notify the actor that it was modified
	Actor->PostEditChange();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetActorNameOrLabel());
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetStringField(TEXT("value"), Value);

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Set %s.%s = '%s'"), *ActorName, *PropertyName, *Value);

	return Result;
}

// ===========================================================================
// Python execution
// ===========================================================================

TSharedPtr<FJsonObject> FBerniSceneOps::ExecutePython(const FString& Code, FString& OutError)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Check if Python plugin is loaded
	IModuleInterface* PythonModule = FModuleManager::Get().GetModule(TEXT("PythonScriptPlugin"));
	if (!PythonModule)
	{
		OutError = TEXT("PythonScriptPlugin is not loaded. Enable the Python Editor Script Plugin in Edit > Plugins.");
		return nullptr;
	}

	// Capture log output during Python execution
	FString CapturedOutput;
	TArray<FString> LogLines;

	// Add output device to capture Python print statements
	class FBerniOutputDevice : public FOutputDevice
	{
	public:
		TArray<FString>& Lines;
		FBerniOutputDevice(TArray<FString>& InLines) : Lines(InLines) {}
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == FName(TEXT("LogPython")) || Category == FName(TEXT("Python")))
			{
				Lines.Add(V);
			}
		}
	};

	FBerniOutputDevice Capture(LogLines);
	GLog->AddOutputDevice(&Capture);

	// Execute via console command - this routes through UE's Python integration
	bool bSuccess = GEngine->Exec(
		GEditor ? GEditor->GetEditorWorldContext().World() : nullptr,
		*FString::Printf(TEXT("py %s"), *Code)
	);

	GLog->RemoveOutputDevice(&Capture);

	// Build output string
	for (const FString& Line : LogLines)
	{
		if (!CapturedOutput.IsEmpty()) CapturedOutput += TEXT("\n");
		CapturedOutput += Line;
	}

	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("output"), CapturedOutput);
	Result->SetNumberField(TEXT("lineCount"), LogLines.Num());

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Executed Python (%d lines of output, success=%d)"),
		LogLines.Num(), bSuccess ? 1 : 0);

	return Result;
}

// ===========================================================================
// Private helpers
// ===========================================================================

UWorld* FBerniSceneOps::GetEditorWorld(FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor is null.");
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		OutError = TEXT("No editor world available.");
		return nullptr;
	}

	return World;
}

AActor* FBerniSceneOps::FindActorByLabel(UWorld* World, const FString& ActorName, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("Null world.");
		return nullptr;
	}

	// Try exact label match first
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		if (Actor->GetActorNameOrLabel() == ActorName || Actor->GetName() == ActorName)
		{
			return Actor;
		}
	}

	// Try case-insensitive / substring match
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		if (Actor->GetActorNameOrLabel().Contains(ActorName, ESearchCase::IgnoreCase) ||
			Actor->GetActorLabel().Contains(ActorName, ESearchCase::IgnoreCase))
		{
			return Actor;
		}
	}

	OutError = FString::Printf(TEXT("Actor '%s' not found in the level."), *ActorName);
	return nullptr;
}

TSharedPtr<FJsonObject> FBerniSceneOps::SerializeActor(AActor* Actor)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Actor) return Obj;

	Obj->SetStringField(TEXT("name"), Actor->GetName());
	Obj->SetStringField(TEXT("label"), Actor->GetActorNameOrLabel());
	Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	Obj->SetBoolField(TEXT("isHidden"), Actor->IsHidden());
	Obj->SetObjectField(TEXT("transform"), SerializeTransform(Actor->GetActorTransform()));

	// List components
	TArray<TSharedPtr<FJsonValue>> CompsArray;
	TInlineComponentArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;
		TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
		CompObj->SetStringField(TEXT("name"), Comp->GetName());
		CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		CompsArray.Add(MakeShared<FJsonValueObject>(CompObj));
	}
	Obj->SetArrayField(TEXT("components"), CompsArray);

	return Obj;
}

TSharedPtr<FJsonObject> FBerniSceneOps::SerializeTransform(const FTransform& Transform)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
	Loc->SetNumberField(TEXT("x"), Transform.GetLocation().X);
	Loc->SetNumberField(TEXT("y"), Transform.GetLocation().Y);
	Loc->SetNumberField(TEXT("z"), Transform.GetLocation().Z);
	Obj->SetObjectField(TEXT("location"), Loc);

	TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
	FRotator Rotator = Transform.GetRotation().Rotator();
	Rot->SetNumberField(TEXT("pitch"), Rotator.Pitch);
	Rot->SetNumberField(TEXT("yaw"), Rotator.Yaw);
	Rot->SetNumberField(TEXT("roll"), Rotator.Roll);
	Obj->SetObjectField(TEXT("rotation"), Rot);

	TSharedPtr<FJsonObject> Scl = MakeShared<FJsonObject>();
	Scl->SetNumberField(TEXT("x"), Transform.GetScale3D().X);
	Scl->SetNumberField(TEXT("y"), Transform.GetScale3D().Y);
	Scl->SetNumberField(TEXT("z"), Transform.GetScale3D().Z);
	Obj->SetObjectField(TEXT("scale"), Scl);

	return Obj;
}
