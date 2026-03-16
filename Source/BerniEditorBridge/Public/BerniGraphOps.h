// Copyright ReapAndRuin Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BerniTypes.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/**
 * Core Blueprint graph operations: inspect, validate, and apply patches.
 */
class BERNIEDITORBRIDGE_API FBerniGraphOps : public TSharedFromThis<FBerniGraphOps>
{
public:
	FBerniGraphOps();

	// ----- Inspection -----

	/** Build a JSON description of all nodes, pins, and links in a graph. */
	TSharedPtr<FJsonObject> InspectGraph(const FString& AssetPath, const FString& GraphName, FString& OutError);

	/** List all graphs in a Blueprint. */
	TSharedPtr<FJsonObject> ListGraphs(const FString& AssetPath, FString& OutError);

	/** Query functions and properties on a UClass. */
	TSharedPtr<FJsonObject> QueryClass(const FString& ClassName, const FString& Filter, FString& OutError);

	// ----- Patching -----

	/** Validate a set of ops without modifying anything. Returns normalized plan + warnings. */
	TSharedPtr<FJsonObject> ValidatePatch(
		const FString& AssetPath,
		const FString& GraphName,
		const TArray<FBerniPatchOperation>& Ops,
		FString& OutError
	);

	/** Apply ops, compile, and save the Blueprint. Returns result + diagnostics. */
	TSharedPtr<FJsonObject> ApplyPatch(
		const FString& AssetPath,
		const FString& GraphName,
		const TArray<FBerniPatchOperation>& Ops,
		FString& OutError
	);

	// ----- Undo -----

	/** Revert the last apply for a given asset path. Returns true on success. */
	bool UndoLastApply(const FString& AssetPath, FString& OutError);

	/** Get the number of undo levels available for an asset. */
	int32 GetUndoLevels(const FString& AssetPath) const;

private:
	// Blueprint loading
	UBlueprint* LoadBlueprintAsset(const FString& AssetPath, FString& OutError);
	UEdGraph* FindGraphByName(UBlueprint* BP, const FString& GraphName, FString& OutError);

	// Node serialization
	TSharedPtr<FJsonObject> SerializeNode(UEdGraphNode* Node);
	TSharedPtr<FJsonObject> SerializePin(class UEdGraphPin* Pin);

	// Op execution
	bool ExecuteAddNode(UBlueprint* BP, UEdGraph* Graph, const FBerniPatchOperation& Op, TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError);
	bool ExecuteRemoveNode(UEdGraph* Graph, const FBerniPatchOperation& Op, TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError);
	bool ExecuteMoveNode(UEdGraph* Graph, const FBerniPatchOperation& Op, TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError);
	bool ExecuteConnect(UEdGraph* Graph, const FBerniPatchOperation& Op, TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError);
	bool ExecuteDisconnect(UEdGraph* Graph, const FBerniPatchOperation& Op, TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError);
	bool ExecuteSetDefault(UEdGraph* Graph, const FBerniPatchOperation& Op, TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError);
	bool ExecuteSetComment(UEdGraph* Graph, const FBerniPatchOperation& Op, TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError);

	// Pin resolution
	UEdGraphPin* ResolvePin(const FString& PinRef, UEdGraph* Graph, TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError);

	// Backup
	bool BackupAsset(const FString& AssetPath, FString& OutError);
	bool RestoreBackup(const FString& AssetPath, FString& OutError);
	void PruneBackups(const FString& AssetPath);

	// Compile
	bool CompileAndSave(UBlueprint* BP, FString& OutError);

	// Backup storage: AssetPath -> stack of backup file paths (most recent last)
	TMap<FString, TArray<FString>> BackupStacks;
};
