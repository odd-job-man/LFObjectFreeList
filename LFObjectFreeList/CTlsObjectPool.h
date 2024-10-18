#pragma once
#include <utility>
#include <new>
#include "CAddressTranslator.h"
#include "MultithreadProfiler.h"


template<typename T,BOOL bPlacementNew>
struct Bucket
{
	static constexpr int size = 1;
	struct NODE
	{
		T data;
		Bucket* pBucket;
	};

	// placementNew �� Malloc���� ������� Cnt,MetaNext�� �ʱ�ȭ�Ѵ�
	// bPlamentNew�� false�̸鼭 Malloc�ǰ� ���� ������ �ʾѴٸ� ��� ��忡 ���� ���� 0���� �⺻ ������ ȣ��
	// bPlacementNew�� true��� �����ڴ� �Ź� ȣ��������ϱ⿡ ���⼭�� ȣ�����
	template<bool isMalloced>
	inline void Init()
	{
		AllocCnt_ = FreeCnt_ = 0;
		metaNext = 0;
		for (int i = 0; i < size; ++i)
		{
			nodeArr_[i].pBucket = this;
			if constexpr (isMalloced && !bPlacementNew)
			{
				new(&nodeArr_[i].data)T{};
			}
		}
	}

	static inline Bucket* DataToBucket(T* pData)
	{
		return *(Bucket**)((char*)pData + offsetof(NODE, pBucket) - offsetof(NODE, data));
	}

	// ��Ŷ���� Node�� �Ҵ�ǥ���ϰ� ��ȯ�Ѵ�
	// �ش� �Լ� ȣ��� ���� ��Ŷ�ȿ� ���̻� �Ҵ��� ��尡 ���ԵǸ� Out parameter�� true�� ��������.
	inline NODE* AllocNode(bool* pbOutMustClearTls)
	{
		NODE* pRet = &nodeArr_[AllocCnt_++];
		if (AllocCnt_ == size)
		{
			*pbOutMustClearTls = true;
		}
		else
		{
			*pbOutMustClearTls = false;
		}
		return pRet;
	}

	// ��Ŷ�� Node�� FREEǥ���Ѵ�
	// �ش� �Լ� ȣ��� ���� ��Ŷ���� ��尡 ��� FREE�� ���¶�� Out Parameter�� true�� ��������. 
	inline bool RETURN_NODE_TO_BUCKET_AND_CHECK_BUCKET_HAVETO_FREE()
	{
		if (_InterlockedIncrement(&FreeCnt_) == size)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	NODE nodeArr_[size];
	long AllocCnt_;
	long FreeCnt_;
	uintptr_t metaNext;
};


#ifdef STACK
template<typename T, bool bPlacementNew>
class CTlsObjectPool
{
private:
	using Bucket = Bucket<T, bPlacementNew>;
	DWORD dwTlsIdx_;

	alignas(64) uintptr_t metaTop_;
	alignas(64) long capacity_;
	alignas(64) long size_;
	alignas(64) size_t metaCnt_;


	// �������� Tls�� ���� �Ҵ� Ȥ�� �ٽἭ ��Ŷ�� ���ٸ� ������ ���ñ����� ��Ŷ�� �Ҵ�޴´�.
	Bucket* AllocBucket()
	{
		uintptr_t metaTop;
		Bucket* pRealTop;
		uintptr_t newMetaTop;

		do
		{
			metaTop = metaTop_;
			if (!metaTop)
			{
				InterlockedIncrement(&capacity_);
				Bucket* pBucket = (Bucket*)malloc(sizeof(Bucket));
				pBucket->Init<true>();
				return pBucket;
			}
			pRealTop = (Bucket*)CAddressTranslator::GetRealAddr(metaTop);
			newMetaTop = pRealTop->metaNext;
		} while (InterlockedCompareExchange(&metaTop_, newMetaTop, metaTop) != metaTop);

		pRealTop->Init<false>();
		InterlockedDecrement(&size_);
		return pRealTop;
	}

public:
	CTlsObjectPool()
		:metaTop_{ 0 }, capacity_{ 0 }, size_{ 0 }
	{
		dwTlsIdx_ = TlsAlloc();
		if (dwTlsIdx_ == TLS_OUT_OF_INDEXES)
		{
			DWORD dwErrCode = GetLastError();
			__debugbreak();
		}
	}

	template<typename... Types> requires (bPlacementNew || (sizeof...(Types) == 0))
	T* Alloc(Types&&... args)
	{
		Bucket* pBucket = (Bucket*)TlsGetValue(dwTlsIdx_);
		if (!pBucket)
		{
			pBucket = AllocBucket();
			TlsSetValue(dwTlsIdx_, pBucket);
		}

		bool bMustFreeBucket;
		auto* pNode = pBucket->AllocNode(&bMustFreeBucket);
		if (bMustFreeBucket)
		{
			TlsSetValue(dwTlsIdx_, nullptr);
		}
		if constexpr (bPlacementNew)
		{
			new(&pNode->data)T{ std::forward<Types>(args)... };
		}
		return &pNode->data;
	}


	void Free(T* pData)
	{
		uintptr_t metaTop;
		Bucket* pBucket = Bucket::DataToBucket(pData);

		if constexpr (bPlacementNew)
		{
			pData->~T();
		}

		if (!pBucket->RETURN_NODE_TO_BUCKET_AND_CHECK_BUCKET_HAVETO_FREE())
		{
			return;
		}

		uintptr_t newMetaTop = CAddressTranslator::GetMetaAddr
		(
			CAddressTranslator::GetCnt(&metaCnt_),
			(uintptr_t)pBucket
		);

		do
		{
			metaTop = metaTop_;
			pBucket->metaNext = metaTop;
		} while (InterlockedCompareExchange(&metaTop_, newMetaTop, metaTop) != metaTop);

		InterlockedIncrement(&size_);
	}

};
#endif

#ifdef QUEUE
template<typename T, bool bPlacementNew>
class CTlsObjectPool
{
	using Bucket = Bucket<T, bPlacementNew>;
private:
	DWORD dwTlsIdx_;
	alignas(64) uintptr_t metaTail_;
	alignas(64) uintptr_t metaHead_;
	alignas(64) uint64_t metaCnt_;
	alignas(64) long capacity_;
	alignas(64) long size_;

	// �������� Tls�� ���� �Ҵ� Ȥ�� �ٽἭ ��Ŷ�� ���ٸ� ������ ���ñ����� ��Ŷ�� �Ҵ�޴´�.

	// meta�� ������ ���� 17��Ʈ�� ���۵� ��
	// real�� ������ ���� 17��Ʈ�� ������ ��
	Bucket* AllocBucket()
	{
		// ���� ������ť ����� ���� ��ŶǮ�� ����ִٸ� ���� ��Ŷ�� �����Ҵ��Ѵ�
		if (InterlockedDecrement(&size_) < 0)
		{
			InterlockedIncrement(&size_);
			Bucket* pBucket = (Bucket*)malloc(sizeof(Bucket));
			pBucket->Init<true>();
			InterlockedIncrement(&capacity_);
			return pBucket;
		}

		// ������ť ����� ���� ��ŶǮ�� ������� �ʴٸ� ������ť���� ��Ŷ�� Deq�ؿ´�.
		while (true)
		{
			uintptr_t metaTail = metaTail_;
			uintptr_t metaHead = metaHead_;
			Bucket* pBucket = (Bucket*)CAddressTranslator::GetRealAddr(metaHead);
			uintptr_t metaNext = pBucket->metaNext;

			if (CAddressTranslator::GetRealAddr(metaNext) == (uintptr_t)nullptr)
				continue;

			if (metaTail == metaHead)
			{
				InterlockedCompareExchange(&metaTail_, metaNext, metaTail);
				continue;
			}

			if (InterlockedCompareExchange(&metaHead_, metaNext, metaHead) == metaHead)
			{
				pBucket->Init<false>();
				return pBucket;
			}
		}
	}

public:
#pragma warning(disable : 6011)
	CTlsObjectPool()
		:metaCnt_{ 0 }, capacity_{ 1 }, size_{ 0 }
	{
		dwTlsIdx_ = TlsAlloc();
		if (dwTlsIdx_ == TLS_OUT_OF_INDEXES)
		{
			DWORD dwErrCode = GetLastError();
			__debugbreak();
		}
		// ������ ť ����̹Ƿ� ���� ��Ŷ�� �����Ҵ��ϰ� head_ == Tail_ �� �ɴ´�
		uint64_t meta = CAddressTranslator::GetCnt(&metaCnt_);
		Bucket* pDummyBucket = (Bucket*)malloc(sizeof(Bucket));
		pDummyBucket->Init<true>();
		pDummyBucket->metaNext = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)nullptr);
		metaTail_ = metaHead_ = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)pDummyBucket);
	}
#pragma warning(default : 6011)

	template<typename... Types> requires (bPlacementNew || (sizeof...(Types) == 0))
	T* Alloc(Types&&... args)
	{
		Bucket* pBucket = (Bucket*)TlsGetValue(dwTlsIdx_);
		if (!pBucket)
		{
			pBucket = AllocBucket();
			TlsSetValue(dwTlsIdx_, pBucket);
		}

		bool bMustFreeBucket;
		auto* pNode = pBucket->AllocNode(&bMustFreeBucket);
		if (bMustFreeBucket)
		{
			TlsSetValue(dwTlsIdx_, nullptr);
		}
		if constexpr (bPlacementNew)
		{
			new(&pNode->data)T{ std::forward<Types>(args)... };
		}
		return &pNode->data;
	}


	void Free(T* pData)
	{
		Bucket* pBucket = Bucket::DataToBucket(pData);
		if constexpr (bPlacementNew)
		{
			pData->~T();
		}

		if (!pBucket->RETURN_NODE_TO_BUCKET_AND_CHECK_BUCKET_HAVETO_FREE())
		{
			return;
		}

		// �� �������� ��Ŷ�� ������ ť ����� ���� ���Ǯ�� ��ȯ(Enqueue �Ǳ�� ������)
		// �̹��� ����� ���� 17��Ʈ�� �����
		uint64_t meta = CAddressTranslator::GetCnt(&metaCnt_);

		// ���ο� ��Ŷ�� ���� 17��Ʈ �ּҸ� ������ meta�ּҸ� ����� ��Ŷ�� next�� ���� ����17��Ʈ�� ���� ��Ÿ�ּҷ� �����
		uintptr_t newMetaTail = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)pBucket);
		pBucket->metaNext = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)nullptr);
		while (true)
		{
			uintptr_t metaTail = metaTail_;
			Bucket* pRealTail = (Bucket*)CAddressTranslator::GetRealAddr(metaTail);
			uintptr_t metaNext = pRealTail->metaNext;

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
			if (InterlockedCompareExchange(&pRealTail->metaNext, newMetaTail, metaNext) != metaNext)
				continue;

			// 2�� CAS 54��° ������ if������ CAS ������ �����Ҽ� ������ �̸����� ���ɶ��� �ƴϰԵ�
			InterlockedCompareExchange(&metaTail_, newMetaTail, metaTail);
			InterlockedIncrement(&size_);
			return;
		}
	}
};
#endif
