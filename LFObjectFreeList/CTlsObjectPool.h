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

	// placementNew 와 Malloc여부 관계없이 Cnt,MetaNext를 초기화한다
	// bPlamentNew가 false이면서 Malloc되고 아직 사용되지 않앗다면 모든 노드에 대해 인자 0개의 기본 생성자 호출
	// bPlacementNew가 true라면 생성자는 매번 호출해줘야하기에 여기서는 호출안함
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

	// 버킷에서 Node를 할당표시하고 반환한다
	// 해당 함수 호출로 인해 버킷안에 더이상 할당할 노드가 없게되면 Out parameter로 true를 내보낸다.
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

	// 버킷에 Node를 FREE표시한다
	// 해당 함수 호출로 인해 버킷안의 노드가 모두 FREE된 상태라면 Out Parameter로 true를 내보낸다. 
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


	// 스레드의 Tls에 최초 할당 혹은 다써서 버킷이 없다면 락프리 스택구조로 버킷을 할당받는다.
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

	// 스레드의 Tls에 최초 할당 혹은 다써서 버킷이 없다면 락프리 스택구조로 버킷을 할당받는다.

	// meta가 붙으면 상위 17비트가 조작된 값
	// real이 붙으면 상위 17비트를 제거한 값
	Bucket* AllocBucket()
	{
		// 현재 락프리큐 방식의 전역 버킷풀이 비어있다면 새로 버킷을 동적할당한다
		if (InterlockedDecrement(&size_) < 0)
		{
			InterlockedIncrement(&size_);
			Bucket* pBucket = (Bucket*)malloc(sizeof(Bucket));
			pBucket->Init<true>();
			InterlockedIncrement(&capacity_);
			return pBucket;
		}

		// 락프리큐 방식의 전역 버킷풀이 비어있지 않다면 락프리큐에서 버킷을 Deq해온다.
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
		// 락프리 큐 방식이므로 더미 버킷을 동적할당하고 head_ == Tail_ 에 꽃는다
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

		// 이 시점에서 버킷은 락프리 큐 방식의 공용 노드풀에 반환(Enqueue 되기로 결정됨)
		// 이번에 사용할 상위 17비트값 만들기
		uint64_t meta = CAddressTranslator::GetCnt(&metaCnt_);

		// 새로운 버킷의 상위 17비트 주소를 조작해 meta주소를 만들고 버킷의 next도 같은 상위17비트를 쓰는 메타주소로 만든다
		uintptr_t newMetaTail = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)pBucket);
		pBucket->metaNext = CAddressTranslator::GetMetaAddr(meta, (uintptr_t)nullptr);
		while (true)
		{
			uintptr_t metaTail = metaTail_;
			Bucket* pRealTail = (Bucket*)CAddressTranslator::GetRealAddr(metaTail);
			uintptr_t metaNext = pRealTail->metaNext;

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
			if (InterlockedCompareExchange(&pRealTail->metaNext, newMetaTail, metaNext) != metaNext)
				continue;

			// 2번 CAS 54번째 라인의 if문안의 CAS 떄문에 실패할수 잇으며 이를통해 스핀락이 아니게됨
			InterlockedCompareExchange(&metaTail_, newMetaTail, metaTail);
			InterlockedIncrement(&size_);
			return;
		}
	}
};
#endif
