#pragma once

#include "CoreMinimal.h"

class UToolMenu;

class FDynamicNewClassContextMenu
{
public:
	DECLARE_DELEGATE_OneParam(FOnNewDynamicClassRequested, const FName&);

	static void MakeContextMenu(
		UToolMenu* InMenu,
		const TArray<FName>& InSelectedClassPaths,
		const FOnNewDynamicClassRequested& InOnNewDynamicClassRequested
	);

private:
	static void ExecuteNewClass(const FName InPath, FOnNewDynamicClassRequested InOnNewDynamicClassRequested);
};
