#include "SDynamicNewClassDialog.h"

#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Framework/Docking/TabManager.h"
#include "EditorStyleSet.h"
#include "Engine/Blueprint.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Interfaces/IProjectManager.h"
#include "SourceCodeNavigation.h"
#include "ClassViewerModule.h"
#include "ClassViewer/Public/ClassViewerFilter.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "SClassViewer.h"
#include "DesktopPlatformModule.h"
#include "IDocumentation.h"
#include "EditorClassUtils.h"
#include "UObject/UObjectHash.h"
#include "Widgets/Workflow/SWizard.h"
#include "Widgets/Input/SHyperlink.h"
#include "TutorialMetaData.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistryModule.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "FeaturedClasses.inl"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "ClassIconFinder.h"
#include "SWarningOrErrorBox.h"
#include "GameProjectGeneration/Public/GameProjectUtils.h"
#include "Common/FUnrealCSharpFunctionLibrary.h"
#include "CoreMacro/Macro.h"
#include "UnrealCSharpCore/Public/Dynamic/FDynamicClassGenerator.h"
#include "Setting/UnrealCSharpEditorSetting.h"

#define LOCTEXT_NAMESPACE "DynamicNewClassDialog"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

struct FParentClassItem
{
	FNewClassInfo ParentClassInfo;

	FParentClassItem(const FNewClassInfo& InParentClassInfo)
		: ParentClassInfo(InParentClassInfo)
	{
	}
};

void SDynamicNewClassDialog::Construct(const FArguments& InArgs)
{
	const auto& CurrentProjects = FDynamicNewClassUtils::GetCurrentProjectsInfo();

	AvailableProjects.Reserve(CurrentProjects.Num());
	for (const FProjectContextInfo& ProjectInfo : CurrentProjects)
	{
		AvailableProjects.Emplace(MakeShareable(new FProjectContextInfo(ProjectInfo)));
	}

	if (AvailableProjects.IsEmpty())
	{
		return;
	}

	const auto AbsoluteInitialPath = InArgs._InitialPath.IsEmpty()
		                                 ? FUnrealCSharpFunctionLibrary::GetGameDirectory()
		                                 : InArgs._InitialPath;

	NewClassPath = AbsoluteInitialPath;

	for (const auto& AvailableProject : AvailableProjects)
	{
		if (AbsoluteInitialPath.StartsWith(AvailableProject->ProjectSourcePath))
		{
			SelectedProjectInfo = AvailableProject;

			break;
		}
	}

	if (!SelectedProjectInfo.IsValid())
	{
		SelectedProjectInfo = AvailableProjects.Top();
	}

	DefaultClassPrefix = InArgs._DefaultClassPrefix;

	DefaultClassName = InArgs._DefaultClassName;

	NewClassTypeIndex = 0;

	bShowFullClassTree = false;

	LastPeriodicValidityCheckTime = 0;

	PeriodicValidityCheckFrequency = 4;

	bLastInputValidityCheckSuccessful = true;

	bPreventPeriodicValidityChecksUntilNextChange = false;

	DynamicClassViewer = SNew(SDynamicClassViewer)
		.OnDynamicClassNodePicked(this, &SDynamicNewClassDialog::OnClassViewerSelected);

	SetupDefaultCommonParentClassItems();

	UpdateInputValidity();

	constexpr float EditableTextHeight = 26.0f;

	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(
		"ContentBrowser").Get();

	OnAddedToProject = InArgs._OnAddedToProject;

	ChildSlot
	[
		SNew(SBorder)
		.Padding(18)
		.BorderImage(FEditorStyle::GetBrush("Docking.Tab.ContentAreaBrush"))
		[
			SNew(SVerticalBox)
			.AddMetaData<FTutorialMetaData>(TEXT("AddCodeMajorAnchor"))

			+ SVerticalBox::Slot()
			[
				SAssignNew(MainWizard, SWizard)

				.ShowPageList(false)
				.CanFinish(this, &SDynamicNewClassDialog::CanFinish)
				.FinishButtonText(LOCTEXT("FinishButtonText_Dynamic", "Create Dynamic Class"))
				.FinishButtonToolTip(
					LOCTEXT("FinishButtonToolTip_Dynamic", "Creates the code files to add your new dynamic class."))
				.OnCanceled(this, &SDynamicNewClassDialog::CancelClicked)
				.OnFinished(this, &SDynamicNewClassDialog::FinishClicked)
				.InitialPageIndex(0)
				+ SWizard::Page()
				.CanShow(true)
				[
					SNew(SVerticalBox)

					// Title
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0)
					[
						SNew(STextBlock)

						.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
						.Text(LOCTEXT("ParentClassTitle", "Choose Parent Class"))
						.TransformPolicy(ETextTransformPolicy::ToUpper)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(SSegmentedControl<bool>)

						.OnValueChanged(this, &SDynamicNewClassDialog::OnFullClassTreeChanged)
						.Value(this, &SDynamicNewClassDialog::IsFullClassTreeShown)
						+ SSegmentedControl<bool>::Slot(false)
						.Text(LOCTEXT("CommonClasses", "Common Classes"))
						+ SSegmentedControl<bool>::Slot(true)
						.Text(LOCTEXT("AllClasses", "All Classes"))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 10)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ChooseParentClassDescription_Dynamic",
						              "This will add a dynamic class file to your game project."))
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					.Padding(0, 10)
					[
						SNew(SBorder)
						.AddMetaData<FTutorialMetaData>(TEXT("AddCodeOptions"))
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							[
								SAssignNew(ParentClassListView, SListView< TSharedPtr<FParentClassItem> >)
								.ListItemsSource(&ParentClassItemsSource)
								.SelectionMode(ESelectionMode::Single)
								.ClearSelectionOnClick(false)
								.OnGenerateRow(this, &SDynamicNewClassDialog::MakeParentClassListViewWidget)
								.OnMouseButtonDoubleClick(this, &SDynamicNewClassDialog::OnParentClassItemDoubleClicked)
								.OnSelectionChanged(this, &SDynamicNewClassDialog::OnCommonClassSelected)
								.Visibility(this, &SDynamicNewClassDialog::GetBasicParentClassVisibility)
							]

							+ SVerticalBox::Slot()
							[
								SNew(SBox)
								.Visibility(this, &SDynamicNewClassDialog::GetAdvancedParentClassVisibility)
								[
									DynamicClassViewer.ToSharedRef()
								]
							]
						]
					]

					+ SVerticalBox::Slot()
					.Padding(30, 2)
					.AutoHeight()
					[
						SNew(SGridPanel)
						.FillColumn(1, 1.0f)

						+ SGridPanel::Slot(0, 0)
						.VAlign(VAlign_Center)
						.Padding(2.0f, 2.0f, 10.0f, 2.0f)
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ParentClassLabel", "Selected Class"))
						]

						/*+ SGridPanel::Slot(0, 1)
						.VAlign(VAlign_Center)
						.Padding(2.0f, 2.0f, 10.0f, 2.0f)
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
							.Visibility(EVisibility::Visible)
							.Text(LOCTEXT("ParentClassSourceLabel", "Selected Class Source"))
						]*/

						+ SGridPanel::Slot(1, 0)
						.VAlign(VAlign_Center)
						.Padding(2.0f)
						.HAlign(HAlign_Left)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							.AutoWidth()
							[
								SNew(STextBlock)
								.Text(this, &SDynamicNewClassDialog::GetSelectedParentClassName)
							]
						]

						/*+ SGridPanel::Slot(1, 1)
						.VAlign(VAlign_Center)
						.Padding(2.0f)
						.HAlign(HAlign_Left)
						[
							SNew(SHyperlink)
							.Style(FEditorStyle::Get(), "Common.GotoNativeCodeHyperlink")
							.OnNavigate(this, &SDynamicNewClassDialog::OnEditCodeClicked)
							.Text(this, &SDynamicNewClassDialog::GetSelectedParentClassFilename)
							.ToolTipText(FText::Format(
								LOCTEXT("GoToCode_ToolTip", "Click to open this source file in {0}"),
								FSourceCodeNavigation::GetSelectedSourceCodeIDE()))
							.Visibility(this, &SDynamicNewClassDialog::GetSourceHyperlinkVisibility)
						]*/
					]
				]

				+ SWizard::Page()
				.OnEnter(this, &SDynamicNewClassDialog::OnNamePageEntered)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0)
					[
						SNew(STextBlock)
						.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
						.Text(this, &SDynamicNewClassDialog::GetNameClassTitle)
						.TransformPolicy(ETextTransformPolicy::ToUpper)
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					.Padding(0, 10)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 5)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ClassNameDescription",
							              "Enter a name for your new class. Class names may only contain alphanumeric characters, and may not contain a space."))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 2)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ClassNameDetails_Blueprint",
							              "When you click the \"Create\" button below, a new dynamic class will be created."))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 5)
						[
							SNew(SWarningOrErrorBox)
							.MessageStyle(EMessageStyle::Error)
							.Visibility(this, &SDynamicNewClassDialog::GetNameErrorLabelVisibility)
							.Message(this, &SDynamicNewClassDialog::GetNameErrorLabelText)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBorder)
							.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
							.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
							.Padding(FMargin(6.0f, 4.0f, 7.0f, 4.0f))
							[
								SNew(SVerticalBox)

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0)
								[
									SNew(SGridPanel)
									.FillColumn(1, 1.0f)

									+ SGridPanel::Slot(0, 0)
									.VAlign(VAlign_Center)
									.Padding(0, 0, 12, 0)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("NewClassType_Label", "Dynamic Class Type"))
									]

									+ SGridPanel::Slot(1, 0)
									.VAlign(VAlign_Center)
									.HAlign(HAlign_Left)
									.Padding(2.0f)
									[
										SNew(SSegmentedControl<int32>)
										.Value_Lambda([this] { return NewClassTypeIndex; })
										.OnValueChanged(this, &SDynamicNewClassDialog::OnNewClassTypeChanged)
										.IsEnabled_Lambda([this]
										{
											if (!SelectedParentClassInfo.IsValid())
											{
												return true;
											}

											return !SelectedParentClassInfo->AssetName.EndsWith(TEXT("_C"));
										})
										+ SSegmentedControl<int32>::Slot(0)
										.Text(LOCTEXT("NewClassType_Cpp", "C++"))
										+ SSegmentedControl<int32>::Slot(1)
										.Text(LOCTEXT("NewClassType_BP", "Blueprint"))
									]

									+ SGridPanel::Slot(1, 0)
									.VAlign(VAlign_Center)
									.HAlign(HAlign_Left)
									.Padding(2.0f)

									+ SGridPanel::Slot(0, 1)
									.VAlign(VAlign_Center)
									.Padding(0, 0, 12, 0)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("NameLabel", "Name"))
									]

									+ SGridPanel::Slot(1, 1)
									.Padding(0.0f, 3.0f)
									.VAlign(VAlign_Center)
									[
										SNew(SBox)
										.HeightOverride(EditableTextHeight)
										.AddMetaData<FTutorialMetaData>(TEXT("ClassName"))
										[
											SNew(SHorizontalBox)

											+ SHorizontalBox::Slot()
											.FillWidth(.7f)
											[
												SAssignNew(ClassNameEditBox, SEditableTextBox)
												.Text(this, &SDynamicNewClassDialog::OnGetClassNameText)
												.OnTextChanged(this, &SDynamicNewClassDialog::OnClassNameTextChanged)
												.OnTextCommitted(
													this, &SDynamicNewClassDialog::OnClassNameTextCommitted)
											]

											+ SHorizontalBox::Slot()
											.AutoWidth()
											.Padding(6.0f, 0.0f, 0.0f, 0.0f)
											[
												SAssignNew(AvailableProjectsCombo,
												           SComboBox<TSharedPtr<FProjectContextInfo>>)
												.Visibility(EVisibility::Visible)
												.ToolTipText(LOCTEXT("ProjectComboToolTip",
												                     "Choose the target project for your new class"))
												.OptionsSource(&AvailableProjects)
												.InitiallySelectedItem(SelectedProjectInfo)
												.OnSelectionChanged(
													this,
													&SDynamicNewClassDialog::SelectedProjectComboBoxSelectionChanged)
												.OnGenerateWidget(
													this, &SDynamicNewClassDialog::MakeWidgetForSelectedModuleCombo)
												[
													SNew(STextBlock)
													.Text(this, &SDynamicNewClassDialog::GetSelectedModuleComboText)
												]
											]
										]
									]

									+ SGridPanel::Slot(0, 2)
									.VAlign(VAlign_Center)
									.Padding(0, 0, 12, 0)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("PathLabel", "Path"))
									]

									+ SGridPanel::Slot(1, 2)
									.Padding(0.0f, 3.0f)
									.VAlign(VAlign_Center)
									[
										SNew(SVerticalBox)

										+ SVerticalBox::Slot()
										.Padding(0)
										.AutoHeight()
										[
											SNew(SBox)
											.Visibility(EVisibility::Visible)
											.HeightOverride(EditableTextHeight)
											.AddMetaData<FTutorialMetaData>(TEXT("Path"))
											[
												SNew(SHorizontalBox)

												+ SHorizontalBox::Slot()
												.FillWidth(1.0f)
												[
													SNew(SEditableTextBox)
													.Text(this, &SDynamicNewClassDialog::OnGetClassPathText)
													.OnTextChanged(
														this, &SDynamicNewClassDialog::OnClassPathTextChanged)
												]

												+ SHorizontalBox::Slot()
												.AutoWidth()
												.Padding(6.0f, 1.0f, 0.0f, 0.0f)
												[
													SNew(SButton)
													.VAlign(VAlign_Center)
													.ButtonStyle(FAppStyle::Get(), "SimpleButton")
													.OnClicked(
														this, &SDynamicNewClassDialog::HandleChooseFolderButtonClicked)
													[
														SNew(SImage)
														.Image(FAppStyle::Get().GetBrush("Icons.FolderClosed"))
														.ColorAndOpacity(FSlateColor::UseForeground())
													]
												]
											]
										]
									]

									+ SGridPanel::Slot(0, 3)
									.VAlign(VAlign_Center)
									.Padding(0, 0, 12, 0)
									[
										SNew(STextBlock)
										.Visibility(EVisibility::Visible)
										.Text(LOCTEXT("ParentClassNameLabel", "Parent Class"))
									]

									+ SGridPanel::Slot(1, 3)
									.Padding(0.0f, 3.0f)
									.VAlign(VAlign_Center)
									[
										SNew(SBox)
										.Visibility(EVisibility::Visible)
										.VAlign(VAlign_Center)
										.HeightOverride(EditableTextHeight)
										[
											SNew(STextBlock)
											.Text(this, &SDynamicNewClassDialog::OnGetParentClassNameText)
										]
									]

									+ SGridPanel::Slot(0, 4)
									.VAlign(VAlign_Center)
									.Padding(0, 0, 12, 0)
									[
										SNew(STextBlock)
										.Visibility(EVisibility::Visible)
										.Text(LOCTEXT("HeaderFileLabel", "Dynamic File Path"))
									]

									+ SGridPanel::Slot(1, 4)
									.Padding(0.0f, 3.0f)
									.VAlign(VAlign_Center)
									[
										SNew(SBox)
										.Visibility(EVisibility::Visible)
										.VAlign(VAlign_Center)
										.HeightOverride(EditableTextHeight)
										[
											SNew(STextBlock)
											.Text(this, &SDynamicNewClassDialog::OnGetDynamicFilePathText)
										]
									]
								]
							]
						]
					]
				]
			]
		]
	];

	// Select the first item
	if (InArgs._Class == nullptr && ParentClassItemsSource.Num() > 0)
	{
		ParentClassListView->SetSelection(ParentClassItemsSource[0], ESelectInfo::Direct);
	}

	TSharedPtr<SWindow> ParentWindow = InArgs._ParentWindow;
	if (ParentWindow.IsValid())
	{
		ParentWindow.Get()->SetWidgetToFocusOnActivate(ParentClassListView);
	}
}

TSharedRef<ITableRow> SDynamicNewClassDialog::MakeParentClassListViewWidget(
	TSharedPtr<FParentClassItem> ParentClassItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!ParentClassItem.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FParentClassItem>>, OwnerTable);
	}

	if (!ParentClassItem->ParentClassInfo.IsSet())
	{
		return SNew(STableRow<TSharedPtr<FParentClassItem>>, OwnerTable);
	}

	const FText ClassName = ParentClassItem->ParentClassInfo.GetClassName();
	const FText ClassFullDescription = ParentClassItem->ParentClassInfo.GetClassDescription(/*bFullDescription*/true);
	const FText ClassShortDescription = ParentClassItem->ParentClassInfo.GetClassDescription(/*bFullDescription*/false);
	const UClass* Class = ParentClassItem->ParentClassInfo.BaseClass;
	const FSlateBrush* const ClassBrush = FClassIconFinder::FindThumbnailForClass(Class);

	constexpr int32 ItemHeight = 64;
	return
			SNew(STableRow<TSharedPtr<FParentClassItem>>, OwnerTable)
			.Padding(4)
			.Style(FEditorStyle::Get(), "NewClassDialog.ParentClassListView.TableRow")
			.ToolTip(IDocumentation::Get()->CreateToolTip(ClassFullDescription, nullptr,
			                                              FEditorClassUtils::GetDocumentationPage(Class),
			                                              FEditorClassUtils::GetDocumentationExcerpt(Class)))
			[
				SNew(SBox)
				.HeightOverride(ItemHeight)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.Padding(8)
					[
						SNew(SBox)
						.HeightOverride(ItemHeight / 2.0f)
						.WidthOverride(ItemHeight / 2.0f)
						[
							SNew(SImage)
							.Image(ClassBrush)
						]
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(4)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						[
							SNew(STextBlock)
							.TextStyle(FAppStyle::Get(), "DialogButtonText")
							.Text(ClassName)
						]

						+ SVerticalBox::Slot()
						[
							SNew(STextBlock)
							.Text(ClassShortDescription)
							.AutoWrapText(true)
						]
					]
				]
			];
}

FText SDynamicNewClassDialog::GetSelectedParentClassName() const
{
	return FText::FromString(SelectedParentClassInfo.IsValid() ? SelectedParentClassInfo->AssetName : TEXT("None"));
}

void SDynamicNewClassDialog::OnParentClassItemDoubleClicked(TSharedPtr<FParentClassItem> TemplateItem)
{
	if (constexpr int32 NamePageIdx = 1; MainWizard->CanShowPage(NamePageIdx))
	{
		MainWizard->ShowPage(NamePageIdx);
	}
}

void SDynamicNewClassDialog::OnCommonClassSelected(TSharedPtr<FParentClassItem> Item, ESelectInfo::Type SelectInfo)
{
	if (Item.IsValid())
	{
		SelectedParentClassInfo = MakeShared<FDynamicClassViewerNode>(
			Item->ParentClassInfo.GetClassName().ToString().Replace(TEXT(" "), TEXT("")));
	}
	else
	{
		SelectedParentClassInfo.Reset();
	}

	UpdateInputValidity();
}

void SDynamicNewClassDialog::OnClassViewerSelected(TSharedPtr<FDynamicClassViewerNode> Node)
{
	if (Node.IsValid())
	{
		ParentClassListView->ClearSelection();

		SelectedParentClassInfo = MakeShared<FDynamicClassViewerNode>(Node->GetClass()->GetName(), Node->ObjectPath);
	}
	else
	{
		SelectedParentClassInfo.Reset();
	}

	UpdateInputValidity();
}

bool SDynamicNewClassDialog::IsFullClassTreeShown() const
{
	return bShowFullClassTree;
}

void SDynamicNewClassDialog::OnFullClassTreeChanged(bool bInShowFullClassTree)
{
	bShowFullClassTree = bInShowFullClassTree;
}

EVisibility SDynamicNewClassDialog::GetBasicParentClassVisibility() const
{
	return bShowFullClassTree ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SDynamicNewClassDialog::GetAdvancedParentClassVisibility() const
{
	return bShowFullClassTree ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SDynamicNewClassDialog::GetNameErrorLabelVisibility() const
{
	return GetNameErrorLabelText().IsEmpty() ? EVisibility::Hidden : EVisibility::Visible;
}

void SDynamicNewClassDialog::OnNewClassTypeChanged(const int32 NewTypeIndex)
{
	NewClassTypeIndex = NewTypeIndex;

	OnNewClassTypeChanged_Internal();
}

FText SDynamicNewClassDialog::GetNameErrorLabelText() const
{
	if (!bLastInputValidityCheckSuccessful)
	{
		return LastInputValidityErrorText;
	}

	return FText::GetEmpty();
}

void SDynamicNewClassDialog::OnNamePageEntered()
{
	NewClassTypeIndex = SelectedParentClassInfo.IsValid()
		                    ? SelectedParentClassInfo->AssetName.EndsWith(
			                      TEXT("_C"))
			                      ? 1
			                      : 0
		                    : 0;

	const FString ParentClassName = SelectedParentClassInfo->AssetName;

	const FString PotentialNewClassName = FString::Printf(TEXT(
		"My%s%s"
	),
	                                                      DefaultClassName.IsEmpty()
		                                                      ? (ParentClassName.IsEmpty()
			                                                         ? TEXT("Class")
			                                                         : *ParentClassName)
		                                                      : *DefaultClassName,
	                                                      NewClassTypeIndex == 1
		                                                      ? ParentClassName.EndsWith(TEXT("_C"))
			                                                        ? TEXT("")
			                                                        : TEXT("_C")
		                                                      : TEXT(""));

	NewClassName = PotentialNewClassName;

	LastAutoGeneratedClassName = PotentialNewClassName;

	UpdateInputValidity();

	FSlateApplication::Get().SetKeyboardFocus(ClassNameEditBox, EFocusCause::SetDirectly);
}

void SDynamicNewClassDialog::OnNewClassTypeChanged_Internal()
{
	static const FString Suffix = TEXT("_C");

	if (NewClassTypeIndex == 1 && !NewClassName.EndsWith(Suffix))
	{
		NewClassName += Suffix;
	}
	else if (NewClassTypeIndex == 0)
	{
		NewClassName.RemoveFromEnd(Suffix);
	}

	UpdateInputValidity();
}

FText SDynamicNewClassDialog::GetNameClassTitle() const
{
	static const FString NoneString = TEXT("None");

	if (const FText ParentClassName = GetSelectedParentClassName(); !ParentClassName.IsEmpty() && ParentClassName.
		ToString() != NoneString)
	{
		return FText::Format(LOCTEXT("NameClassTitle", "Name Your New {0}"), ParentClassName);
	}

	return LOCTEXT("NameClassGenericTitle", "Name Your New Class");
}

FText SDynamicNewClassDialog::OnGetClassNameText() const
{
	return FText::FromString(NewClassName);
}

void SDynamicNewClassDialog::OnClassNameTextChanged(const FText& NewText)
{
	NewClassName = NewText.ToString();
	UpdateInputValidity();
}

void SDynamicNewClassDialog::OnClassNameTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		if (CanFinish())
		{
			FinishClicked();
		}
	}
}

FText SDynamicNewClassDialog::OnGetClassPathText() const
{
	return FText::FromString(NewClassPath);
}

void SDynamicNewClassDialog::OnClassPathTextChanged(const FText& NewText)
{
	NewClassPath = NewText.ToString();

	// If the user has selected a path which matches the root of a known module, then update our selected module to be that module
	for (const auto& AvailableProject : AvailableProjects)
	{
		if (NewClassPath.StartsWith(AvailableProject->ProjectSourcePath))
		{
			SelectedProjectInfo = AvailableProject;
			AvailableProjectsCombo->SetSelectedItem(SelectedProjectInfo);
			break;
		}
	}

	UpdateInputValidity();
}

FText SDynamicNewClassDialog::OnGetParentClassNameText() const
{
	return FText::FromString(SelectedParentClassInfo.IsValid() ? SelectedParentClassInfo->AssetName : DefaultClassName);
}

FText SDynamicNewClassDialog::OnGetDynamicFilePathText() const
{
	return FText::FromString(GetDynamicFileName());
}

FString SDynamicNewClassDialog::GetDynamicFileName() const
{
	return NewClassPath / NewClassName + CSHARP_SUFFIX;
}

void SDynamicNewClassDialog::CancelClicked()
{
	CloseContainingWindow();
}

bool SDynamicNewClassDialog::CanFinish() const
{
	return bLastInputValidityCheckSuccessful;
}

void SDynamicNewClassDialog::FinishClicked()
{
	UpdateInputValidity();

	if (CanFinish())
	{
		if (const auto ParentClass = SelectedParentClassInfo->GetClass(); ParentClass)
		{
			FString NewClassContent;

			FDynamicNewClassUtils::GetDynamicClassContent(ParentClass, NewClassName, NewClassContent);

			FUnrealCSharpFunctionLibrary::SaveStringToFile(*GetDynamicFileName(), NewClassContent);

			CloseContainingWindow();

			FSourceCodeNavigation::OpenSourceFile(GetDynamicFileName());

			
		}
	}
}

FReply SDynamicNewClassDialog::HandleChooseFolderButtonClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		void* ParentWindowWindowHandle = ParentWindow.IsValid()
			                                 ? ParentWindow->GetNativeWindow()->GetOSWindowHandle()
			                                 : nullptr;

		FString FolderName;
		const FString Title = LOCTEXT("NewClassBrowseTitle", "Choose a dynamic class source location").ToString();
		const bool bFolderSelected = DesktopPlatform->OpenDirectoryDialog(
			ParentWindowWindowHandle,
			Title,
			NewClassPath,
			FolderName
		);

		if (bFolderSelected)
		{
			if (!FolderName.EndsWith(TEXT("/")))
			{
				FolderName += TEXT("/");
			}

			NewClassPath = FolderName;

			// If the user has selected a path which matches the root of a known module, then update our selected module to be that module
			for (const auto& AvailableProject : AvailableProjects)
			{
				if (NewClassPath.StartsWith(AvailableProject->ProjectSourcePath))
				{
					SelectedProjectInfo = AvailableProject;
					AvailableProjectsCombo->SetSelectedItem(SelectedProjectInfo);
					break;
				}
			}

			UpdateInputValidity();
		}
	}

	return FReply::Handled();
}

FText SDynamicNewClassDialog::GetSelectedModuleComboText() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("ModuleName"), FText::FromString(SelectedProjectInfo->ProjectName));
	return FText::Format(LOCTEXT("ModuleComboEntry", "{ModuleName}"), Args);
}

void SDynamicNewClassDialog::SelectedProjectComboBoxSelectionChanged(TSharedPtr<FProjectContextInfo> Value,
                                                                     ESelectInfo::Type SelectInfo)
{
	SelectedProjectInfo = Value;

	NewClassPath = SelectedProjectInfo->ProjectSourcePath;

	UpdateInputValidity();
}

TSharedRef<SWidget> SDynamicNewClassDialog::MakeWidgetForSelectedModuleCombo(TSharedPtr<FProjectContextInfo> Value)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("ModuleName"), FText::FromString(Value->ProjectName));
	return SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("ModuleComboEntry", "{ModuleName}"), Args));
}

void SDynamicNewClassDialog::UpdateInputValidity()
{
	bLastInputValidityCheckSuccessful = true;

	bLastInputValidityCheckSuccessful = GameProjectUtils::IsValidClassNameForCreation(
		NewClassName, LastInputValidityErrorText);

	if (bLastInputValidityCheckSuccessful)
	{
		if (DynamicClassViewer->ValidateClass(NewClassName))
		{
			bLastInputValidityCheckSuccessful = false;

			FFormatNamedArguments Args;

			Args.Add(TEXT("NewClassName"), FText::FromString(NewClassName));

			LastInputValidityErrorText = FText::Format(
				LOCTEXT("ValidClassNameError", "The name {NewClassName} is already used by another class."), Args);

			return;
		}

		bool bIsStartWithProjectDirectory = false;

		for (const auto& AvailableProject : AvailableProjects)
		{
			if (NewClassPath.StartsWith(AvailableProject->ProjectSourcePath))
			{
				bIsStartWithProjectDirectory = true;
			}
		}

		if (!bIsStartWithProjectDirectory)
		{
			bLastInputValidityCheckSuccessful = false;

			LastInputValidityErrorText = LOCTEXT("SelectedPathNotProjectDirectory",
			                                     "The selected path is not a project or a custom project path.");

			return;
		}

		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*OnGetDynamicFilePathText().ToString()))
		{
			bLastInputValidityCheckSuccessful = false;

			LastInputValidityErrorText = FText::Format(
				LOCTEXT("DynamicClassAlreadyExists", "An dynamic class called {0} already exists in {1}."),
				FText::FromString(NewClassName), FText::FromString(NewClassPath));

			return;
		}

		if (SelectedParentClassInfo.IsValid() && SelectedParentClassInfo->GetClass() == nullptr)
		{
			bLastInputValidityCheckSuccessful = false;

			LastInputValidityErrorText = FText::Format(
				LOCTEXT("InvalidParentClass", "Invalid parent class {0}."),
				FText::FromString(SelectedParentClassInfo->AssetName));

			return;
		}

		if (!SelectedParentClassInfo.IsValid())
		{
			bLastInputValidityCheckSuccessful = false;

			LastInputValidityErrorText = LOCTEXT("NotSelectedParentClass",
			                                     "Please select a parent class for your new class.");

			return;
		}
	}

	LastPeriodicValidityCheckTime = FSlateApplication::Get().GetCurrentTime();

	bPreventPeriodicValidityChecksUntilNextChange = false;
}

void SDynamicNewClassDialog::SetupDefaultCommonParentClassItems()
{
	TArray<FNewClassInfo> DefaultFeaturedClasses;

	DefaultFeaturedClasses.Add(FNewClassInfo(FNewClassInfo::EClassType::EmptyCpp));

	DefaultFeaturedClasses.Append(FFeaturedClasses::ActorClasses());

	DefaultFeaturedClasses.Add(FNewClassInfo(UActorComponent::StaticClass()));

	DefaultFeaturedClasses.Add(FNewClassInfo(USceneComponent::StaticClass()));

	for (const auto& Featured : DefaultFeaturedClasses)
	{
		ParentClassItemsSource.Add(MakeShareable(new FParentClassItem(Featured)));
	}
}

void SDynamicNewClassDialog::CloseContainingWindow()
{
	if (const TSharedPtr<SWindow> ContainingWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		ContainingWindow.IsValid())
	{
		ContainingWindow->RequestDestroyWindow();
	}
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FReply SDynamicNewClassDialog::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		// Pressing Escape returns as if the user clicked Cancel
		CancelClicked();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		// Pressing Enter move to the next page like a double-click or the Next button
		OnParentClassItemDoubleClicked(TSharedPtr<FParentClassItem>());
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SDynamicNewClassDialog::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
                                  const float InDeltaTime)
{
	if (!bPreventPeriodicValidityChecksUntilNextChange && (InCurrentTime > LastPeriodicValidityCheckTime +
		PeriodicValidityCheckFrequency))
	{
		UpdateInputValidity();
	}
}

#undef LOCTEXT_NAMESPACE
