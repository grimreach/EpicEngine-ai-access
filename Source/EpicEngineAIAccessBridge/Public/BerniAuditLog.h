// Copyright ReapAndRuin Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Audit logger for EpicEngineAIAccessBridge.
 * Writes structured log lines to Saved/Logs/EpicEngineAIAccessBridge.log
 */
class BERNIEDITORBRIDGE_API FBerniAuditLog
{
public:
	FBerniAuditLog();
	~FBerniAuditLog();

	/** Log an API request and its result. */
	void LogRequest(
		const FString& RequestId,
		const FString& Endpoint,
		const FString& AssetPath,
		const FString& OpsDescription,
		bool bSuccess,
		const FString& ResultMessage
	);

	/** Log a session event. */
	void LogSession(const FString& Token, const FString& Event);

private:
	void WriteToFile(const FString& Line);

	FString LogFilePath;
	FCriticalSection WriteLock;
};
