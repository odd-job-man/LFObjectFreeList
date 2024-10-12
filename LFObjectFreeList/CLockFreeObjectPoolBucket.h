#pragma once
#include <utility>
#include <new>
#include "CAddressTranslator.h"

template<typename T, bool bPlacementNew>
class TlsLockFreeObjectPool
{
private:

	struct FreeListNode
	{
		TlsLockFreeObjectPool* pBucket_;
		T data_;
	};

	struct FreeListBucket
	{
		static constexpr int ElementCnt = 100;
		FreeListNode nodeArr_[ElementCnt];
		TlsLockFreeObjectPool* pObjectPool_;
		int AllocCnt_;

		FreeListBucket(TlsLockFreeObjectPool* pPool)
			:AllocCnt_{ 0 }, pObjectPool_{ pPool }
		{}

		__forceinline FreeListNode* Alloc()
		{
			if (AllocCnt_ == ElementCnt)
			{
				return nullptr;
			}
			else
			{
				++AllocCnt_;
				return nodeArr_ + AllocCnt_;
			}
		}
	};

	alignas(64) uintptr_t metaTop_;
	alignas(64) long capacity_;
	alignas(64) long size_;
	alignas(64) size_t metaCnt_;
	FreeListBucket* pBucket_;

public:
	TlsLockFreeObjectPool()
		:metaTop_{ 0 }, capacity_{ 0 }, size_{ 0 }, pBucket_{ nullptr }
	{
		InterlockedExchange(&metaCnt_, 0);
	}

	template<typename... Types> requires (bPlacementNew || (sizeof...(Types) == 0))
	T* Alloc(Types&&... args)
	{
		uintptr_t metaTop;
		FreeListNode* pRealTop;
		uintptr_t newMetaTop;

		do
		{
			metaTop = metaTop_;
			if (!metaTop)
			{
				InterlockedIncrement(&capacity_);
				FreeListBucket* pBucket = new FreeListBucket{ this };
				FreeListNode* pNode = pBucket->Alloc();
				new(&pNode->data_)T{ std::forward<Types>(args)... };
				pNode->pBucket_ = pBucket;
				return &pNode->data_;
			}
			pRealTop = (FreeListNode*)CAddressTranslator::GetRealAddr(metaTop);
			newMetaTop = pRealTop->metaNext;
		} while (InterlockedCompareExchange(&metaTop_, newMetaTop, metaTop) != metaTop);

	}
};
