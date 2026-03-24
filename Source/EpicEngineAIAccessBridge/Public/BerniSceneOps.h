// Copyright ReapAndRuin Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class AActor;

/**
 * Scene/level actor operations: list, find, spawn, delete, transform, properties.
 */
class EPICENGINEAIACCESSBRIDGE_API FBerniSceneOps : public TSharedFromThis<FBerniSceneOps>
{
public:
	FBerniSceneOps();

	/** List all actors in the current editor world. Optional class filter. */
	TSharedPtr<FJsonObject> ListActors(const FString& ClassFilter, FString& OutError);

	/** Find actors by name pattern (substring match). */
	TSharedPtr<FJsonObject> FindActors(const FString& NamePattern, const FString& ClassFilter, FString& OutError);

	/** Spawn an actor by class name or asset path. */
	TSharedPtr<FJsonObject> SpawnActor(
		const FString& ClassName,
		const FString& AssetPath,
		const FString& ActorLabel,
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& Scale,
		FString& OutError
	);

	/** Delete an actor by label or name. */
	TSharedPtr<FJsonObject> DeleteActor(const FString& ActorName, FString& OutError);

	/** Get or set an actor's transform. */
	TSharedPtr<FJsonObject> GetTransform(const FString& ActorName, FString& OutError);
	TSharedPtr<FJsonObject> SetTransform(
		const FString& ActorName,
		const TSharedPtr<FJsonObject>& LocationObj,
		const TSharedPtr<FJsonObject>& RotationObj,
		const TSharedPtr<FJsonObject>& ScaleObj,
		FString& OutError
	);

	/** Get properties of an actor. */
	TSharedPtr<FJsonObject> GetProperties(const FString& ActorName, FString& OutError);

	/** Set a property on an actor by name. */
	TSharedPtr<FJsonObject> SetProperty(const FString& ActorName, const FString& PropertyName, const FString& Value, FString& OutError);

	/** Execute a Python script and return output. */
	TSharedPtr<FJsonObject> ExecutePython(const FString& Code, FString& OutError);

	/** Create a new Blueprint asset from a parent class at the given content path. */
	TSharedPtr<FJsonObject> CreateBlueprint(const FString& ParentClass, const FString& AssetPath, const FString& AssetName, FString& OutError);

private:
	UWorld* GetEditorWorld(FString& OutError);
	AActor* FindActorByLabel(UWorld* World, const FString& ActorName, FString& OutError);
	TSharedPtr<FJsonObject> SerializeActor(AActor* Actor);
	TSharedPtr<FJsonObject> SerializeTransform(const FTransform& Transform);
};
