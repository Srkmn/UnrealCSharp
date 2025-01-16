#include "SDynamicClassViewer.h"

#include "WidgetBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Common/FUnrealCSharpFunctionLibrary.h"
#include "Dynamic/FDynamicClassGenerator.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "Setting/UnrealCSharpEditorSetting.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"

class FDynamicClassHierarchy : public TSharedFromThis<FDynamicClassHierarchy>
{
public:
	FDynamicClassHierarchy()
	{
		PopulateClassHierarchy();
	}

	TArray<TSharedPtr<FDynamicClassViewerNode>>& GetAllNodes() { return AllNodes; }

	bool ValidateNode(const FString& InClassName) const
	{
		return AllNodes.FindByPredicate(
			[InClassName](const TSharedPtr<FDynamicClassViewerNode>& Node)
			{
				return Node.IsValid() && Node->AssetName == InClassName;
			}) != nullptr;
	}

private:
	void PopulateClassHierarchy()
	{
		AllNodes.Empty();

		PopulateNativeClass(AllNodes);

		PopulateBlueprintClass(AllNodes);

		PopulateDynamicClass(AllNodes);
	}

	static void PopulateNativeClass(TArray<TSharedPtr<FDynamicClassViewerNode>>& OutNodes)
	{
		TArray<UClass*> ValidClasses;

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* CurrentClass = *ClassIt;

			if (!IsNativeClassValid(CurrentClass))
			{
				continue;
			}

			ValidClasses.Add(CurrentClass);
		}

		TArray<UClass*> FilterClasses;

		if (const auto UnrealCSharpEditorSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
			UUnrealCSharpEditorSetting>())
		{
			if (!UnrealCSharpEditorSetting->IsGenerateAllModules())
			{
				auto SupportedModules = UnrealCSharpEditorSetting->GetSupportedModule();

				if (const int32 Index = SupportedModules.Find(FApp::GetProjectName()); Index != INDEX_NONE)
				{
					SupportedModules[Index] = TEXT("Game");
				}

				for (auto ClassIt = ValidClasses.CreateIterator(); ClassIt; ++ClassIt)
				{
					if (const auto CurrentClass = *ClassIt; SupportedModules.Contains(
						FUnrealCSharpFunctionLibrary::GetModuleName(CurrentClass)))
					{
						FilterClasses.Add(CurrentClass);
					}
				}
			}
			else
			{
				FilterClasses = ValidClasses;
			}
		}

		for (auto ClassIt = FilterClasses.CreateConstIterator(); ClassIt; ++ClassIt)
		{
			const auto CurrentClass = *ClassIt;

			TSharedPtr<FDynamicClassViewerNode> NewNode = MakeShared<FDynamicClassViewerNode>(
				CurrentClass->GetName(),
				CurrentClass->GetPathName()
			);

			OutNodes.Add(NewNode);
		}
	}

	static void PopulateBlueprintClass(TArray<TSharedPtr<FDynamicClassViewerNode>>& OutNodes)
	{
		if (const auto UnrealCSharpEditorSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
			UUnrealCSharpEditorSetting>())
		{
			if (UnrealCSharpEditorSetting->IsGenerateAsset())
			{
				const auto& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
					TEXT("AssetRegistry"));

				TArray<FAssetData> OutAssetData;
				TArray<FName> AssetPaths;

				for (auto AssetPath : UnrealCSharpEditorSetting->GetSupportedAssetPath())
				{
					AssetPath = AssetPath == FApp::GetProjectName() ? TEXT("Game") : AssetPath;
					AssetPaths.Add(*FString::Printf(TEXT(
						"/%s"
					),
					                                *AssetPath));
				}

				AssetRegistryModule.Get().GetAssetsByPaths(AssetPaths, OutAssetData, true);

				for (const auto& AssetData : OutAssetData)
				{
					if (const auto AssetDataClass = AssetData.GetClass())
					{
						TArray<FName> SupportedAssetClassNames;

						for (auto SupportedAssetClass : UnrealCSharpEditorSetting->GetSupportedAssetClass())
						{
							SupportedAssetClassNames.Add(SupportedAssetClass->GetFName());
						}

						if (const auto& AssetDataClassName = AssetDataClass->GetFName();
							SupportedAssetClassNames.Contains(AssetDataClassName))
						{
							if (AssetDataClassName == UBlueprint::StaticClass()->GetFName() ||
								AssetDataClassName == UWidgetBlueprint::StaticClass()->GetFName())
							{
								TSharedPtr<FDynamicClassViewerNode> NewNode = MakeShared<FDynamicClassViewerNode>(
									AssetData.AssetName.ToString(),
									AssetData.ObjectPath.ToString()
								);
								OutNodes.Add(NewNode);
							}
						}
					}
				}
			}
		}
	}

	static void PopulateDynamicClass(TArray<TSharedPtr<FDynamicClassViewerNode>>& OutNodes)
	{
		const auto DynamicClasses = FDynamicClassGenerator::GetDynamicClasses();
		
		for (const auto ClassIt : DynamicClasses)
		{
			TSharedPtr<FDynamicClassViewerNode> NewNode = MakeShared<FDynamicClassViewerNode>(
				ClassIt->GetName(),
				ClassIt->GetPathName()
			);

			OutNodes.Add(NewNode);
		}
	}

	static bool IsNativeClassValid(const UClass* InClass)
	{
		if (!InClass)
		{
			return false;
		}

		// 忽略废弃的和临时的类  
		if (InClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden))
		{
			return false;
		}

		// 忽略蓝图骨架类  
		if (FKismetEditorUtilities::IsClassABlueprintSkeleton(InClass))
		{
			return false;
		}

		const bool bIsBlueprintClass = (InClass->ClassGeneratedBy != nullptr);

		const bool bIsExplicitlyUObject = (InClass == UObject::StaticClass());

		const FString ClassModuleName = InClass->GetOutermost()->GetName().RightChop(FString(TEXT("/Script/")).Len());
		const bool bNeedsAPI = !bIsExplicitlyUObject /*&& InDoesClassNeedAPIExport.Execute(ClassModuleName)*/;

		const bool bHasAPI = InClass->HasAnyClassFlags(CLASS_RequiredAPI) || InClass->
			HasAnyClassFlags(CLASS_MinimalAPI);

		const bool bIsInterface = InClass->IsChildOf(UInterface::StaticClass());

		return !bIsBlueprintClass && (!bNeedsAPI || bHasAPI) && !bIsInterface;
	}

private:
	TArray<TSharedPtr<FDynamicClassViewerNode>> AllNodes;
};

void SDynamicClassViewer::Construct(const FArguments& InArgs)
{
	OnDynamicClassNodePickedDelegate = InArgs._OnDynamicClassNodePicked;

	ClassHierarchy = MakeShared<FDynamicClassHierarchy>();

	AllItems = ClassHierarchy->GetAllNodes();

	TextFilter = MakeShared<FTextFilterExpressionEvaluator>(ETextFilterExpressionEvaluatorMode::BasicString);

	AllItems.Sort([](const TSharedPtr<FDynamicClassViewerNode>& A, const TSharedPtr<FDynamicClassViewerNode>& B)
	{
		return A->AssetName.Compare(B->AssetName, ESearchCase::IgnoreCase) < 0;
	});

	FilteredItems = AllItems;

	SearchText = FText::GetEmpty();

	ChildSlot
	[
		SNew(SVerticalBox)

		// 搜索框  
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SAssignNew(SearchBox, SSearchBox)
			.OnTextChanged(this, &SDynamicClassViewer::OnSearchTextChanged)
			.OnTextCommitted(this, &SDynamicClassViewer::OnSearchTextCommitted)
		]

		// 列表视图  
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(ListView, SListView<TSharedPtr<FDynamicClassViewerNode>>)
			.ListItemsSource(&FilteredItems)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SDynamicClassViewer::OnGenerateRow)
			.OnSelectionChanged(this, &SDynamicClassViewer::OnSelectionChanged)
			.ItemHeight(24.0f)
		]

		// 计数器显示（移到底部）  
		+ SVerticalBox::Slot()
		  .AutoHeight()
		  .Padding(5) // 增加一些内边距  
		  .HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Text(this, &SDynamicClassViewer::GetItemCountText)
		]
	];
}

void SDynamicClassViewer::RefreshTreeItems(const TArray<TSharedPtr<FDynamicClassViewerNode>>& InItems)
{
	AllItems = InItems;
	UpdateFilteredItems();
}

bool SDynamicClassViewer::ValidateClass(const FString& InClassName) const
{
	if (ClassHierarchy.IsValid())
	{
		return ClassHierarchy->ValidateNode(InClassName);
	}

	return false;
}

TSharedRef<ITableRow> SDynamicClassViewer::OnGenerateRow(
	TSharedPtr<FDynamicClassViewerNode> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	FSlateFontInfo FontInfo = FAppStyle::Get().GetFontStyle("PropertyWindow.NormalFont");

	FontInfo.Size = 10;

	return SNew(STableRow<TSharedPtr<FDynamicClassViewerNode>>, OwnerTable)
		.Padding(FMargin(2.0f))
		.ShowSelection(true)
		.SignalSelectionMode(ETableRowSignalSelectionMode::Instantaneous)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(5.0f, 0.0f)
			[
				SNew(STextBlock)
				                .Text(FText::FromString(Item->AssetName))
				                .HighlightText(SearchText)
				                .Font(FontInfo)
				                .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f))
			]
		];
}

void SDynamicClassViewer::OnSelectionChanged(TSharedPtr<FDynamicClassViewerNode> Item,
                                             ESelectInfo::Type SelectType)
{
	if (Item.IsValid() && OnDynamicClassNodePickedDelegate.IsBound())
	{
		if (ListView.IsValid())  
		{
			FSlateApplication::Get().SetKeyboardFocus(ListView.ToSharedRef());  
		}
		
		OnDynamicClassNodePickedDelegate.Execute(Item);
	}
}

void SDynamicClassViewer::OnSearchTextChanged(const FText& InSearchText)
{
	SearchText = InSearchText;
	TextFilter->SetFilterText(InSearchText);
	UpdateFilteredItems();

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SDynamicClassViewer::OnSearchTextCommitted(const FText& InSearchText, ETextCommit::Type CommitType)
{
	SearchText = InSearchText;

	UpdateFilteredItems();

	if (ListView.IsValid())  
	{
		FSlateApplication::Get().SetKeyboardFocus(ListView.ToSharedRef());  
	}  
}

void SDynamicClassViewer::UpdateFilteredItems()
{
	const FString SearchString = SearchText.ToString();

	FilteredItems.Empty();

	if (SearchString.IsEmpty())
	{
		FilteredItems = AllItems;
	}
	else
	{
		for (const auto& Item : AllItems)
		{
			const FTextFilterString FilterString(Item->AssetName);
			if (TextFilter->TestTextFilter(FBasicStringFilterExpressionContext(Item->AssetName)))
			{
				FilteredItems.Add(Item);
			}
		}
	}

	// 使用忽略大小写的字典序排序  
	FilteredItems.Sort([](const TSharedPtr<FDynamicClassViewerNode>& A, const TSharedPtr<FDynamicClassViewerNode>& B)
	{
		return A->AssetName.Compare(B->AssetName, ESearchCase::IgnoreCase) < 0;
	});

	if (ListView.IsValid())
	{
		ListView->RebuildList();
	}
}

FText SDynamicClassViewer::GetItemCountText() const
{
	return FText::Format(NSLOCTEXT("ClassViewer", "ItemCount", "Items Count: {0}"),
	                     FText::AsNumber(FilteredItems.Num()));
}
