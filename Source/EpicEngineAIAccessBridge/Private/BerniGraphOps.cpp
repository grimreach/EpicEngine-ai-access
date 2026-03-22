// Copyright ReapAndRuin Dev. All Rights Reserved.

#include "BerniGraphOps.h"
#include "BerniTypes.h"

#include "Engine/Blueprint.h"
#include "UObject/SavePackage.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Knot.h"
#include "K2Node_MakeArray.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet/KismetSystemLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"

FBerniGraphOps::FBerniGraphOps()
{
}

// ===========================================================================
// Inspection
// ===========================================================================

TSharedPtr<FJsonObject> FBerniGraphOps::InspectGraph(const FString& AssetPath, const FString& GraphName, FString& OutError)
{
	UBlueprint* BP = LoadBlueprintAsset(AssetPath, OutError);
	if (!BP) return nullptr;

	UEdGraph* Graph = FindGraphByName(BP, GraphName, OutError);
	if (!Graph) return nullptr;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("graph"), Graph->GetName());
	Result->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		NodesArray.Add(MakeShared<FJsonValueObject>(SerializeNode(Node)));
	}
	Result->SetArrayField(TEXT("nodes"), NodesArray);

	// Build links array
	TArray<TSharedPtr<FJsonValue>> LinksArray;
	TSet<FString> SeenLinks;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;

				FString LinkId = FString::Printf(TEXT("%s.%s->%s.%s"),
					*Node->GetName(), *Pin->GetName(),
					*LinkedPin->GetOwningNode()->GetName(), *LinkedPin->GetName());

				if (SeenLinks.Contains(LinkId)) continue;
				SeenLinks.Add(LinkId);

				TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
				LinkObj->SetStringField(TEXT("fromNode"), Node->GetName());
				LinkObj->SetStringField(TEXT("fromPin"), Pin->GetName());
				LinkObj->SetStringField(TEXT("toNode"), LinkedPin->GetOwningNode()->GetName());
				LinkObj->SetStringField(TEXT("toPin"), LinkedPin->GetName());
				LinksArray.Add(MakeShared<FJsonValueObject>(LinkObj));
			}
		}
	}
	Result->SetArrayField(TEXT("links"), LinksArray);

	return Result;
}

// ===========================================================================
// List graphs
// ===========================================================================

TSharedPtr<FJsonObject> FBerniGraphOps::ListGraphs(const FString& AssetPath, FString& OutError)
{
	UBlueprint* BP = LoadBlueprintAsset(AssetPath, OutError);
	if (!BP) return nullptr;

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), AssetPath);

	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("class"), Graph->GetClass()->GetName());
		GraphObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

		bool bIsFunction = BP->FunctionGraphs.Contains(Graph);
		bool bIsMacro = BP->MacroGraphs.Contains(Graph);
		bool bIsUber = BP->UbergraphPages.Contains(Graph);
		FString Category = bIsFunction ? TEXT("function") : bIsMacro ? TEXT("macro") : bIsUber ? TEXT("eventGraph") : TEXT("other");
		GraphObj->SetStringField(TEXT("category"), Category);

		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}
	Result->SetArrayField(TEXT("graphs"), GraphsArray);
	Result->SetNumberField(TEXT("count"), GraphsArray.Num());

	return Result;
}

// ===========================================================================
// Query class
// ===========================================================================

TSharedPtr<FJsonObject> FBerniGraphOps::QueryClass(const FString& ClassName, const FString& Filter, FString& OutError)
{
	UClass* Class = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
	if (!Class)
	{
		// Try with common prefixes
		Class = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::None);
		if (!Class)
		{
			Class = FindFirstObject<UClass>(*(TEXT("A") + ClassName), EFindFirstObjectOptions::None);
		}
	}
	if (!Class)
	{
		OutError = FString::Printf(TEXT("Class '%s' not found."), *ClassName);
		return nullptr;
	}

	bool bOnlyCallable = Filter.Equals(TEXT("blueprintCallable"), ESearchCase::IgnoreCase);
	bool bOnlyVisible = Filter.Equals(TEXT("blueprintVisible"), ESearchCase::IgnoreCase);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("class"), Class->GetName());
	Result->SetStringField(TEXT("parent"), Class->GetSuperClass() ? Class->GetSuperClass()->GetName() : TEXT("None"));

	// Functions
	TArray<TSharedPtr<FJsonValue>> FuncsArray;
	for (TFieldIterator<UFunction> It(Class); It; ++It)
	{
		UFunction* Func = *It;
		if (!Func) continue;

		bool bCallable = Func->HasAnyFunctionFlags(FUNC_BlueprintCallable);
		if (bOnlyCallable && !bCallable) continue;

		TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Func->GetName());
		FuncObj->SetStringField(TEXT("owner"), Func->GetOwnerClass()->GetName());
		FuncObj->SetBoolField(TEXT("isStatic"), Func->HasAnyFunctionFlags(FUNC_Static));
		FuncObj->SetBoolField(TEXT("isBlueprintCallable"), bCallable);
		FuncObj->SetBoolField(TEXT("isPure"), Func->HasAnyFunctionFlags(FUNC_BlueprintPure));

		// Parameters
		TArray<TSharedPtr<FJsonValue>> ParamsArr;
		for (TFieldIterator<FProperty> PIt(Func); PIt; ++PIt)
		{
			FProperty* Param = *PIt;
			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), Param->GetName());
			ParamObj->SetStringField(TEXT("type"), Param->GetCPPType());
			ParamObj->SetBoolField(TEXT("isReturn"), Param->HasAnyPropertyFlags(CPF_ReturnParm));
			ParamObj->SetBoolField(TEXT("isOutput"), Param->HasAnyPropertyFlags(CPF_OutParm));
			ParamsArr.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
		FuncObj->SetArrayField(TEXT("parameters"), ParamsArr);
		FuncsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
	}
	Result->SetArrayField(TEXT("functions"), FuncsArray);

	// Properties
	TArray<TSharedPtr<FJsonValue>> PropsArray;
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;

		bool bVisible = Prop->HasAnyPropertyFlags(CPF_BlueprintVisible);
		if (bOnlyVisible && !bVisible) continue;

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Prop->GetName());
		PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());
		PropObj->SetStringField(TEXT("owner"), Prop->GetOwnerClass()->GetName());
		PropObj->SetBoolField(TEXT("isBlueprintVisible"), bVisible);
		PropObj->SetBoolField(TEXT("isBlueprintReadOnly"), Prop->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
		PropsArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}
	Result->SetArrayField(TEXT("properties"), PropsArray);

	return Result;
}

// ===========================================================================
// Patch validation (dry-run)
// ===========================================================================

TSharedPtr<FJsonObject> FBerniGraphOps::ValidatePatch(
	const FString& AssetPath,
	const FString& GraphName,
	const TArray<FBerniPatchOperation>& Ops,
	FString& OutError)
{
	UBlueprint* BP = LoadBlueprintAsset(AssetPath, OutError);
	if (!BP) return nullptr;

	UEdGraph* Graph = FindGraphByName(BP, GraphName, OutError);
	if (!Graph) return nullptr;

	// Build node map from existing nodes
	TMap<FString, UEdGraphNode*> NodeMap;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			NodeMap.Add(Node->GetName(), Node);
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("graph"), GraphName);
	Result->SetBoolField(TEXT("dryRun"), true);

	TArray<TSharedPtr<FJsonValue>> OpResults;
	int32 ErrorCount = 0;
	int32 WarningCount = 0;

	for (int32 i = 0; i < Ops.Num(); i++)
	{
		const FBerniPatchOperation& Op = Ops[i];
		TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
		OpResult->SetNumberField(TEXT("index"), i);
		OpResult->SetStringField(TEXT("op"), Op.RawOp);

		if (Op.Op == EBerniPatchOp::Unknown)
		{
			OpResult->SetStringField(TEXT("status"), TEXT("error"));
			OpResult->SetStringField(TEXT("message"), FString::Printf(TEXT("Unknown op: '%s'"), *Op.RawOp));
			ErrorCount++;
		}
		else if (Op.Op == EBerniPatchOp::AddNode)
		{
			if (!IsNodeClassAllowed(Op.NodeClass))
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Node class '%s' is not in the allowlist."), *Op.NodeClass));
				ErrorCount++;
			}
			else if (Op.NodeId.IsEmpty())
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"), TEXT("Missing 'id' for addNode op."));
				ErrorCount++;
			}
			else if (NodeMap.Contains(Op.NodeId))
			{
				OpResult->SetStringField(TEXT("status"), TEXT("warning"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Node id '%s' already exists in graph. Will generate unique name."), *Op.NodeId));
				WarningCount++;
				NodeMap.Add(Op.NodeId + TEXT("_new"), nullptr);
			}
			else
			{
				OpResult->SetStringField(TEXT("status"), TEXT("ok"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Would add %s as '%s' at (%d, %d)"), *Op.NodeClass, *Op.NodeId, Op.X, Op.Y));
				NodeMap.Add(Op.NodeId, nullptr); // placeholder for subsequent reference
			}
		}
		else if (Op.Op == EBerniPatchOp::RemoveNode)
		{
			if (Op.TargetNodeId.IsEmpty())
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"), TEXT("Missing 'targetNode' for removeNode op."));
				ErrorCount++;
			}
			else if (!NodeMap.Contains(Op.TargetNodeId))
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Node '%s' not found."), *Op.TargetNodeId));
				ErrorCount++;
			}
			else
			{
				OpResult->SetStringField(TEXT("status"), TEXT("ok"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Would remove node '%s'"), *Op.TargetNodeId));
				NodeMap.Remove(Op.TargetNodeId);
			}
		}
		else if (Op.Op == EBerniPatchOp::MoveNode)
		{
			if (Op.TargetNodeId.IsEmpty())
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"), TEXT("Missing 'targetNode' for moveNode op."));
				ErrorCount++;
			}
			else if (!NodeMap.Contains(Op.TargetNodeId))
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Node '%s' not found."), *Op.TargetNodeId));
				ErrorCount++;
			}
			else
			{
				OpResult->SetStringField(TEXT("status"), TEXT("ok"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Would move node '%s' to (%d, %d)"), *Op.TargetNodeId, Op.X, Op.Y));
			}
		}
		else if (Op.Op == EBerniPatchOp::ConnectPins || Op.Op == EBerniPatchOp::DisconnectPins)
		{
			if (Op.FromPinRef.IsEmpty() || Op.ToPinRef.IsEmpty())
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"), TEXT("Missing 'from' or 'to' pin reference."));
				ErrorCount++;
			}
			else
			{
				// Validate pin refs have correct format: "nodeId.pinName"
				FString FromNode, FromPin, ToNode, ToPin;
				if (!Op.FromPinRef.Split(TEXT("."), &FromNode, &FromPin) ||
					!Op.ToPinRef.Split(TEXT("."), &ToNode, &ToPin))
				{
					OpResult->SetStringField(TEXT("status"), TEXT("error"));
					OpResult->SetStringField(TEXT("message"), TEXT("Pin ref format must be 'nodeId.pinName'."));
					ErrorCount++;
				}
				else
				{
					bool bFromExists = NodeMap.Contains(FromNode);
					bool bToExists = NodeMap.Contains(ToNode);

					if (!bFromExists || !bToExists)
					{
						OpResult->SetStringField(TEXT("status"), TEXT("error"));
						OpResult->SetStringField(TEXT("message"),
							FString::Printf(TEXT("Unknown node(s): from='%s'(%s) to='%s'(%s)"),
								*FromNode, bFromExists ? TEXT("ok") : TEXT("MISSING"),
								*ToNode, bToExists ? TEXT("ok") : TEXT("MISSING")));
						ErrorCount++;
					}
					else
					{
						OpResult->SetStringField(TEXT("status"), TEXT("ok"));
						OpResult->SetStringField(TEXT("message"),
							FString::Printf(TEXT("Would %s %s -> %s"),
								Op.Op == EBerniPatchOp::ConnectPins ? TEXT("connect") : TEXT("disconnect"),
								*Op.FromPinRef, *Op.ToPinRef));
					}
				}
			}
		}
		else if (Op.Op == EBerniPatchOp::SetDefault)
		{
			if (Op.TargetNodeId.IsEmpty() || Op.PinName.IsEmpty())
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"), TEXT("Missing 'targetNode' or 'pin' for setDefault."));
				ErrorCount++;
			}
			else if (!NodeMap.Contains(Op.TargetNodeId))
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Unknown node: '%s'"), *Op.TargetNodeId));
				ErrorCount++;
			}
			else
			{
				OpResult->SetStringField(TEXT("status"), TEXT("ok"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Would set %s.%s = '%s'"), *Op.TargetNodeId, *Op.PinName, *Op.Value));
			}
		}
		else if (Op.Op == EBerniPatchOp::SetComment)
		{
			if (Op.TargetNodeId.IsEmpty())
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"), TEXT("Missing 'targetNode' for setComment."));
				ErrorCount++;
			}
			else if (!NodeMap.Contains(Op.TargetNodeId))
			{
				OpResult->SetStringField(TEXT("status"), TEXT("error"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Unknown node: '%s'"), *Op.TargetNodeId));
				ErrorCount++;
			}
			else
			{
				OpResult->SetStringField(TEXT("status"), TEXT("ok"));
				OpResult->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Would set comment on '%s'"), *Op.TargetNodeId));
			}
		}

		OpResults.Add(MakeShared<FJsonValueObject>(OpResult));
	}

	Result->SetArrayField(TEXT("ops"), OpResults);
	Result->SetNumberField(TEXT("totalOps"), Ops.Num());
	Result->SetNumberField(TEXT("errors"), ErrorCount);
	Result->SetNumberField(TEXT("warnings"), WarningCount);
	Result->SetBoolField(TEXT("canApply"), ErrorCount == 0);

	return Result;
}

// ===========================================================================
// Apply patch
// ===========================================================================

TSharedPtr<FJsonObject> FBerniGraphOps::ApplyPatch(
	const FString& AssetPath,
	const FString& GraphName,
	const TArray<FBerniPatchOperation>& Ops,
	FString& OutError)
{
	UBlueprint* BP = LoadBlueprintAsset(AssetPath, OutError);
	if (!BP) return nullptr;

	UEdGraph* Graph = FindGraphByName(BP, GraphName, OutError);
	if (!Graph) return nullptr;

	// Backup first
	FString BackupError;
	if (!BackupAsset(AssetPath, BackupError))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EpicEngineAIAccessBridge] Backup failed: %s (continuing anyway)"), *BackupError);
	}

	// Start a UE transaction for atomic rollback
	bool bUseTransaction = GEditor != nullptr;
	if (bUseTransaction)
	{
		GEditor->BeginTransaction(FText::FromString(TEXT("EpicEngineAIAccessBridge Patch")));
		Graph->Modify();
	}

	// Build node map
	TMap<FString, UEdGraphNode*> NodeMap;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			NodeMap.Add(Node->GetName(), Node);
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("graph"), GraphName);
	Result->SetBoolField(TEXT("dryRun"), false);

	TArray<TSharedPtr<FJsonValue>> OpResults;
	int32 SuccessCount = 0;
	int32 FailCount = 0;
	bool bRolledBack = false;

	for (int32 i = 0; i < Ops.Num(); i++)
	{
		const FBerniPatchOperation& Op = Ops[i];
		TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
		OpResult->SetNumberField(TEXT("index"), i);
		OpResult->SetStringField(TEXT("op"), Op.RawOp);

		FString OpError;
		bool bSuccess = false;

		switch (Op.Op)
		{
		case EBerniPatchOp::AddNode:
			bSuccess = ExecuteAddNode(BP, Graph, Op, NodeMap, OpError);
			break;
		case EBerniPatchOp::RemoveNode:
			bSuccess = ExecuteRemoveNode(Graph, Op, NodeMap, OpError);
			break;
		case EBerniPatchOp::MoveNode:
			bSuccess = ExecuteMoveNode(Graph, Op, NodeMap, OpError);
			break;
		case EBerniPatchOp::ConnectPins:
			bSuccess = ExecuteConnect(Graph, Op, NodeMap, OpError);
			break;
		case EBerniPatchOp::DisconnectPins:
			bSuccess = ExecuteDisconnect(Graph, Op, NodeMap, OpError);
			break;
		case EBerniPatchOp::SetDefault:
			bSuccess = ExecuteSetDefault(Graph, Op, NodeMap, OpError);
			break;
		case EBerniPatchOp::SetComment:
			bSuccess = ExecuteSetComment(Graph, Op, NodeMap, OpError);
			break;
		default:
			OpError = FString::Printf(TEXT("Unknown op: '%s'"), *Op.RawOp);
			break;
		}

		if (bSuccess)
		{
			OpResult->SetStringField(TEXT("status"), TEXT("ok"));
			SuccessCount++;
		}
		else
		{
			OpResult->SetStringField(TEXT("status"), TEXT("error"));
			OpResult->SetStringField(TEXT("message"), OpError);
			FailCount++;

			OpResults.Add(MakeShared<FJsonValueObject>(OpResult));

			// Atomic rollback: cancel the transaction and stop processing
			if (bUseTransaction)
			{
				GEditor->CancelTransaction(0);
				bRolledBack = true;
			}

			UE_LOG(LogTemp, Warning, TEXT("[EpicEngineAIAccessBridge] Op %d failed, rolled back all changes: %s"), i, *OpError);
			break;
		}

		OpResults.Add(MakeShared<FJsonValueObject>(OpResult));
	}

	// Compile and save (only if no rollback)
	bool bCompiled = false;
	FString CompileError;

	if (!bRolledBack)
	{
		if (bUseTransaction)
		{
			GEditor->EndTransaction();
		}

		bCompiled = CompileAndSave(BP, CompileError);
	}

	Result->SetArrayField(TEXT("ops"), OpResults);
	Result->SetNumberField(TEXT("succeeded"), SuccessCount);
	Result->SetNumberField(TEXT("failed"), FailCount);
	Result->SetBoolField(TEXT("rolledBack"), bRolledBack);
	Result->SetBoolField(TEXT("compiled"), bCompiled);
	if (!bCompiled && !bRolledBack)
	{
		Result->SetStringField(TEXT("compileError"), CompileError);
	}
	Result->SetBoolField(TEXT("success"), FailCount == 0 && bCompiled);

	return Result;
}

// ===========================================================================
// Undo
// ===========================================================================

bool FBerniGraphOps::UndoLastApply(const FString& AssetPath, FString& OutError)
{
	return RestoreBackup(AssetPath, OutError);
}

int32 FBerniGraphOps::GetUndoLevels(const FString& AssetPath) const
{
	const TArray<FString>* Stack = BackupStacks.Find(AssetPath);
	return Stack ? Stack->Num() : 0;
}

// ===========================================================================
// Private: Blueprint loading
// ===========================================================================

UBlueprint* FBerniGraphOps::LoadBlueprintAsset(const FString& AssetPath, FString& OutError)
{
	// Build the full object path
	FString ObjectPath = AssetPath;
	FString AssetName = FPackageName::GetShortName(AssetPath);
	if (!ObjectPath.Contains(TEXT(".")))
	{
		ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
	}

	UObject* Loaded = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ObjectPath);
	if (!Loaded)
	{
		// Try without the object name suffix
		Loaded = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath);
	}

	UBlueprint* BP = Cast<UBlueprint>(Loaded);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Failed to load Blueprint at '%s'."), *AssetPath);
	}
	return BP;
}

UEdGraph* FBerniGraphOps::FindGraphByName(UBlueprint* BP, const FString& GraphName, FString& OutError)
{
	if (!BP)
	{
		OutError = TEXT("Null Blueprint.");
		return nullptr;
	}

	// Normalize: "ConstructionScript" -> "UserConstructionScript"
	FString NormalizedName = GraphName;
	if (NormalizedName == TEXT("ConstructionScript"))
	{
		NormalizedName = TEXT("UserConstructionScript");
	}

	// Search function graphs
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == NormalizedName)
		{
			return Graph;
		}
	}

	// Search all graphs
	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName() == NormalizedName)
		{
			return Graph;
		}
	}

	// Fuzzy match
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName().Contains(GraphName))
		{
			return Graph;
		}
	}

	OutError = FString::Printf(TEXT("Graph '%s' not found in Blueprint. Available graphs: "), *GraphName);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph)
		{
			OutError += Graph->GetName() + TEXT(", ");
		}
	}
	return nullptr;
}

// ===========================================================================
// Private: Serialization
// ===========================================================================

TSharedPtr<FJsonObject> FBerniGraphOps::SerializeNode(UEdGraphNode* Node)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Node) return Obj;

	Obj->SetStringField(TEXT("id"), Node->GetName());
	Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	Obj->SetNumberField(TEXT("x"), Node->NodePosX);
	Obj->SetNumberField(TEXT("y"), Node->NodePosY);
	Obj->SetStringField(TEXT("comment"), Node->NodeComment);

	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin)
		{
			PinsArray.Add(MakeShared<FJsonValueObject>(SerializePin(Pin)));
		}
	}
	Obj->SetArrayField(TEXT("pins"), PinsArray);

	return Obj;
}

TSharedPtr<FJsonObject> FBerniGraphOps::SerializePin(UEdGraphPin* Pin)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Pin) return Obj;

	Obj->SetStringField(TEXT("id"), Pin->GetName());
	Obj->SetStringField(TEXT("displayName"), Pin->GetDisplayName().ToString());
	Obj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
	Obj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());

	if (Pin->PinType.PinSubCategoryObject.IsValid())
	{
		Obj->SetStringField(TEXT("subType"), Pin->PinType.PinSubCategoryObject->GetName());
	}

	Obj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
	Obj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
	Obj->SetNumberField(TEXT("linkCount"), Pin->LinkedTo.Num());

	// Connected-to list
	TArray<TSharedPtr<FJsonValue>> LinksArr;
	for (UEdGraphPin* Linked : Pin->LinkedTo)
	{
		if (Linked && Linked->GetOwningNode())
		{
			TSharedPtr<FJsonObject> LinkRef = MakeShared<FJsonObject>();
			LinkRef->SetStringField(TEXT("node"), Linked->GetOwningNode()->GetName());
			LinkRef->SetStringField(TEXT("pin"), Linked->GetName());
			LinksArr.Add(MakeShared<FJsonValueObject>(LinkRef));
		}
	}
	Obj->SetArrayField(TEXT("linkedTo"), LinksArr);

	return Obj;
}

// ===========================================================================
// Private: Op execution
// ===========================================================================

bool FBerniGraphOps::ExecuteAddNode(UBlueprint* BP, UEdGraph* Graph, const FBerniPatchOperation& Op,
	TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError)
{
	if (!IsNodeClassAllowed(Op.NodeClass))
	{
		OutError = FString::Printf(TEXT("Node class '%s' is not allowlisted."), *Op.NodeClass);
		return false;
	}

	// Find the UClass for the node
	UClass* NodeClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/BlueprintGraph.%s"), *Op.NodeClass));
	if (!NodeClass)
	{
		// Try engine module
		NodeClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *Op.NodeClass));
	}
	if (!NodeClass)
	{
		// Fallback: search all classes
		NodeClass = FindFirstObject<UClass>(*Op.NodeClass, EFindFirstObjectOptions::None);
	}

	if (!NodeClass || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Could not find valid UClass for '%s'."), *Op.NodeClass);
		return false;
	}

	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, NodeClass);
	if (!NewNode)
	{
		OutError = FString::Printf(TEXT("Failed to create node of class '%s'."), *Op.NodeClass);
		return false;
	}

	NewNode->CreateNewGuid();
	NewNode->NodePosX = Op.X;
	NewNode->NodePosY = Op.Y;
	Graph->AddNode(NewNode, false, false);

	// Configure specialized node types BEFORE allocating pins
	bool bNeedsReconstruct = false;

	if (Op.NodeClass == TEXT("K2Node_CallFunction") && !Op.FunctionReference.IsEmpty())
	{
		UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(NewNode);
		if (FuncNode)
		{
			FString ClassName, FuncName;
			if (Op.FunctionReference.Split(TEXT("."), &ClassName, &FuncName))
			{
				// Try finding the class
				UClass* OwnerClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
				if (!OwnerClass)
				{
					OwnerClass = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::None);
				}
				if (!OwnerClass)
				{
					OwnerClass = FindFirstObject<UClass>(*(TEXT("A") + ClassName), EFindFirstObjectOptions::None);
				}

				if (OwnerClass)
				{
					UFunction* Func = OwnerClass->FindFunctionByName(FName(*FuncName));
					if (Func)
					{
						FuncNode->SetFromFunction(Func);
						bNeedsReconstruct = true;
					}
					else
					{
						OutError = FString::Printf(TEXT("Function '%s' not found on class '%s'."), *FuncName, *ClassName);
						Graph->RemoveNode(NewNode);
						return false;
					}
				}
				else
				{
					OutError = FString::Printf(TEXT("Class '%s' not found for functionReference."), *ClassName);
					Graph->RemoveNode(NewNode);
					return false;
				}
			}
			else
			{
				OutError = FString::Printf(TEXT("functionReference must be 'ClassName.FunctionName', got '%s'."), *Op.FunctionReference);
				Graph->RemoveNode(NewNode);
				return false;
			}
		}
	}
	else if ((Op.NodeClass == TEXT("K2Node_VariableGet") || Op.NodeClass == TEXT("K2Node_VariableSet"))
		&& !Op.VariableReference.IsEmpty())
	{
		// Parse variableReference: can be "PropertyName" (looks in BP class) or "ClassName.PropertyName"
		FString ClassName, PropName;
		UClass* SearchClass = nullptr;

		if (Op.VariableReference.Split(TEXT("."), &ClassName, &PropName))
		{
			SearchClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
			if (!SearchClass) SearchClass = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::None);
			if (!SearchClass) SearchClass = FindFirstObject<UClass>(*(TEXT("A") + ClassName), EFindFirstObjectOptions::None);
		}
		else
		{
			PropName = Op.VariableReference;
			// Default to BP's generated class
			if (BP && BP->GeneratedClass)
			{
				SearchClass = BP->GeneratedClass;
			}
		}

		if (!SearchClass)
		{
			OutError = FString::Printf(TEXT("Could not find class for variableReference '%s'."), *Op.VariableReference);
			Graph->RemoveNode(NewNode);
			return false;
		}

		FProperty* Prop = SearchClass->FindPropertyByName(FName(*PropName));
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Property '%s' not found on class '%s'."), *PropName, *SearchClass->GetName());
			Graph->RemoveNode(NewNode);
			return false;
		}

		if (UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(NewNode))
		{
			GetNode->VariableReference.SetFromField<FProperty>(Prop, false);
			bNeedsReconstruct = true;
		}
		else if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(NewNode))
		{
			SetNode->VariableReference.SetFromField<FProperty>(Prop, false);
			bNeedsReconstruct = true;
		}
	}
	else if (Op.NodeClass == TEXT("K2Node_DynamicCast") && !Op.CastClass.IsEmpty())
	{
		UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(NewNode);
		if (CastNode)
		{
			UClass* TargetClass = FindFirstObject<UClass>(*Op.CastClass, EFindFirstObjectOptions::None);
			if (!TargetClass) TargetClass = FindFirstObject<UClass>(*(TEXT("U") + Op.CastClass), EFindFirstObjectOptions::None);
			if (!TargetClass) TargetClass = FindFirstObject<UClass>(*(TEXT("A") + Op.CastClass), EFindFirstObjectOptions::None);

			if (TargetClass)
			{
				CastNode->TargetType = TargetClass;
				bNeedsReconstruct = true;
			}
			else
			{
				OutError = FString::Printf(TEXT("Cast target class '%s' not found."), *Op.CastClass);
				Graph->RemoveNode(NewNode);
				return false;
			}
		}
	}

	if (bNeedsReconstruct)
	{
		NewNode->ReconstructNode();
	}
	else
	{
		NewNode->AllocateDefaultPins();
	}

	// Register in our node map with the user-provided id
	NodeMap.Add(Op.NodeId, NewNode);

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Added node %s ('%s') at (%d,%d)"),
		*Op.NodeClass, *Op.NodeId, Op.X, Op.Y);

	return true;
}

bool FBerniGraphOps::ExecuteRemoveNode(UEdGraph* Graph, const FBerniPatchOperation& Op,
	TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError)
{
	UEdGraphNode** NodePtr = NodeMap.Find(Op.TargetNodeId);
	if (!NodePtr || !*NodePtr)
	{
		OutError = FString::Printf(TEXT("Node '%s' not found."), *Op.TargetNodeId);
		return false;
	}

	UEdGraphNode* Node = *NodePtr;
	Node->BreakAllNodeLinks();
	Graph->RemoveNode(Node);
	NodeMap.Remove(Op.TargetNodeId);

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Removed node '%s'"), *Op.TargetNodeId);
	return true;
}

bool FBerniGraphOps::ExecuteMoveNode(UEdGraph* Graph, const FBerniPatchOperation& Op,
	TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError)
{
	UEdGraphNode** NodePtr = NodeMap.Find(Op.TargetNodeId);
	if (!NodePtr || !*NodePtr)
	{
		OutError = FString::Printf(TEXT("Node '%s' not found."), *Op.TargetNodeId);
		return false;
	}

	UEdGraphNode* Node = *NodePtr;
	Node->NodePosX = Op.X;
	Node->NodePosY = Op.Y;

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Moved node '%s' to (%d, %d)"), *Op.TargetNodeId, Op.X, Op.Y);
	return true;
}

bool FBerniGraphOps::ExecuteConnect(UEdGraph* Graph, const FBerniPatchOperation& Op,
	TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError)
{
	UEdGraphPin* FromPin = ResolvePin(Op.FromPinRef, Graph, NodeMap, OutError);
	if (!FromPin) return false;

	UEdGraphPin* ToPin = ResolvePin(Op.ToPinRef, Graph, NodeMap, OutError);
	if (!ToPin) return false;

	// Validate direction
	if (FromPin->Direction == ToPin->Direction)
	{
		OutError = FString::Printf(TEXT("Cannot connect pins with same direction (%s -> %s)."),
			*Op.FromPinRef, *Op.ToPinRef);
		return false;
	}

	// Check schema compatibility
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (Schema)
	{
		FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
		if (Response.Response == CONNECT_RESPONSE_DISALLOW)
		{
			OutError = FString::Printf(TEXT("Schema disallows connection: %s"), *Response.Message.ToString());
			return false;
		}
	}

	FromPin->MakeLinkTo(ToPin);

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Connected %s -> %s"), *Op.FromPinRef, *Op.ToPinRef);
	return true;
}

bool FBerniGraphOps::ExecuteDisconnect(UEdGraph* Graph, const FBerniPatchOperation& Op,
	TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError)
{
	UEdGraphPin* FromPin = ResolvePin(Op.FromPinRef, Graph, NodeMap, OutError);
	if (!FromPin) return false;

	UEdGraphPin* ToPin = ResolvePin(Op.ToPinRef, Graph, NodeMap, OutError);
	if (!ToPin) return false;

	FromPin->BreakLinkTo(ToPin);

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Disconnected %s -> %s"), *Op.FromPinRef, *Op.ToPinRef);
	return true;
}

bool FBerniGraphOps::ExecuteSetDefault(UEdGraph* Graph, const FBerniPatchOperation& Op,
	TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError)
{
	UEdGraphNode** NodePtr = NodeMap.Find(Op.TargetNodeId);
	if (!NodePtr || !*NodePtr)
	{
		OutError = FString::Printf(TEXT("Node '%s' not found."), *Op.TargetNodeId);
		return false;
	}

	UEdGraphNode* Node = *NodePtr;
	UEdGraphPin* Pin = Node->FindPin(FName(*Op.PinName));
	if (!Pin)
	{
		// Try case-insensitive
		for (UEdGraphPin* CandidatePin : Node->Pins)
		{
			if (CandidatePin && CandidatePin->GetName().Equals(Op.PinName, ESearchCase::IgnoreCase))
			{
				Pin = CandidatePin;
				break;
			}
		}
	}
	if (!Pin)
	{
		OutError = FString::Printf(TEXT("Pin '%s' not found on node '%s'."), *Op.PinName, *Op.TargetNodeId);
		return false;
	}

	// Pin type validation
	const FString PinCategory = Pin->PinType.PinCategory.ToString();

	if (PinCategory == UEdGraphSchema_K2::PC_Exec.ToString())
	{
		OutError = FString::Printf(TEXT("Cannot set default value on exec pin '%s'."), *Op.PinName);
		return false;
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Int.ToString())
	{
		if (!Op.Value.IsNumeric())
		{
			OutError = FString::Printf(TEXT("Pin '%s' is int but value '%s' is not numeric."), *Op.PinName, *Op.Value);
			return false;
		}
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Real.ToString()
		|| PinCategory == TEXT("float")
		|| PinCategory == TEXT("double"))
	{
		if (!Op.Value.IsNumeric())
		{
			OutError = FString::Printf(TEXT("Pin '%s' is float/double but value '%s' is not numeric."), *Op.PinName, *Op.Value);
			return false;
		}
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Boolean.ToString())
	{
		FString Lower = Op.Value.ToLower();
		if (Lower != TEXT("true") && Lower != TEXT("false") && Lower != TEXT("0") && Lower != TEXT("1"))
		{
			OutError = FString::Printf(TEXT("Pin '%s' is bool but value '%s' is not a valid boolean."), *Op.PinName, *Op.Value);
			return false;
		}
	}

	// Use schema's default-value setter when available for proper conversion
	const UEdGraphSchema_K2* K2Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
	if (K2Schema)
	{
		K2Schema->TrySetDefaultValue(*Pin, Op.Value);
	}
	else
	{
		Pin->DefaultValue = Op.Value;
	}

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Set %s.%s = '%s'"), *Op.TargetNodeId, *Op.PinName, *Op.Value);
	return true;
}

bool FBerniGraphOps::ExecuteSetComment(UEdGraph* Graph, const FBerniPatchOperation& Op,
	TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError)
{
	UEdGraphNode** NodePtr = NodeMap.Find(Op.TargetNodeId);
	if (!NodePtr || !*NodePtr)
	{
		OutError = FString::Printf(TEXT("Node '%s' not found."), *Op.TargetNodeId);
		return false;
	}

	UEdGraphNode* Node = *NodePtr;
	Node->NodeComment = Op.Comment;
	Node->bCommentBubblePinned = !Op.Comment.IsEmpty();
	Node->bCommentBubbleVisible = !Op.Comment.IsEmpty();

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Set comment on '%s': '%s'"), *Op.TargetNodeId, *Op.Comment);
	return true;
}

// ===========================================================================
// Private: Pin resolution
// ===========================================================================

UEdGraphPin* FBerniGraphOps::ResolvePin(const FString& PinRef, UEdGraph* Graph,
	TMap<FString, UEdGraphNode*>& NodeMap, FString& OutError)
{
	FString NodeId, PinName;
	if (!PinRef.Split(TEXT("."), &NodeId, &PinName))
	{
		OutError = FString::Printf(TEXT("Invalid pin ref format: '%s'. Expected 'nodeId.pinName'."), *PinRef);
		return nullptr;
	}

	// Look up node
	UEdGraphNode** NodePtr = NodeMap.Find(NodeId);
	if (!NodePtr || !*NodePtr)
	{
		OutError = FString::Printf(TEXT("Node '%s' not found (from pin ref '%s')."), *NodeId, *PinRef);
		return nullptr;
	}

	UEdGraphNode* Node = *NodePtr;

	// Common pin name aliases
	FString NormalizedPinName = PinName;
	if (NormalizedPinName == TEXT("execIn"))
		NormalizedPinName = UEdGraphSchema_K2::PN_Execute.ToString();
	else if (NormalizedPinName == TEXT("execOut") || NormalizedPinName == TEXT("then"))
		NormalizedPinName = UEdGraphSchema_K2::PN_Then.ToString();
	else if (NormalizedPinName == TEXT("condition"))
		NormalizedPinName = UEdGraphSchema_K2::PN_Condition.ToString();
	else if (NormalizedPinName == TEXT("returnValue"))
		NormalizedPinName = UEdGraphSchema_K2::PN_ReturnValue.ToString();

	UEdGraphPin* Pin = Node->FindPin(FName(*NormalizedPinName));
	if (!Pin)
	{
		// Try case-insensitive search
		for (UEdGraphPin* CandidatePin : Node->Pins)
		{
			if (CandidatePin && CandidatePin->GetName().Equals(PinName, ESearchCase::IgnoreCase))
			{
				Pin = CandidatePin;
				break;
			}
		}
	}

	if (!Pin)
	{
		// Build helpful error with available pin names
		OutError = FString::Printf(TEXT("Pin '%s' not found on node '%s'. Available pins: "), *PinName, *NodeId);
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P) OutError += P->GetName() + TEXT(", ");
		}
		return nullptr;
	}

	return Pin;
}

// ===========================================================================
// Private: Backup/Restore
// ===========================================================================

bool FBerniGraphOps::BackupAsset(const FString& AssetPath, FString& OutError)
{
	FString PackagePath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, PackagePath, TEXT(".uasset")))
	{
		OutError = FString::Printf(TEXT("Could not resolve file path for '%s'."), *AssetPath);
		return false;
	}

	if (!IFileManager::Get().FileExists(*PackagePath))
	{
		OutError = FString::Printf(TEXT("Asset file not found: '%s'."), *PackagePath);
		return false;
	}

	FString BackupDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EpicEngineAIAccessBridge"), TEXT("Backups"));
	IFileManager::Get().MakeDirectory(*BackupDir, true);

	FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString AssetFilename = FPaths::GetCleanFilename(PackagePath);
	FString BackupPath = FPaths::Combine(BackupDir, FString::Printf(TEXT("%s_%s.bak"), *AssetFilename, *Timestamp));

	uint32 CopyResult = IFileManager::Get().Copy(*BackupPath, *PackagePath, true, true);
	if (CopyResult != COPY_OK)
	{
		OutError = FString::Printf(TEXT("Failed to copy '%s' to '%s'."), *PackagePath, *BackupPath);
		return false;
	}

	// Push onto backup stack
	TArray<FString>& Stack = BackupStacks.FindOrAdd(AssetPath);
	Stack.Add(BackupPath);

	// Prune old backups
	PruneBackups(AssetPath);

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Backed up %s -> %s (stack depth: %d)"), *PackagePath, *BackupPath, Stack.Num());
	return true;
}

bool FBerniGraphOps::RestoreBackup(const FString& AssetPath, FString& OutError)
{
	TArray<FString>* Stack = BackupStacks.Find(AssetPath);
	if (!Stack || Stack->Num() == 0)
	{
		OutError = FString::Printf(TEXT("No backup found for '%s'."), *AssetPath);
		return false;
	}

	FString BackupPath = Stack->Last();

	if (!IFileManager::Get().FileExists(*BackupPath))
	{
		OutError = FString::Printf(TEXT("Backup file missing: '%s'."), *BackupPath);
		Stack->RemoveAt(Stack->Num() - 1);
		return false;
	}

	FString PackagePath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, PackagePath, TEXT(".uasset")))
	{
		OutError = FString::Printf(TEXT("Could not resolve file path for '%s'."), *AssetPath);
		return false;
	}

	uint32 CopyResult = IFileManager::Get().Copy(*PackagePath, *BackupPath, true, true);
	if (CopyResult != COPY_OK)
	{
		OutError = FString::Printf(TEXT("Failed to restore backup."));
		return false;
	}

	// Remove the used backup file and pop from stack
	IFileManager::Get().Delete(*BackupPath);
	Stack->RemoveAt(Stack->Num() - 1);

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Restored %s from backup (remaining levels: %d)"), *AssetPath, Stack->Num());
	return true;
}

void FBerniGraphOps::PruneBackups(const FString& AssetPath)
{
	TArray<FString>* Stack = BackupStacks.Find(AssetPath);
	if (!Stack) return;

	// Count-based: remove oldest if exceeding max
	while (Stack->Num() > BerniConstants::MaxBackupsPerAsset)
	{
		FString OldestPath = (*Stack)[0];
		IFileManager::Get().Delete(*OldestPath);
		Stack->RemoveAt(0);
		UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Pruned old backup: %s"), *OldestPath);
	}

	// Age-based: scan backup directory for old files from any asset
	FString BackupDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EpicEngineAIAccessBridge"), TEXT("Backups"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(BackupDir, TEXT("*.bak")), true, false);

	FDateTime CutoffTime = FDateTime::UtcNow() - FTimespan::FromDays(BerniConstants::BackupMaxAgeDays);
	for (const FString& File : Files)
	{
		FString FullPath = FPaths::Combine(BackupDir, File);
		FDateTime ModTime = IFileManager::Get().GetTimeStamp(*FullPath);
		if (ModTime < CutoffTime)
		{
			IFileManager::Get().Delete(*FullPath);
			// Remove from any stack that references it
			for (auto& Pair : BackupStacks)
			{
				Pair.Value.Remove(FullPath);
			}
			UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Pruned expired backup: %s"), *FullPath);
		}
	}
}

// ===========================================================================
// Private: Compile
// ===========================================================================

bool FBerniGraphOps::CompileAndSave(UBlueprint* BP, FString& OutError)
{
	if (!BP)
	{
		OutError = TEXT("Null Blueprint.");
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);

	if (BP->Status == BS_Error)
	{
		OutError = TEXT("Blueprint compiled with errors. Check Output Log.");
		return false;
	}

	// Save the package
	UPackage* Package = BP->GetOutermost();
	if (Package)
	{
		Package->SetDirtyFlag(true);
		FString PackageFilename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, TEXT(".uasset")))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			bool bSaved = UPackage::SavePackage(Package, BP, *PackageFilename, SaveArgs);
			if (!bSaved)
			{
				OutError = TEXT("Compiled successfully but failed to save package to disk.");
				return false;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Compiled and saved: %s"), *BP->GetPathName());
	return true;
}
