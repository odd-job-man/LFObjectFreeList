#pragma once
#include <cstdint>
#include <windows.h>
#include <optional>
#include "CAddressTranslator.h"
#define QUEUE
#include "CTlsObjectPool.h"


template <typename T>
class CLockFreeQueue
{
private:
	// tail->next를 갱신할때 지역 Tail이 다른 큐에 들어가잇는걸 확인하기 위해 DOUBLE CAS를 해야하고 이를 위해 기존의 metaNext에 식별자를 추가한 구조체를 만듬
	struct QNodePtr
	{
		uint64_t identifier_;
		uintptr_t metaAddr_;
		QNodePtr() {}
		QNodePtr(uint64_t identifier, uintptr_t metaAddr)
			:identifier_{ identifier }, metaAddr_{ metaAddr }{}
	};

	struct Node
	{
		T data_;
		alignas(16) QNodePtr next_;

#pragma warning(disable : 26495)
		Node(uint64_t identifier, uint64_t meta)
			:next_{ identifier, CAddressTranslator::GetMetaAddr(meta,(uintptr_t)nullptr) }
		{}
#pragma warning(default: 26495)

		Node(T&& data, uint64_t identifier, uint64_t meta)
			:data_{ std::forward<T>(data) },
			next_{ identifier,CAddressTranslator::GetMetaAddr(meta,(uintptr_t)nullptr) }
		{}
	};

	static inline CTlsObjectPool<Node, true> nodePool_;
	static inline uint64_t identifier = 0;

	const uint64_t identifier_;
	alignas(64) uintptr_t metaTail_;
	alignas(64) uintptr_t metaHead_;
	alignas(64) uint64_t metaCnt_;
public:
	alignas(64) long num_;
	CLockFreeQueue()
		:metaCnt_{ 0 }, identifier_{ InterlockedIncrement(&identifier) - 1 }
	{
		InterlockedExchange(&num_, 0);
		uint64_t meta = CAddressTranslator::GetCnt(&metaCnt_);
		Node* pDummy = nodePool_.Alloc(identifier_, meta);
		metaTail_ = metaHead_ = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)pDummy);
	}

	// meta가 붙으면 상위 17비트가 조작된 값
	// real이 붙으면 상위 17비트를 제거한 값
	void Enqueue(T data) 
	{
		using namespace CAddressTranslator;
		// 이번에 사용할 상위 17비트값 만들기
		uint64_t meta = GetCnt(&metaCnt_);

		// 새로운 노드 : next의 상위 17비트값의 초기화와 node의 T타입 생성자도 호출함
		Node* pNewNode = nodePool_.Alloc(std::move(data), identifier_, meta);
		QNodePtr newTail{ identifier_,GetMetaAddr(meta,(uintptr_t)pNewNode) };
		while (true)
		{
			uintptr_t metaTail = metaTail_;
			Node* pRealTail = (Node*)GetRealAddr(metaTail);
			QNodePtr nextOfTail = pRealTail->next_;

			if (GetRealAddr(nextOfTail.metaAddr_) != (uintptr_t)nullptr)
			{
				InterlockedCompareExchange(&metaTail_, nextOfTail.metaAddr_, metaTail);
				continue;
			}

			if (ExtractMetaCnt(metaTail) != ExtractMetaCnt(nextOfTail.metaAddr_))
				continue;

			if (identifier_ != nextOfTail.identifier_)
				continue;

			// 1번 CAS
			// 바로 위의 if문으로 인해서 pRealTail이 재활용 되었다면 metaNext는 이미 재활용 되기 이전값임을 보장한다 따라서 재활용된경우 무조건 실패한다.
			if (InterlockedCompareExchange128((LONG64*)&pRealTail->next_, newTail.metaAddr_, newTail.identifier_, (LONG64*)&nextOfTail) == FALSE)
				continue;

			// 2번 CAS 77번째 라인의 if문안의 CAS 떄문에 실패할수 잇으며 이를통해 스핀락이 아니게됨
			InterlockedCompareExchange(&metaTail_, newTail.metaAddr_, metaTail);
			InterlockedIncrement(&num_);
			return;
		}
	}

	std::optional<T> Dequeue()
	{
		using namespace CAddressTranslator;

		if (InterlockedDecrement(&num_) < 0)
		{
			InterlockedIncrement(&num_);
			return std::nullopt;
		}

		while (true)
		{
			uintptr_t metaTail = metaTail_;
			uintptr_t metaHead = metaHead_;
			Node* pRealHead = (Node*)GetRealAddr(metaHead);
			QNodePtr nextOfHead = pRealHead->next_;

			if (GetRealAddr(nextOfHead.metaAddr_) == (uintptr_t)nullptr)
				continue;

			if (metaTail == metaHead)
			{
				InterlockedCompareExchange(&metaTail_, nextOfHead.metaAddr_, metaTail);
				continue;
			}

			T retData = ((Node*)GetRealAddr(nextOfHead.metaAddr_))->data_;
			if (InterlockedCompareExchange(&metaHead_, nextOfHead.metaAddr_, metaHead) == metaHead)
			{
				nodePool_.Free(pRealHead);
				return retData;
			}
		}
	}

	__forceinline const long GetSize() const
	{
		return num_;
	}
};
