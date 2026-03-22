// Copyright ReapAndRuin Dev. All Rights Reserved.

#include "EpicEngineAIAccessBridgeModule.h"
#include "BerniHttpServer.h"

#define LOCTEXT_NAMESPACE "FEpicEngineAIAccessBridgeModule"

void FEpicEngineAIAccessBridgeModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Module starting up."));

	HttpServer = MakeShared<FBerniHttpServer>();
	HttpServer->Start();
}

void FEpicEngineAIAccessBridgeModule::ShutdownModule()
{
	if (HttpServer)
	{
		HttpServer->Stop();
		HttpServer.Reset();
	}

	UE_LOG(LogTemp, Log, TEXT("[EpicEngineAIAccessBridge] Module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FEpicEngineAIAccessBridgeModule, EpicEngineAIAccessBridge)
