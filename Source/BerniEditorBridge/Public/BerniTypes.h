// Copyright ReapAndRuin Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

struct FBerniSession
{
	FString Token;
	FDateTime ExpiresAt;

	bool IsValid() const
	{
		return !Token.IsEmpty() && FDateTime::UtcNow() < ExpiresAt;
	}
};

// ---------------------------------------------------------------------------
// Patch operations
// ---------------------------------------------------------------------------

/** Allowlisted patch operation types */
enum class EBerniPatchOp : uint8
{
	AddNode,
	RemoveNode,
	MoveNode,
	ConnectPins,
	DisconnectPins,
	SetDefault,
	SetComment,
	Unknown
};

inline EBerniPatchOp ParsePatchOp(const FString& OpStr)
{
	if (OpStr == TEXT("addNode"))       return EBerniPatchOp::AddNode;
	if (OpStr == TEXT("removeNode"))    return EBerniPatchOp::RemoveNode;
	if (OpStr == TEXT("moveNode"))      return EBerniPatchOp::MoveNode;
	if (OpStr == TEXT("connect"))       return EBerniPatchOp::ConnectPins;
	if (OpStr == TEXT("disconnect"))    return EBerniPatchOp::DisconnectPins;
	if (OpStr == TEXT("setDefault"))    return EBerniPatchOp::SetDefault;
	if (OpStr == TEXT("setComment"))    return EBerniPatchOp::SetComment;
	return EBerniPatchOp::Unknown;
}

/** A single patch operation parsed from JSON */
struct FBerniPatchOperation
{
	EBerniPatchOp Op = EBerniPatchOp::Unknown;
	FString RawOp;

	// addNode
	FString NodeClass;
	FString NodeId;
	int32 X = 0;
	int32 Y = 0;

	// addNode: specialized configuration
	FString FunctionReference;   // "ClassName.FunctionName" for K2Node_CallFunction
	FString VariableReference;   // "PropertyName" for K2Node_VariableGet/Set
	FString CastClass;           // "ClassName" for K2Node_DynamicCast

	// connect / disconnect
	FString FromPinRef;  // "nodeId.pinName"
	FString ToPinRef;

	// setDefault
	FString TargetNodeId;
	FString PinName;
	FString Value;

	// setComment
	FString Comment;

	// Validation result
	bool bValid = false;
	FString ValidationError;

	static FBerniPatchOperation FromJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FBerniPatchOperation Result;
		if (!Obj) return Result;

		Obj->TryGetStringField(TEXT("op"), Result.RawOp);
		Result.Op = ParsePatchOp(Result.RawOp);

		// addNode fields
		Obj->TryGetStringField(TEXT("class"), Result.NodeClass);
		Obj->TryGetStringField(TEXT("id"), Result.NodeId);
		double TempX = 0, TempY = 0;
		if (Obj->TryGetNumberField(TEXT("x"), TempX)) Result.X = static_cast<int32>(TempX);
		if (Obj->TryGetNumberField(TEXT("y"), TempY)) Result.Y = static_cast<int32>(TempY);

		// addNode: specialized configuration
		Obj->TryGetStringField(TEXT("functionReference"), Result.FunctionReference);
		Obj->TryGetStringField(TEXT("variableReference"), Result.VariableReference);
		Obj->TryGetStringField(TEXT("castClass"), Result.CastClass);

		// connect / disconnect
		Obj->TryGetStringField(TEXT("from"), Result.FromPinRef);
		Obj->TryGetStringField(TEXT("to"), Result.ToPinRef);

		// setDefault / removeNode / moveNode / setComment
		Obj->TryGetStringField(TEXT("targetNode"), Result.TargetNodeId);
		Obj->TryGetStringField(TEXT("pin"), Result.PinName);
		Obj->TryGetStringField(TEXT("value"), Result.Value);
		Obj->TryGetStringField(TEXT("comment"), Result.Comment);

		return Result;
	}
};

// ---------------------------------------------------------------------------
// Allowlisted node classes for addNode
// ---------------------------------------------------------------------------

inline bool IsNodeClassAllowed(const FString& ClassName)
{
	static TSet<FString> Allowed = {
		TEXT("K2Node_CallFunction"),
		TEXT("K2Node_IfThenElse"),
		TEXT("K2Node_VariableGet"),
		TEXT("K2Node_VariableSet"),
		TEXT("K2Node_DynamicCast"),
		TEXT("K2Node_MakeArray"),
		TEXT("K2Node_BreakStruct"),
		TEXT("K2Node_MakeStruct"),
		TEXT("K2Node_Knot"),           // reroute
		TEXT("K2Node_IsValidObject"),  // the IsValid we need for gating
		TEXT("K2Node_MacroInstance"),
		TEXT("K2Node_TemporaryVariable"),
	};
	return Allowed.Contains(ClassName);
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace BerniConstants
{
	constexpr int32 HttpPort = 8523;
	constexpr int32 MaxOpsPerRequest = 100;
	constexpr int32 SessionLifetimeSeconds = 3600; // 1 hour
	constexpr int32 MaxSessions = 20;
	constexpr int32 MaxUndoLevels = 10;
	constexpr int32 MaxBackupsPerAsset = 10;
	constexpr int32 BackupMaxAgeDays = 7;
}
