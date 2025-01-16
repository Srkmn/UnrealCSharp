#pragma once

#include "CoreMinimal.h"

struct FDynamicClassViewerNode
{
	FString AssetName;

	FString ObjectPath;

	FDynamicClassViewerNode(const FString& InAssetName, const FString& InObjectPath = TEXT(""))
		: AssetName(InAssetName)
		  , ObjectPath(InObjectPath)
	{
	}

	UClass* GetClass() const
	{
		UClass* FindClass = FindObject<UClass>(ANY_PACKAGE, *AssetName);

		if (FindClass != nullptr)
		{
			return FindClass;
		}

		if (const auto Blueprint = LoadObject<
			UBlueprint>(nullptr, *ObjectPath))
		{
			if (Blueprint->GeneratedClass)
			{
				FindClass = Cast<UClass>(Blueprint->GeneratedClass);

				return FindClass;
			}
		}

		return nullptr;
	}

	bool operator==(const FDynamicClassViewerNode& Other) const  
	{  
		return AssetName == Other.AssetName;  
	}

	friend uint32 GetTypeHash(const FDynamicClassViewerNode& Node)  
	{  
		return GetTypeHash(Node.AssetName); 
	}  
};

DECLARE_DELEGATE_OneParam(FOnDynamicClassNodePicked, TSharedPtr<FDynamicClassViewerNode>);

class SDynamicClassViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDynamicClassViewer)
			: _OnDynamicClassNodePicked()
		{
		}

		SLATE_EVENT(FOnDynamicClassNodePicked, OnDynamicClassNodePicked)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

	/** 刷新数据源 */
	void RefreshTreeItems(const TArray<TSharedPtr<FDynamicClassViewerNode>>& InItems);

	bool ValidateClass(const FString& InClassName) const;

private:
	/** 生成列表行 */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FDynamicClassViewerNode> Item,
	                                    const TSharedRef<STableViewBase>& OwnerTable);
	
	/** 处理选中变化 */
	void OnSelectionChanged(TSharedPtr<FDynamicClassViewerNode> Item,
	                        ESelectInfo::Type SelectType);

	/** 搜索框文本变化 */
	void OnSearchTextChanged(const FText& InSearchText);

	/** 搜索框提交 */
	void OnSearchTextCommitted(const FText& InSearchText, ETextCommit::Type CommitType);

	/** 更新过滤后的列表 */
	void UpdateFilteredItems();

	FText GetItemCountText() const;

private:
	/** UI组件 */
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<TSharedPtr<FDynamicClassViewerNode>>> ListView;

	/** 数据 */
	TSharedPtr<class FDynamicClassHierarchy> ClassHierarchy;
	TSharedPtr<class FTextFilterExpressionEvaluator> TextFilter;
	TArray<TSharedPtr<FDynamicClassViewerNode>> AllItems;
	TArray<TSharedPtr<FDynamicClassViewerNode>> FilteredItems;
	FText SearchText;
	int32 TotalItemCount = 0;

	/** 委托 */
	FOnDynamicClassNodePicked OnDynamicClassNodePickedDelegate;
};
