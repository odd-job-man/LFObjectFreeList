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

	// meta가 붙으면 상위 17비트가 조작된 값
	// real이 붙으면 상위 17비트를 제거한 값
	void Enqueue(T data) 
	{
		// 이번에 사용할 상위 17비트값 만들기
		uint64_t meta = CAddressTranslator::GetCnt(&metaCnt_);

		// 새로운 노드 : next의 상위 17비트값의 초기화와 node의 T타입 생성자도 호출함
		Node* pNewNode = nodePool_.Alloc(std::move(data), meta);

		// 새로운 노드의 상위 17비트 주소
		uintptr_t newMetaTail = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)pNewNode);

		while (true)
		{
			uintptr_t metaTail = metaTail_;
			Node* pRealTail = (Node*)CAddressTranslator::GetRealAddr(metaTail);
			uintptr_t metaNext = pRealTail->metaNext_;

			// tail->next가 null이아니면 2번 CAS를 통해서 옮기고 continue
			// 따라서 아래의 1번카스의 metaNext의 하위 47비트는 모두 0임
			if (CAddressTranslator::GetRealAddr(metaNext) != (uintptr_t)nullptr)
			{
				InterlockedCompareExchange(&metaTail_, metaNext, metaTail);
				continue;
			}

			// pRealTail만 초기화되고 다른스레드가 pRealTail이 가리키는 노드를 재할당한후 pRealTail->metaNext_까지 초기화 된 후 다시 원래 스레드가 깨어나서 metaNext가 초기화 되었다면
			// 아래의 if문이 없다면 1번 CAS가 성공해서 큐에 연결되지 않은 새로 할당된 노드에 pNewNode를 연결하는 일이 발생한다.
			// 이를 막기 위해서 metaTail과 metaNext의 상위 17비트 값을 비교해서 만약 다른 경우 continue하도록함
			if (CAddressTranslator::ExtractMetaCnt(metaTail) != CAddressTranslator::ExtractMetaCnt(metaNext))
				continue;

			// 1번 CAS
			// 바로 위의 if문으로 인해서 pRealTail이 재활용 되었다면 metaNext는 이미 재활용 되기 이전값임을 보장한다 따라서 재활용된경우 무조건 실패한다.
			if (InterlockedCompareExchange(&pRealTail->metaNext_, newMetaTail, metaNext) != metaNext)
				continue;

			// 2번 CAS 54번째 라인의 if문안의 CAS 떄문에 실패할수 잇으며 이를통해 스핀락이 아니게됨
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

	__forceinline const int long GetSize() const
	{
		return num_;
	}
};
