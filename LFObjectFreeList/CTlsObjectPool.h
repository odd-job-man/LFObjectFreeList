#pragma once
#include <utility>
#include <new>
#include "CAddressTranslator.h"
#include "MultithreadProfiler.h"

template<typename T, bool bPlacementNew>
class CTlsObjectPool
{
private:
	DWORD dwTlsIdx_;
	struct Bucket
	{
		static constexpr int size = 300;
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
		PROFILE(1, "Alloc From Tls Bucket Pool");
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
		PROFILE(1,"Free To Tls Bucket Pool")
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
