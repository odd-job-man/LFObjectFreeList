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
	// tail->next�� �����Ҷ� ���� Tail�� �ٸ� ť�� ���մ°� Ȯ���ϱ� ���� DOUBLE CAS�� �ؾ��ϰ� �̸� ���� ������ metaNext�� �ĺ��ڸ� �߰��� ����ü�� ����
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

	// meta�� ������ ���� 17��Ʈ�� ���۵� ��
	// real�� ������ ���� 17��Ʈ�� ������ ��
	void Enqueue(T data) 
	{
		using namespace CAddressTranslator;
		// �̹��� ����� ���� 17��Ʈ�� �����
		uint64_t meta = GetCnt(&metaCnt_);

		// ���ο� ��� : next�� ���� 17��Ʈ���� �ʱ�ȭ�� node�� TŸ�� �����ڵ� ȣ����
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

			// 1�� CAS
			// �ٷ� ���� if������ ���ؼ� pRealTail�� ��Ȱ�� �Ǿ��ٸ� metaNext�� �̹� ��Ȱ�� �Ǳ� ���������� �����Ѵ� ���� ��Ȱ��Ȱ�� ������ �����Ѵ�.
			if (InterlockedCompareExchange128((LONG64*)&pRealTail->next_, newTail.metaAddr_, newTail.identifier_, (LONG64*)&nextOfTail) == FALSE)
				continue;

			// 2�� CAS 77��° ������ if������ CAS ������ �����Ҽ� ������ �̸����� ���ɶ��� �ƴϰԵ�
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
