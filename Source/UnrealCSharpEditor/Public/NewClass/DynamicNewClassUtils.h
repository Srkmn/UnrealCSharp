#pragma once

#include "CoreMinimal.h"
#include "AddToProjectConfig.h"

struct FProjectContextInfo
{
	FProjectContextInfo(const FString& InProjectName, const FString& InProjectSourcePath)
		: ProjectName(InProjectName)
		  , ProjectSourcePath(InProjectSourcePath)
	{
	}

	FProjectContextInfo(FString&& InProjectName, FString&& InProjectSourcePath)
		: ProjectName(MoveTemp(InProjectName)),
		  ProjectSourcePath(MoveTemp(InProjectSourcePath))
	{
	}

	bool operator==(const FProjectContextInfo& Other) const
	{
		return ProjectName.Equals(Other.ProjectName);
	}

	bool operator==(FProjectContextInfo&& Other) const
	{
		return ProjectName.Equals(Other.ProjectName);
	}

	friend bool operator==(const FString& Lhs, const FProjectContextInfo& Rhs)
	{
		return Lhs.Equals(Rhs.ProjectName);
	}

	friend bool operator==(FString&& Lhs, const FProjectContextInfo& Rhs)
	{
		return Lhs.Equals(Rhs.ProjectName);
	}

	friend bool operator==(const FProjectContextInfo& Lhs, const FString& Rhs)
	{
		return Lhs.ProjectName.Equals(Rhs);
	}

	friend bool operator==(const FProjectContextInfo& Lhs, FString&& Rhs)
	{
		return Lhs.ProjectName.Equals(Rhs);
	}

	friend uint32 GetTypeHash(const FProjectContextInfo& Info)
	{
		return GetTypeHash(Info.ProjectName);
	}

	FString ProjectName;

	FString ProjectSourcePath;
};

class FDynamicNewClassUtils
{
public:
	static void OpenAddDynamicClassToProjectDialog(const FAddToProjectConfig& Config);

	static bool IsValidBaseClassForCreation(const UClass* InClass,
	                                        const TArray<FProjectContextInfo>& InProjectInfoArray);

	DECLARE_DELEGATE_RetVal_OneParam(bool, FDoesClassNeedAPIExportCallback, const FString& /*ClassModuleName*/);
	static bool IsValidBaseClassForCreation_Internal(const UClass* InClass,
	                                                 const FDoesClassNeedAPIExportCallback& InDoesClassNeedAPIExport);

	static TArray<FProjectContextInfo> GetCurrentProjectsInfo();

	static bool IsSelfOrChildOf(const UClass* InClass, const UClass* TargetClass);

	static void GetDynamicClassContent(const UClass* InParentClass, const FString& InNewClassName, FString& OutContent);

	static UClass* GetAncestorClass(const UClass* InParentClass);

	static void AddNamespaceIfUnique(FString& OutContent, const FString& InNamespace);
};
