// Copyright ReapAndRuin Dev. All Rights Reserved.

#include "BerniEditorBridgeModule.h"
#include "BerniHttpServer.h"

#define LOCTEXT_NAMESPACE "FBerniEditorBridgeModule"

void FBerniEditorBridgeModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("[BerniEditorBridge] Module starting up."));

	HttpServer = MakeShared<FBerniHttpServer>();
	HttpServer->Start();
}

void FBerniEditorBridgeModule::ShutdownModule()
{
	if (HttpServer)
	{
		HttpServer->Stop();
		HttpServer.Reset();
	}

	UE_LOG(LogTemp, Log, TEXT("[BerniEditorBridge] Module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBerniEditorBridgeModule, BerniEditorBridge)
