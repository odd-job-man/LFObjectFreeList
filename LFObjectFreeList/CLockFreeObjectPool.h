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
		uintptr_t metaNext;

		template<typename... Types>
		FreeListNode(Types&&... args)
			:data{ std::forward<Types>(args)... }, metaNext{ 0 }
		{
		}

		static inline FreeListNode* DataToNode(T* pData) 
		{
			return (FreeListNode*)((char*)pData - offsetof(FreeListNode, data));
		}
	};

	alignas(64) uintptr_t metaTop_;
	alignas(64) long capacity_;
	alignas(64) long size_;
	alignas(64) size_t metaCnt;

public:
	CLockFreeObjectPool() 
		:metaTop_{ 0 }, capacity_{ 0 }, size_{ 0 }
	{
		InterlockedExchange(&metaCnt, 0);
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
				FreeListNode* pNode = new FreeListNode{ std::forward<Types>(args)... };
				return &pNode->data;
			}
			pRealTop = (FreeListNode*)CAddressTranslator::GetRealAddr(metaTop);
			newMetaTop = pRealTop->metaNext;
		} while (InterlockedCompareExchange(&metaTop_, newMetaTop, metaTop) != metaTop);

		if constexpr (bPlacementNew)
		{
			new(&pRealTop->data)T{ std::forward<Types>(args)... };
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
			CAddressTranslator::GetCnt(&metaCnt), 
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
