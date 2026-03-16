// Copyright ReapAndRuin Dev. All Rights Reserved.

#include "BerniAuditLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"

FBerniAuditLog::FBerniAuditLog()
{
	LogFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("BerniEditorBridge.log"));
	UE_LOG(LogTemp, Log, TEXT("[BerniEditorBridge] Audit log: %s"), *LogFilePath);
}

FBerniAuditLog::~FBerniAuditLog()
{
}

void FBerniAuditLog::LogRequest(
	const FString& RequestId,
	const FString& Endpoint,
	const FString& AssetPath,
	const FString& OpsDescription,
	bool bSuccess,
	const FString& ResultMessage)
{
	FString Line = FString::Printf(
		TEXT("[%s] REQ id=%s endpoint=%s asset=%s ops=%s result=%s msg=%s"),
		*FDateTime::UtcNow().ToIso8601(),
		*RequestId,
		*Endpoint,
		*AssetPath,
		*OpsDescription,
		bSuccess ? TEXT("OK") : TEXT("FAIL"),
		*ResultMessage
	);

	WriteToFile(Line);
	UE_LOG(LogTemp, Log, TEXT("[BerniEditorBridge] %s"), *Line);
}

void FBerniAuditLog::LogSession(const FString& Token, const FString& Event)
{
	FString Line = FString::Printf(
		TEXT("[%s] SESSION token=%s event=%s"),
		*FDateTime::UtcNow().ToIso8601(),
		*Token.Left(8), // Only log first 8 chars of token
		*Event
	);

	WriteToFile(Line);
	UE_LOG(LogTemp, Log, TEXT("[BerniEditorBridge] %s"), *Line);
}

void FBerniAuditLog::WriteToFile(const FString& Line)
{
	FScopeLock Lock(&WriteLock);
	FFileHelper::SaveStringToFile(
		Line + TEXT("\n"),
		*LogFilePath,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(),
		FILEWRITE_Append
	);
}
