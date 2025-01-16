#include "NewClass/DynamicNewClassUtils.h"
#include "SDynamicNewClassDialog.h"
#include "SourceCodeNavigation.h"
#include "Blueprint/UserWidget.h"
#include "Common/FUnrealCSharpFunctionLibrary.h"
#include "CoreMacro/Macro.h"
#include "Dynamic/FDynamicClassGenerator.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "DynamicNewClassDialog"

void FDynamicNewClassUtils::OpenAddDynamicClassToProjectDialog(const FAddToProjectConfig& Config)
{
	const auto PendingDynamicClassPath = Config._InitialPath;

	const auto WindowTitle = LOCTEXT("AddCodeWindowHeader_DynamicCppClass", "Add Dynamic C# Class");

	const FVector2D WindowSize = FVector2D(940, 540);

	TSharedRef<SWindow> AddCodeWindow =
		SNew(SWindow)
		.Title(WindowTitle)
		.ClientSize(WindowSize)
		.SizingRule(ESizingRule::FixedSize)
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	TSharedRef<SDynamicNewClassDialog> NewClassDialog =
		SNew(SDynamicNewClassDialog)
		.ParentWindow(AddCodeWindow)
		.Class(Config._ParentClass)
		.ClassViewerFilter(Config._AllowableParents)
		.FeaturedClasses(Config._FeaturedClasses)
		.InitialPath(Config._InitialPath)
		.OnAddedToProject(Config._OnAddedToProject)
		.DefaultClassPrefix(Config._DefaultClassPrefix)
		.DefaultClassName(Config._DefaultClassName);

	AddCodeWindow->SetContent(NewClassDialog);

	TSharedPtr<SWindow> ParentWindow = Config._ParentWindow;
	if (!ParentWindow.IsValid())
	{
		static const FName MainFrameModuleName = "MainFrame";
		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(MainFrameModuleName);
		ParentWindow = MainFrameModule.GetParentWindow();
	}

	if (Config._bModal)
	{
		FSlateApplication::Get().AddModalWindow(AddCodeWindow, ParentWindow);
	}
	else if (ParentWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(AddCodeWindow, ParentWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(AddCodeWindow);
	}
}

bool FDynamicNewClassUtils::IsValidBaseClassForCreation(const UClass* InClass,
                                                        const TArray<FProjectContextInfo>& InProjectInfoArray)
{
	auto DoesClassNeedAPIExport = [&InProjectInfoArray](const FString& InClassProjectName) -> bool
	{
		for (const FProjectContextInfo& ProjectInfo : InProjectInfoArray)
		{
			if (ProjectInfo.ProjectName == InClassProjectName)
			{
				return false;
			}
		}
		return true;
	};

	return IsValidBaseClassForCreation_Internal(
		InClass, FDoesClassNeedAPIExportCallback::CreateLambda(DoesClassNeedAPIExport));
}

bool FDynamicNewClassUtils::IsValidBaseClassForCreation_Internal(const UClass* InClass,
                                                                 const FDoesClassNeedAPIExportCallback&
                                                                 InDoesClassNeedAPIExport)
{
	// You may not make native classes based on blueprint generated classes
	const bool bIsBlueprintClass = (InClass->ClassGeneratedBy != nullptr);

	// UObject is special cased to be extensible since it would otherwise not be since it doesn't pass the API check (intrinsic class).
	const bool bIsExplicitlyUObject = (InClass == UObject::StaticClass());

	// You need API if you are not UObject itself, and you're in a module that was validated as needing API export
	const FString ClassModuleName = InClass->GetOutermost()->GetName().RightChop(FString(TEXT("/Script/")).Len());
	const bool bNeedsAPI = !bIsExplicitlyUObject && InDoesClassNeedAPIExport.Execute(ClassModuleName);

	// You may not make a class that is not DLL exported.
	// MinimalAPI classes aren't compatible with the DLL export macro, but can still be used as a valid base
	const bool bHasAPI = InClass->HasAnyClassFlags(CLASS_RequiredAPI) || InClass->HasAnyClassFlags(CLASS_MinimalAPI);

	// @todo should we support interfaces?
	const bool bIsInterface = InClass->IsChildOf(UInterface::StaticClass());

	return !bIsBlueprintClass && (!bNeedsAPI || bHasAPI) && !bIsInterface;
}

TArray<FProjectContextInfo> FDynamicNewClassUtils::GetCurrentProjectsInfo()
{
	TArray CustomProjectsName = {FUnrealCSharpFunctionLibrary::GetGameName()};

	CustomProjectsName.Append(FUnrealCSharpFunctionLibrary::GetCustomProjectsName());

	TArray<FProjectContextInfo> CustomProjectsInfo;

	for (const auto& ProjectName : CustomProjectsName)
	{
		CustomProjectsInfo.Add(FProjectContextInfo(ProjectName,
		                                           FUnrealCSharpFunctionLibrary::GetFullScriptDirectory() /
		                                           ProjectName));
	}

	return CustomProjectsInfo;
}

bool FDynamicNewClassUtils::IsSelfOrChildOf(const UClass* InClass, const UClass* TargetClass)
{
	if (!InClass || !TargetClass)
	{
		return false;
	}

	return InClass == TargetClass || InClass->IsChildOf(TargetClass);
}

void FDynamicNewClassUtils::GetDynamicClassContent(const UClass* InParentClass, const FString& InNewClassName,
                                                   FString& OutContent)
{
	OutContent.Empty();

	if (InParentClass == nullptr)
	{
		return;
	}

	const auto AncestorClass = GetAncestorClass(InParentClass);

	if (AncestorClass == nullptr)
	{
		return;
	}

	const bool bIsCppParentClass = InParentClass->IsNative() && !
		FDynamicClassGenerator::IsDynamicBlueprintGeneratedClass(
			InParentClass);

	const FString ParentNamespace = FUnrealCSharpFunctionLibrary::GetClassNameSpace(InParentClass);

	const FString ParentClassName = (bIsCppParentClass ? InParentClass->GetPrefixCPP() : TEXT("")) + InParentClass->
		GetName();

	const FString GeneratedClassName = FString::Printf(TEXT(
		"%s%s"
	),
	                                                   bIsCppParentClass && !InNewClassName.EndsWith(TEXT("_C"))
		                                                   ? InParentClass->GetPrefixCPP()
		                                                   : TEXT(""),
	                                                   *InNewClassName);

	const auto TemplateFileName = FUnrealCSharpFunctionLibrary::GetPluginTemplateDynamicFileName(
		AncestorClass);

	FFileHelper::LoadFileToString(OutContent, *TemplateFileName);

	AddNamespaceIfUnique(OutContent, ParentNamespace);

	OutContent.ReplaceInline(*FString::Printf(TEXT(
		                         ": %s"
	                         ),
	                                          *FUnrealCSharpFunctionLibrary::GetFullClass(AncestorClass)),
	                         *FString::Printf(TEXT(
		                         ": %s"
	                         ),
	                                          *ParentClassName));

	const FString OldClassFullName = FString::Printf(TEXT(
		"%sMy%s"
	),
	                                                 AncestorClass->GetPrefixCPP(),
	                                                 *AncestorClass->GetName());

	OutContent.ReplaceInline(*FString::Printf(TEXT(
		                         "class %s"
	                         ),
	                                          *OldClassFullName),
	                         *FString::Printf(TEXT(
		                         "class %s"
	                         ),
	                                          *GeneratedClassName));

	OutContent.ReplaceInline(*FString::Printf(TEXT(
		                         "public %s()"
	                         ),
	                                          *OldClassFullName),
	                         *FString::Printf(TEXT(
		                         "public %s()"
	                         ),
	                                          *GeneratedClassName));
}

UClass* FDynamicNewClassUtils::GetAncestorClass(const UClass* InParentClass)
{
	if (!InParentClass)
	{
		return nullptr;
	}

	static TArray TemplateClasses =
	{
		AActor::StaticClass(),
		UActorComponent::StaticClass(),
		UUserWidget::StaticClass(),
		UObject::StaticClass()
	};

	for (const auto& TemplateClass : TemplateClasses)
	{
		if (IsSelfOrChildOf(InParentClass, TemplateClass))
		{
			return TemplateClass;
		}
	}

	return nullptr;
}

void FDynamicNewClassUtils::AddNamespaceIfUnique(FString& OutContent, const FString& InNamespace)
{
	if (OutContent.IsEmpty() || InNamespace.IsEmpty())
	{
		return;
	}

	TSet<FString> Namespaces;

	TArray<FString> Lines;

	const FString Using = TEXT("using");

	const FString NameSpace = TEXT("namespace");

	OutContent.ParseIntoArrayLines(Lines, false);

	for (const FString& Line : Lines)
	{
		if (FString TrimmedLine = Line.TrimStartAndEnd(); TrimmedLine.StartsWith(Using))
		{
			FString Path = TrimmedLine
			               .RightChop(Using.Len())
			               .LeftChop(1)
			               .TrimStartAndEnd();

			if (Path != DYNAMIC_CLASS_NAMESPACE_PLACEHOLDER)
			{
				Namespaces.Add(Path);
			}
		}
		else if (TrimmedLine.StartsWith(NameSpace))
		{
			const FString Path = TrimmedLine
			                     .RightChop(NameSpace.Len())
			                     .TrimStartAndEnd();
			Namespaces.Add(Path);

			break;
		}
	}

	if (const bool bIsUnique = !Namespaces.Contains(InNamespace); bIsUnique)
	{
		OutContent.ReplaceInline(
			*FString::Printf(TEXT(
				"%s %s;"
			),
			                 *Using,
			                 *DYNAMIC_CLASS_NAMESPACE_PLACEHOLDER),
			*FString::Printf(TEXT(
				"%s %s;"
			),
			                 *Using,
			                 *InNamespace));
	}
	else
	{
		OutContent.ReplaceInline(
			*FString::Printf(TEXT(
				"%s %s;%s"
			),
			                 *Using,
			                 *DYNAMIC_CLASS_NAMESPACE_PLACEHOLDER,
			                 LINE_TERMINATOR),
			TEXT("")
		);
	}
}

FText FNewClassInfo::GetClassName() const
{
	switch (ClassType)
	{
	case EClassType::UObject:
		return BaseClass ? BaseClass->GetDisplayNameText() : FText::GetEmpty();

	case EClassType::EmptyCpp:
		return LOCTEXT("NoParentClass", "None");

	case EClassType::SlateWidget:
		return LOCTEXT("SlateWidgetParentClass", "Slate Widget");

	case EClassType::SlateWidgetStyle:
		return LOCTEXT("SlateWidgetStyleParentClass", "Slate Widget Style");

	case EClassType::UInterface:
		return LOCTEXT("UInterfaceParentClass", "Unreal Interface");

	default:
		break;
	}

	return FText::GetEmpty();
}

FText FNewClassInfo::GetClassDescription(const bool bFullDescription/* = true*/) const
{
	switch (ClassType)
	{
	case EClassType::UObject:
		{
			if (BaseClass)
			{
				FString ClassDescription = BaseClass->GetToolTipText(/*bShortTooltip=*/!bFullDescription).ToString();

				if (!bFullDescription)
				{
					int32 FullStopIndex = 0;
					if (ClassDescription.FindChar('.', FullStopIndex))
					{
						// Only show the first sentence so as not to clutter up the UI with a detailed description of implementation details
						ClassDescription.LeftInline(FullStopIndex + 1, false);
					}

					// Strip out any new-lines in the description
					ClassDescription.ReplaceInline(TEXT("\n"), TEXT(" "), ESearchCase::CaseSensitive);
				}

				return FText::FromString(ClassDescription);
			}
		}
		break;

	case EClassType::EmptyCpp:
		return LOCTEXT("EmptyClassDescription", "An empty C++ class with a default constructor and destructor.");

	case EClassType::SlateWidget:
		return LOCTEXT("SlateWidgetClassDescription", "A custom Slate widget, deriving from SCompoundWidget.");

	case EClassType::SlateWidgetStyle:
		return LOCTEXT("SlateWidgetStyleClassDescription",
		               "A custom Slate widget style, deriving from FSlateWidgetStyle, along with its associated UObject wrapper class.");

	case EClassType::UInterface:
		return LOCTEXT("UInterfaceClassDescription",
		               "A UObject Interface class, to be implemented by other UObject-based classes.");

	default:
		break;
	}

	return FText::GetEmpty();
}

FString FNewClassInfo::GetClassPrefixCPP() const
{
	switch (ClassType)
	{
	case EClassType::UObject:
		return BaseClass ? BaseClass->GetPrefixCPP() : TEXT("U");

	case EClassType::EmptyCpp:
		return TEXT("F");

	case EClassType::SlateWidget:
		return TEXT("S");

	case EClassType::SlateWidgetStyle:
		return TEXT("F");

	case EClassType::UInterface:
		return TEXT("U");

	default:
		break;
	}
	return TEXT("");
}

FString FNewClassInfo::GetClassNameCPP() const
{
	switch (ClassType)
	{
	case EClassType::UObject:
		return BaseClass ? BaseClass->GetName() : TEXT("");

	case EClassType::EmptyCpp:
		return TEXT("");

	case EClassType::SlateWidget:
		return TEXT("CompoundWidget");

	case EClassType::SlateWidgetStyle:
		return TEXT("SlateWidgetStyle");

	case EClassType::UInterface:
		return TEXT("Interface");

	default:
		break;
	}
	return TEXT("");
}

FString FNewClassInfo::GetBaseClassHeaderFilename() const
{
	FString IncludePath;

	switch (ClassType)
	{
	case EClassType::UObject:
		if (BaseClass)
		{
			FString ClassHeaderPath;
			if (FSourceCodeNavigation::FindClassHeaderPath(BaseClass, ClassHeaderPath) && IFileManager::Get().
				FileSize(*ClassHeaderPath) != INDEX_NONE)
			{
				return ClassHeaderPath;
			}
		}
		break;

	case EClassType::SlateWidget:
	case EClassType::SlateWidgetStyle:
		GetIncludePath(IncludePath);
		return FPaths::EngineDir() / TEXT("Source") / TEXT("Runtime") / TEXT("SlateCore") / TEXT("Public") /
			IncludePath;
	default:
		return FString();
	}

	return FString();
}

bool FNewClassInfo::GetIncludePath(FString& OutIncludePath) const
{
	switch (ClassType)
	{
	case EClassType::UObject:
		if (BaseClass && BaseClass->HasMetaData(TEXT("IncludePath")))
		{
			OutIncludePath = BaseClass->GetMetaData(TEXT("IncludePath"));
			return true;
		}
		break;

	case EClassType::SlateWidget:
		OutIncludePath = "Widgets/SCompoundWidget.h";
		return true;

	case EClassType::SlateWidgetStyle:
		OutIncludePath = "Styling/SlateWidgetStyle.h";
		return true;

	default:
		break;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
