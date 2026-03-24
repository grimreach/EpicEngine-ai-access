// Copyright ReapAndRuin Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "HttpResultCallback.h"
#include "BerniTypes.h"

class FBerniGraphOps;
class FBerniSceneOps;
class FBerniAuditLog;

/**
 * Localhost-only HTTP server for Blueprint inspection, patching, scene manipulation, and scripting.
 */
class EPICENGINEAIACCESSBRIDGE_API FBerniHttpServer : public TSharedFromThis<FBerniHttpServer>
{
public:
	FBerniHttpServer();
	~FBerniHttpServer();

	void Start();
	void Stop();
	bool IsRunning() const { return bRunning; }

private:
	// Route handlers — session
	bool HandleSessionOpen(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Route handlers — blueprint
	bool HandleBpInspect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleBpGraphs(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleBpClasses(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleBpPatch(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleBpApply(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleBpUndo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Route handlers — scene
	bool HandleSceneActors(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneFind(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneSpawn(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneTransform(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSceneProperties(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Route handlers — scripting
	bool HandleExecPython(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Route handlers — blueprint creation
	bool HandleSceneCreateBlueprint(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Auth
	bool ValidateToken(const FHttpServerRequest& Request, FString& OutError);
	FString GenerateToken();

	// JSON helpers
	TSharedPtr<FJsonObject> ParseRequestBody(const FHttpServerRequest& Request);
	TUniquePtr<FHttpServerResponse> MakeJsonResponse(int32 Code, TSharedPtr<FJsonObject> Body);
	TUniquePtr<FHttpServerResponse> MakeErrorResponse(int32 Code, const FString& Message);

	// State
	bool bRunning = false;
	TArray<FHttpRouteHandle> RouteHandles;
	TMap<FString, FBerniSession> Sessions;

	// Sub-components
	TSharedPtr<FBerniGraphOps> GraphOps;
	TSharedPtr<FBerniSceneOps> SceneOps;
	TSharedPtr<FBerniAuditLog> AuditLog;
};
