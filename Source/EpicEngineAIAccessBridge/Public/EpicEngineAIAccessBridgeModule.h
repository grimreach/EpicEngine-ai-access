// Copyright ReapAndRuin Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FBerniHttpServer;

class FEpicEngineAIAccessBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<FBerniHttpServer> HttpServer;
};
