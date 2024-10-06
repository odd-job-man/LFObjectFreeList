#pragma once
#include <cstdint>
#include <windows.h>
#include <optional>
#include "CLockFreeObjectPool.h"
template <typename T>
class CLockFreeQueue
{
private:
	struct Node
	{
		T data_;
		uintptr_t metaNext_;

		Node(uint64_t meta)
			:metaNext_{ CAddressTranslator::GetMetaAddr(meta,(uintptr_t)nullptr) }
		{}

		Node(T&& data, uint64_t meta)
			:data_{ std::forward<T>(data) }, 
			metaNext_{ CAddressTranslator::GetMetaAddr(meta, (uintptr_t)nullptr) }
		{}
	};

	CLockFreeObjectPool<Node, true> nodePool_;
	alignas(64) uintptr_t metaTail_;
	alignas(64) uintptr_t metaHead_;
	alignas(64) uint64_t metaCnt_;

public:
	alignas(64) long num_;
	CLockFreeQueue()
		:metaCnt_{ 0 }, num_{ 0 }
	{
		InterlockedExchange(&num_, 0);
		uint64_t meta = CAddressTranslator::GetCnt(&metaCnt_);
		Node* pDummy = nodePool_.Alloc(meta);
		metaTail_ = metaHead_ = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)pDummy);
	}

	void Enqueue(T data) 
	{
		uint64_t meta = CAddressTranslator::GetCnt(&metaCnt_);
		Node* pNewNode = nodePool_.Alloc(std::move(data), meta);
		uintptr_t newMetaTail = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)pNewNode);

		while (true)
		{
			uintptr_t metaTail = metaTail_;
			Node* pRealTail = (Node*)CAddressTranslator::GetRealAddr(metaTail);
			uintptr_t metaNext = pRealTail->metaNext_;

			if (CAddressTranslator::GetRealAddr(metaNext) != (uintptr_t)nullptr)
			{
				InterlockedCompareExchange(&metaTail_, metaNext, metaTail);
				continue;
			}

			if (InterlockedCompareExchange(&pRealTail->metaNext_, newMetaTail, metaNext) != metaNext)
				continue;

			InterlockedCompareExchange(&metaTail_, newMetaTail, metaTail);
			InterlockedIncrement(&num_);
			return;
		}
	}

	std::optional<T> Dequeue()
	{
		if (InterlockedDecrement(&num_) < 0)
		{
			InterlockedIncrement(&num_);
			return std::nullopt;
		}

		while (true)
		{
			uintptr_t metaTail = metaTail_;
			uintptr_t metaHead = metaHead_;
			Node* pRealHead_ = (Node*)CAddressTranslator::GetRealAddr(metaHead);
			uintptr_t metaNext = pRealHead_->metaNext_;


			if (CAddressTranslator::GetRealAddr(metaNext) == (uintptr_t)nullptr)
				continue;

			if (metaTail == metaHead)
			{
				InterlockedCompareExchange(&metaTail_, metaNext, metaTail);
				continue;
			}

			T retData = ((Node*)CAddressTranslator::GetRealAddr(metaNext))->data_;
			if (InterlockedCompareExchange(&metaHead_, metaNext, metaHead) == metaHead)
			{
				nodePool_.Free(pRealHead_);
				return retData;
			}
		}
	}
};
