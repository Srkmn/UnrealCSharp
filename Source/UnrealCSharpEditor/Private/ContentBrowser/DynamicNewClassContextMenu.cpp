#include "ContentBrowser/DynamicNewClassContextMenu.h"

#include "ToolMenus.h"
#include "UnrealCSharpEditorStyle.h"

#define LOCTEXT_NAMESPACE "ContentBrowserClassDataSource"

void FDynamicNewClassContextMenu::MakeContextMenu(UToolMenu* InMenu, const TArray<FName>& InSelectedClassPaths,
                                                  const FOnNewDynamicClassRequested& InOnNewDynamicClassRequested)
{
	if (InSelectedClassPaths.Num() == 0)
	{
		return;
	}

	const FName FirstSelectedPath = InSelectedClassPaths[0];
	const bool bHasSinglePathSelected = InSelectedClassPaths.Num() == 1;

	auto CanExecuteClassActions = [bHasSinglePathSelected]() -> bool
	{
		return bHasSinglePathSelected;
	};
	const FCanExecuteAction CanExecuteClassActionsDelegate =
		FCanExecuteAction::CreateLambda(CanExecuteClassActions);

	if (InOnNewDynamicClassRequested.IsBound())
	{
		FName ClassCreationPath = FirstSelectedPath;
		FText NewClassToolTip;
		if (bHasSinglePathSelected)
		{
			NewClassToolTip = FText::Format(
				LOCTEXT("NewClassTooltip_CreateIn", "Create a new dynamic class in {0}."),
				FText::FromName(ClassCreationPath));
		}
		else
		{
			NewClassToolTip = LOCTEXT("NewClassTooltip_SelectedInvalidNumberOfPaths",
			                          "Can only create classes when there is a single path selected.");
		}

		{
			FToolMenuSection& Section = InMenu->AddSection("ContentBrowserNewClass",
			                                               LOCTEXT("ClassMenuHeading", "New Dynamic Class"));
			Section.AddMenuEntry(
				"NewClass",
				LOCTEXT("NewClassLabel", "New dynamic Class..."),
				NewClassToolTip,
				FSlateIcon(FUnrealCSharpEditorStyle::GetStyleSetName(), "UnrealCSharpEditor.PluginAction"),
				FUIAction(
					FExecuteAction::CreateStatic(&FDynamicNewClassContextMenu::ExecuteNewClass, ClassCreationPath,
					                             InOnNewDynamicClassRequested),
					CanExecuteClassActionsDelegate
				)
			);
		}
	}
}

void FDynamicNewClassContextMenu::ExecuteNewClass(const FName InPath,
                                                  FOnNewDynamicClassRequested InOnNewDynamicClassRequested)
{
	InOnNewDynamicClassRequested.ExecuteIfBound(InPath);
}

#undef LOCTEXT_NAMESPACE
