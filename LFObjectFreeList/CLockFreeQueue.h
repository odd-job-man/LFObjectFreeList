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

	// meta�� ������ ���� 17��Ʈ�� ���۵� ��
	// real�� ������ ���� 17��Ʈ�� ������ ��
	void Enqueue(T data) 
	{
		// �̹��� ����� ���� 17��Ʈ�� �����
		uint64_t meta = CAddressTranslator::GetCnt(&metaCnt_);

		// ���ο� ��� : next�� ���� 17��Ʈ���� �ʱ�ȭ�� node�� TŸ�� �����ڵ� ȣ����
		Node* pNewNode = nodePool_.Alloc(std::move(data), meta);

		// ���ο� ����� ���� 17��Ʈ �ּ�
		uintptr_t newMetaTail = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)pNewNode);

		while (true)
		{
			uintptr_t metaTail = metaTail_;
			Node* pRealTail = (Node*)CAddressTranslator::GetRealAddr(metaTail);
			uintptr_t metaNext = pRealTail->metaNext_;

			// tail->next�� null�̾ƴϸ� 2�� CAS�� ���ؼ� �ű�� continue
			// ���� �Ʒ��� 1��ī���� metaNext�� ���� 47��Ʈ�� ��� 0��
			if (CAddressTranslator::GetRealAddr(metaNext) != (uintptr_t)nullptr)
			{
				InterlockedCompareExchange(&metaTail_, metaNext, metaTail);
				continue;
			}

			// pRealTail�� �ʱ�ȭ�ǰ� �ٸ������尡 pRealTail�� ����Ű�� ��带 ���Ҵ����� pRealTail->metaNext_���� �ʱ�ȭ �� �� �ٽ� ���� �����尡 ����� metaNext�� �ʱ�ȭ �Ǿ��ٸ�
			// �Ʒ��� if���� ���ٸ� 1�� CAS�� �����ؼ� ť�� ������� ���� ���� �Ҵ�� ��忡 pNewNode�� �����ϴ� ���� �߻��Ѵ�.
			// �̸� ���� ���ؼ� metaTail�� metaNext�� ���� 17��Ʈ ���� ���ؼ� ���� �ٸ� ��� continue�ϵ�����
			if (CAddressTranslator::ExtractMetaCnt(metaTail) != CAddressTranslator::ExtractMetaCnt(metaNext))
				continue;

			// 1�� CAS
			// �ٷ� ���� if������ ���ؼ� pRealTail�� ��Ȱ�� �Ǿ��ٸ� metaNext�� �̹� ��Ȱ�� �Ǳ� ���������� �����Ѵ� ���� ��Ȱ��Ȱ�� ������ �����Ѵ�.
			if (InterlockedCompareExchange(&pRealTail->metaNext_, newMetaTail, metaNext) != metaNext)
				continue;

			// 2�� CAS 54��° ������ if������ CAS ������ �����Ҽ� ������ �̸����� ���ɶ��� �ƴϰԵ�
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
