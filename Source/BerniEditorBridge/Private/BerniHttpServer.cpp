// Copyright ReapAndRuin Dev. All Rights Reserved.

#include "BerniHttpServer.h"
#include "BerniGraphOps.h"
#include "BerniSceneOps.h"
#include "BerniAuditLog.h"
#include "BerniTypes.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"

FBerniHttpServer::FBerniHttpServer()
{
	GraphOps = MakeShared<FBerniGraphOps>();
	SceneOps = MakeShared<FBerniSceneOps>();
	AuditLog = MakeShared<FBerniAuditLog>();
}

FBerniHttpServer::~FBerniHttpServer()
{
	Stop();
}

void FBerniHttpServer::Start()
{
	if (bRunning) return;

	FHttpServerModule& HttpModule = FHttpServerModule::Get();
	TSharedPtr<IHttpRouter> Router = HttpModule.GetHttpRouter(BerniConstants::HttpPort);

	if (!Router)
	{
		UE_LOG(LogTemp, Error, TEXT("[BerniEditorBridge] Failed to create HTTP router on port %d"), BerniConstants::HttpPort);
		return;
	}

	// Bind routes
	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/session/open")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleSessionOpen)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/bp/inspect")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleBpInspect)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/bp/graphs")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleBpGraphs)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/bp/classes")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleBpClasses)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/bp/patch")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleBpPatch)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/bp/apply")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleBpApply)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/bp/undo")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleBpUndo)
	));

	// Scene routes
	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/scene/actors")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleSceneActors)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/scene/find")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleSceneFind)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/scene/spawn")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleSceneSpawn)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/scene/delete")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleSceneDelete)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/scene/transform")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleSceneTransform)
	));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/scene/properties")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleSceneProperties)
	));

	// Scripting routes
	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/exec/python")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FBerniHttpServer::HandleExecPython)
	));

	HttpModule.StartAllListeners();

	bRunning = true;
	UE_LOG(LogTemp, Warning, TEXT("[BerniEditorBridge] HTTP server listening on 127.0.0.1:%d"), BerniConstants::HttpPort);
}

void FBerniHttpServer::Stop()
{
	if (!bRunning) return;

	FHttpServerModule& HttpModule = FHttpServerModule::Get();
	for (const FHttpRouteHandle& Handle : RouteHandles)
	{
		HttpModule.GetHttpRouter(BerniConstants::HttpPort)->UnbindRoute(Handle);
	}
	RouteHandles.Empty();

	HttpModule.StopAllListeners();
	bRunning = false;

	UE_LOG(LogTemp, Log, TEXT("[BerniEditorBridge] HTTP server stopped."));
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleSessionOpen(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Prune expired sessions
	TArray<FString> Expired;
	for (auto& Pair : Sessions)
	{
		if (!Pair.Value.IsValid())
			Expired.Add(Pair.Key);
	}
	for (const FString& Key : Expired)
	{
		Sessions.Remove(Key);
	}

	// Enforce max sessions
	if (Sessions.Num() >= BerniConstants::MaxSessions)
	{
		OnComplete(MakeErrorResponse(429, TEXT("Too many active sessions. Try again later.")));
		return true;
	}

	FBerniSession NewSession;
	NewSession.Token = GenerateToken();
	NewSession.ExpiresAt = FDateTime::UtcNow() + FTimespan::FromSeconds(BerniConstants::SessionLifetimeSeconds);

	Sessions.Add(NewSession.Token, NewSession);

	AuditLog->LogSession(NewSession.Token, TEXT("opened"));

	TSharedPtr<FJsonObject> ResponseBody = MakeShared<FJsonObject>();
	ResponseBody->SetStringField(TEXT("token"), NewSession.Token);
	ResponseBody->SetStringField(TEXT("expiresAt"), NewSession.ExpiresAt.ToIso8601());

	OnComplete(MakeJsonResponse(200, ResponseBody));
	return true;
}

// ---------------------------------------------------------------------------
// /bp/inspect
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleBpInspect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString AssetPath = Body->GetStringField(TEXT("assetPath"));
	FString GraphName = Body->GetStringField(TEXT("graph"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	if (AssetPath.IsEmpty())
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing 'assetPath'.")));
		return true;
	}
	if (GraphName.IsEmpty())
	{
		GraphName = TEXT("UserConstructionScript");
	}

	FString OpError;
	TSharedPtr<FJsonObject> Result = GraphOps->InspectGraph(AssetPath, GraphName, OpError);

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/bp/inspect"), AssetPath, TEXT("inspect"), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/bp/inspect"), AssetPath, TEXT("inspect"), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /bp/graphs
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleBpGraphs(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString AssetPath = Body->GetStringField(TEXT("assetPath"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	if (AssetPath.IsEmpty())
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing 'assetPath'.")));
		return true;
	}

	FString OpError;
	TSharedPtr<FJsonObject> Result = GraphOps->ListGraphs(AssetPath, OpError);

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/bp/graphs"), AssetPath, TEXT("listGraphs"), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/bp/graphs"), AssetPath, TEXT("listGraphs"), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /bp/classes
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleBpClasses(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString ClassName = Body->GetStringField(TEXT("className"));
	FString Filter = Body->GetStringField(TEXT("filter"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	if (ClassName.IsEmpty())
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing 'className'.")));
		return true;
	}

	FString OpError;
	TSharedPtr<FJsonObject> Result = GraphOps->QueryClass(ClassName, Filter, OpError);

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/bp/classes"), ClassName, TEXT("queryClass"), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/bp/classes"), ClassName, TEXT("queryClass"), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /bp/patch (dry-run)
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleBpPatch(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString AssetPath = Body->GetStringField(TEXT("assetPath"));
	FString GraphName = Body->GetStringField(TEXT("graph"));
	bool bDryRun = true;
	Body->TryGetBoolField(TEXT("dryRun"), bDryRun);
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	if (AssetPath.IsEmpty())
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing 'assetPath'.")));
		return true;
	}

	// Parse ops
	const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
	if (!Body->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing or invalid 'ops' array.")));
		return true;
	}

	if (OpsArray->Num() > BerniConstants::MaxOpsPerRequest)
	{
		OnComplete(MakeErrorResponse(400,
			FString::Printf(TEXT("Too many ops (%d). Max is %d."), OpsArray->Num(), BerniConstants::MaxOpsPerRequest)));
		return true;
	}

	TArray<FBerniPatchOperation> Ops;
	for (const TSharedPtr<FJsonValue>& Val : *OpsArray)
	{
		if (Val && Val->Type == EJson::Object)
		{
			Ops.Add(FBerniPatchOperation::FromJson(Val->AsObject()));
		}
	}

	FString OpError;
	TSharedPtr<FJsonObject> Result;

	if (bDryRun)
	{
		Result = GraphOps->ValidatePatch(AssetPath, GraphName, Ops, OpError);
	}
	else
	{
		// Non-dry-run through /bp/patch is disallowed — use /bp/apply
		OnComplete(MakeErrorResponse(400, TEXT("Set dryRun=true for /bp/patch, or use /bp/apply to execute.")));
		return true;
	}

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/bp/patch"), AssetPath,
			FString::Printf(TEXT("validate %d ops"), Ops.Num()), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/bp/patch"), AssetPath,
		FString::Printf(TEXT("validate %d ops"), Ops.Num()), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /bp/apply
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleBpApply(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString AssetPath = Body->GetStringField(TEXT("assetPath"));
	FString GraphName = Body->GetStringField(TEXT("graph"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	if (AssetPath.IsEmpty())
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing 'assetPath'.")));
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
	if (!Body->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing or invalid 'ops' array.")));
		return true;
	}

	if (OpsArray->Num() > BerniConstants::MaxOpsPerRequest)
	{
		OnComplete(MakeErrorResponse(400,
			FString::Printf(TEXT("Too many ops (%d). Max is %d."), OpsArray->Num(), BerniConstants::MaxOpsPerRequest)));
		return true;
	}

	TArray<FBerniPatchOperation> Ops;
	for (const TSharedPtr<FJsonValue>& Val : *OpsArray)
	{
		if (Val && Val->Type == EJson::Object)
		{
			Ops.Add(FBerniPatchOperation::FromJson(Val->AsObject()));
		}
	}

	FString OpError;
	TSharedPtr<FJsonObject> Result = GraphOps->ApplyPatch(AssetPath, GraphName, Ops, OpError);

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/bp/apply"), AssetPath,
			FString::Printf(TEXT("apply %d ops"), Ops.Num()), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/bp/apply"), AssetPath,
		FString::Printf(TEXT("apply %d ops"), Ops.Num()), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /bp/undo
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleBpUndo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString AssetPath = Body->GetStringField(TEXT("assetPath"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	if (AssetPath.IsEmpty())
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing 'assetPath'.")));
		return true;
	}

	// Support multi-level undo
	double LevelsDouble = 1;
	Body->TryGetNumberField(TEXT("levels"), LevelsDouble);
	int32 RequestedLevels = FMath::Clamp(static_cast<int32>(LevelsDouble), 1, BerniConstants::MaxUndoLevels);

	int32 UndoneCount = 0;
	FString LastError;

	for (int32 i = 0; i < RequestedLevels; i++)
	{
		FString OpError;
		if (GraphOps->UndoLastApply(AssetPath, OpError))
		{
			UndoneCount++;
		}
		else
		{
			LastError = OpError;
			break;
		}
	}

	TSharedPtr<FJsonObject> ResponseBody = MakeShared<FJsonObject>();
	ResponseBody->SetBoolField(TEXT("success"), UndoneCount > 0);
	ResponseBody->SetStringField(TEXT("assetPath"), AssetPath);
	ResponseBody->SetNumberField(TEXT("levelsUndone"), UndoneCount);
	ResponseBody->SetNumberField(TEXT("levelsRemaining"), GraphOps->GetUndoLevels(AssetPath));

	if (UndoneCount == 0)
	{
		ResponseBody->SetStringField(TEXT("error"), LastError);
		AuditLog->LogRequest(RequestId, TEXT("/bp/undo"), AssetPath, TEXT("undo"), false, LastError);
		OnComplete(MakeJsonResponse(500, ResponseBody));
	}
	else
	{
		ResponseBody->SetStringField(TEXT("message"),
			FString::Printf(TEXT("Reverted %d level(s)."), UndoneCount));
		AuditLog->LogRequest(RequestId, TEXT("/bp/undo"), AssetPath,
			FString::Printf(TEXT("undo %d levels"), UndoneCount), true, TEXT("ok"));
		OnComplete(MakeJsonResponse(200, ResponseBody));
	}

	return true;
}

// ---------------------------------------------------------------------------
// /scene/actors
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleSceneActors(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString ClassFilter = Body->GetStringField(TEXT("classFilter"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	FString OpError;
	TSharedPtr<FJsonObject> Result = SceneOps->ListActors(ClassFilter, OpError);

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/scene/actors"), TEXT(""), TEXT("listActors"), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/scene/actors"), TEXT(""), TEXT("listActors"), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /scene/find
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleSceneFind(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString NamePattern = Body->GetStringField(TEXT("name"));
	FString ClassFilter = Body->GetStringField(TEXT("classFilter"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	FString OpError;
	TSharedPtr<FJsonObject> Result = SceneOps->FindActors(NamePattern, ClassFilter, OpError);

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/scene/find"), NamePattern, TEXT("findActors"), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/scene/find"), NamePattern, TEXT("findActors"), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /scene/spawn
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleSceneSpawn(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString ClassName = Body->GetStringField(TEXT("className"));
	FString AssetPath = Body->GetStringField(TEXT("assetPath"));
	FString ActorLabel = Body->GetStringField(TEXT("label"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	// Parse location
	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (Body->TryGetObjectField(TEXT("location"), LocObj) && LocObj)
	{
		double X = 0, Y = 0, Z = 0;
		(*LocObj)->TryGetNumberField(TEXT("x"), X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Z);
		Location = FVector(X, Y, Z);
	}

	// Parse rotation
	FRotator Rotation = FRotator::ZeroRotator;
	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	if (Body->TryGetObjectField(TEXT("rotation"), RotObj) && RotObj)
	{
		double Pitch = 0, Yaw = 0, Roll = 0;
		(*RotObj)->TryGetNumberField(TEXT("pitch"), Pitch);
		(*RotObj)->TryGetNumberField(TEXT("yaw"), Yaw);
		(*RotObj)->TryGetNumberField(TEXT("roll"), Roll);
		Rotation = FRotator(Pitch, Yaw, Roll);
	}

	// Parse scale
	FVector Scale = FVector::OneVector;
	const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
	if (Body->TryGetObjectField(TEXT("scale"), ScaleObj) && ScaleObj)
	{
		double X = 1, Y = 1, Z = 1;
		(*ScaleObj)->TryGetNumberField(TEXT("x"), X);
		(*ScaleObj)->TryGetNumberField(TEXT("y"), Y);
		(*ScaleObj)->TryGetNumberField(TEXT("z"), Z);
		Scale = FVector(X, Y, Z);
	}

	FString OpError;
	TSharedPtr<FJsonObject> Result = SceneOps->SpawnActor(ClassName, AssetPath, ActorLabel, Location, Rotation, Scale, OpError);

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/scene/spawn"), ClassName.IsEmpty() ? AssetPath : ClassName, TEXT("spawn"), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/scene/spawn"), ClassName.IsEmpty() ? AssetPath : ClassName, TEXT("spawn"), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /scene/delete
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleSceneDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString ActorName = Body->GetStringField(TEXT("name"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	if (ActorName.IsEmpty())
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing 'name'.")));
		return true;
	}

	FString OpError;
	TSharedPtr<FJsonObject> Result = SceneOps->DeleteActor(ActorName, OpError);

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/scene/delete"), ActorName, TEXT("delete"), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/scene/delete"), ActorName, TEXT("delete"), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /scene/transform
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleSceneTransform(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString ActorName = Body->GetStringField(TEXT("name"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	if (ActorName.IsEmpty())
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing 'name'.")));
		return true;
	}

	// Check if this is a get or set operation
	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	const TSharedPtr<FJsonObject>* ScaleObj = nullptr;

	bool bHasLocation = Body->TryGetObjectField(TEXT("location"), LocObj);
	bool bHasRotation = Body->TryGetObjectField(TEXT("rotation"), RotObj);
	bool bHasScale = Body->TryGetObjectField(TEXT("scale"), ScaleObj);

	FString OpError;
	TSharedPtr<FJsonObject> Result;

	if (bHasLocation || bHasRotation || bHasScale)
	{
		// Set transform
		Result = SceneOps->SetTransform(
			ActorName,
			bHasLocation ? *LocObj : nullptr,
			bHasRotation ? *RotObj : nullptr,
			bHasScale ? *ScaleObj : nullptr,
			OpError
		);
	}
	else
	{
		// Get transform
		Result = SceneOps->GetTransform(ActorName, OpError);
	}

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/scene/transform"), ActorName, TEXT("transform"), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/scene/transform"), ActorName, TEXT("transform"), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /scene/properties
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleSceneProperties(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString ActorName = Body->GetStringField(TEXT("name"));
	FString PropertyName = Body->GetStringField(TEXT("property"));
	FString Value = Body->GetStringField(TEXT("value"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	if (ActorName.IsEmpty())
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing 'name'.")));
		return true;
	}

	FString OpError;
	TSharedPtr<FJsonObject> Result;

	if (!PropertyName.IsEmpty() && Body->HasField(TEXT("value")))
	{
		// Set property
		Result = SceneOps->SetProperty(ActorName, PropertyName, Value, OpError);
	}
	else
	{
		// Get properties
		Result = SceneOps->GetProperties(ActorName, OpError);
	}

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/scene/properties"), ActorName, TEXT("properties"), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/scene/properties"), ActorName, TEXT("properties"), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// /exec/python
// ---------------------------------------------------------------------------

bool FBerniHttpServer::HandleExecPython(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthError;
	if (!ValidateToken(Request, AuthError))
	{
		OnComplete(MakeErrorResponse(401, AuthError));
		return true;
	}

	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		OnComplete(MakeErrorResponse(400, TEXT("Invalid JSON body.")));
		return true;
	}

	FString Code = Body->GetStringField(TEXT("code"));
	FString RequestId = FGuid::NewGuid().ToString().Left(8);

	if (Code.IsEmpty())
	{
		OnComplete(MakeErrorResponse(400, TEXT("Missing 'code'.")));
		return true;
	}

	FString OpError;
	TSharedPtr<FJsonObject> Result = SceneOps->ExecutePython(Code, OpError);

	if (!Result)
	{
		AuditLog->LogRequest(RequestId, TEXT("/exec/python"), TEXT(""), TEXT("python"), false, OpError);
		OnComplete(MakeErrorResponse(500, OpError));
		return true;
	}

	AuditLog->LogRequest(RequestId, TEXT("/exec/python"), TEXT(""), TEXT("python"), true, TEXT("ok"));
	OnComplete(MakeJsonResponse(200, Result));
	return true;
}

// ---------------------------------------------------------------------------
// Auth helpers
// ---------------------------------------------------------------------------

bool FBerniHttpServer::ValidateToken(const FHttpServerRequest& Request, FString& OutError)
{
	// Check Authorization header: "Bearer <token>"
	// Try multiple casings for broad HTTP client compatibility
	const TArray<FString>* AuthHeaders = Request.Headers.Find(TEXT("Authorization"));
	if (!AuthHeaders || AuthHeaders->Num() == 0)
	{
		AuthHeaders = Request.Headers.Find(TEXT("authorization"));
	}
	if (!AuthHeaders || AuthHeaders->Num() == 0)
	{
		OutError = TEXT("Missing Authorization header. Use 'Bearer <token>'.");
		return false;
	}

	FString AuthValue = (*AuthHeaders)[0];
	if (!AuthValue.StartsWith(TEXT("Bearer ")))
	{
		OutError = TEXT("Invalid Authorization format. Use 'Bearer <token>'.");
		return false;
	}

	FString Token = AuthValue.Mid(7).TrimStartAndEnd();
	FBerniSession* Session = Sessions.Find(Token);

	if (!Session)
	{
		OutError = TEXT("Unknown session token.");
		return false;
	}

	if (!Session->IsValid())
	{
		Sessions.Remove(Token);
		OutError = TEXT("Session expired. Open a new session.");
		return false;
	}

	return true;
}

FString FBerniHttpServer::GenerateToken()
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FBerniHttpServer::ParseRequestBody(const FHttpServerRequest& Request)
{
	// Use explicit length — TArray<uint8> is not guaranteed null-terminated
	FUTF8ToTCHAR Converter(reinterpret_cast<const char*>(Request.Body.GetData()), Request.Body.Num());
	FString BodyStr(Converter.Length(), Converter.Get());

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		return nullptr;
	}
	return JsonObject;
}

TUniquePtr<FHttpServerResponse> FBerniHttpServer::MakeJsonResponse(int32 Code, TSharedPtr<FJsonObject> Body)
{
	FString OutputStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	// Build response manually for broad UE5 compatibility
	TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
	FTCHARToUTF8 Converter(*OutputStr);
	Response->Body.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
	Response->Headers.Add(TEXT("Content-Type"), { TEXT("application/json; charset=utf-8") });
	Response->Code = static_cast<EHttpServerResponseCodes>(Code);
	return Response;
}

TUniquePtr<FHttpServerResponse> FBerniHttpServer::MakeErrorResponse(int32 Code, const FString& Message)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetBoolField(TEXT("success"), false);
	Body->SetStringField(TEXT("error"), Message);
	return MakeJsonResponse(Code, Body);
}
