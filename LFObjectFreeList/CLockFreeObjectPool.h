#pragma once
#include <utility>
#include <new>
#include "CAddressTranslator.h"

template<typename T, bool bPlacementNew>
class CLockFreeObjectPool
{
	struct FreeListNode
	{
		T data;
		uintptr_t metaNext = 0;

		template<typename... Args>
		FreeListNode(Args... args)
			:data{ args... }
		{
		}

		static FreeListNode* DataToNode(T* pData) 
		{
			return (FreeListNode*)((char*)pData - offsetof(FreeListNode, data));
		}

	};

	alignas(64) uintptr_t metaTop_ = 0;
	alignas(64) LONG capacity_ = 0;
	alignas(64) LONG size_ = 0;
	alignas(64) size_t metaAddrCnt = 0;

public:
	template<typename... Types>
	T* Alloc(Types... args)
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
				FreeListNode* pNode = new FreeListNode{ args... };
				return &pNode->data;
			}
			pRealTop = (FreeListNode*)CAddressTranslator::GetRealAddr(metaTop);
			newMetaTop = pRealTop->metaNext;
		} while (InterlockedCompareExchange(&metaTop_, newMetaTop, metaTop) != metaTop);

		if constexpr (bPlacementNew)
		{
			new(&pRealTop->data)T{ args... };
		}

		InterlockedDecrement(&size_);
		return &pRealTop->data;
	}

	void Free(T* pData)
	{
		uintptr_t metaTop;
		FreeListNode* pNewRealTop = FreeListNode::DataToNode(pData);

		if constexpr (bPlacementNew)
		{
			pData->~T();
		}

		uintptr_t newMetaTop = CAddressTranslator::GetMetaAddr
		(
			CAddressTranslator::GetCnt(&metaAddrCnt), 
			(uintptr_t)pNewRealTop
		);


		do
		{
			metaTop = metaTop_;
			pNewRealTop->metaNext = metaTop;
		} while (InterlockedCompareExchange(&metaTop_,newMetaTop,metaTop) != metaTop);

		InterlockedIncrement(&size_);
	}

};
