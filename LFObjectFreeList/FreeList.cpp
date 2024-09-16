#include <windows.h>
#include "LFStack.h"
#include "FreeList.h"
#include "CheckMetaCntBits.h"

__forceinline int GetPadding(int iObjectSize)
{
	return sizeof(ULONG_PTR) - iObjectSize % sizeof(ULONG_PTR);
}

__forceinline int GetNodeSize(int iObjectSize)
{
	return iObjectSize + GetPadding(iObjectSize) + sizeof(ULONG_PTR);
}

__forceinline void* GetObjectAddr(void* pNode)
{
	return (char*)pNode + sizeof(ULONG_PTR);
}

__forceinline LF_STACK_METADATA* DataToNode(void* pData)
{
	return (LF_STACK_METADATA*)((char*)pData - sizeof(ULONG_PTR));
}

BOOL Init(FreeList* pFreeList, LONG lObjectSize, BOOL bPlacementNew, INIT_PROC initProc_NULLABLE, DESTRUCTOR_PROC destProc_NULLABLE)
{
	InterlockedExchange(&pFreeList->lCapacity, 0);
	InterlockedExchange(&pFreeList->lSize, 0);
	InterlockedExchange((LONG*)&pFreeList->bPlacementNew, bPlacementNew);
	InterlockedExchange(&pFreeList->ullMetaAddrCnt, 0);
	InterlockedExchange(&pFreeList->lObjectSize, lObjectSize);
	InterlockedExchangePointer((PVOID*)&pFreeList->initProc, initProc_NULLABLE);
	InterlockedExchangePointer((PVOID*)&pFreeList->destProc, destProc_NULLABLE);
	InterlockedExchangePointer((PVOID*)&pFreeList->pMetaTop, nullptr);
	return TRUE;
}

void* Alloc(FreeList* pFreeList)
{
	void* pLocalMetaTop;
	LF_STACK_METADATA* pLocalRealTop;
	void* pNewMetaTop;
	do
	{
		pLocalMetaTop = pFreeList->pMetaTop;
		if (!pLocalMetaTop)
		{
			InterlockedIncrement(&pFreeList->lCapacity);
			void* pNode = new char[GetNodeSize(pFreeList->lObjectSize)];
			// 초기화함수가 정의되어잇다면 플래그 관계없이 최초에는 호출해준다
			if (pFreeList->initProc)
				pFreeList->initProc(GetObjectAddr(pNode));
			return GetObjectAddr(pNode);
		}
		pLocalRealTop = (LF_STACK_METADATA*)GetRealAddr(pLocalMetaTop);
		pNewMetaTop = pLocalRealTop->pMetaNext;
	} while (InterlockedCompareExchangePointer(&pFreeList->pMetaTop, pNewMetaTop, pLocalMetaTop) != pLocalMetaTop);

	// bPlacementNew가 TRUE라면 당연히도 초기화함수는 집어넣엇어야한다
	if (pFreeList->bPlacementNew)
		pFreeList->initProc(GetObjectAddr(pLocalRealTop));

	InterlockedDecrement(&pFreeList->lSize);
	return GetObjectAddr(pLocalRealTop);
}

void Free(FreeList* pFreeList, void* pData)
{
	void* pLocalMetaTop;
	LF_STACK_METADATA* pNewRealTop;
	void* pNewMetaTop;

	// bPlacmentNew가 TRUE이더라도 소멸자는 없을수 잇음
	if (pFreeList->bPlacementNew && pFreeList->destProc)
		pFreeList->destProc(pData);
	do
	{
		pLocalMetaTop = pFreeList->pMetaTop;
		pNewRealTop = DataToNode(pData);
		pNewRealTop->pMetaNext = pLocalMetaTop;
		pNewMetaTop = GetMetaAddr(GetCnt(&pFreeList->ullMetaAddrCnt), pNewRealTop);
	} while (InterlockedCompareExchangePointer((PVOID*)&pFreeList->pMetaTop, (PVOID)pNewMetaTop, (PVOID)pLocalMetaTop) != pLocalMetaTop);

	InterlockedIncrement(&pFreeList->lSize);
}

void Clear(FreeList* pFreeList)
{
	LF_STACK_METADATA* pLocalRealTop = (LF_STACK_METADATA*)GetRealAddr(pFreeList->pMetaTop);
	LF_STACK_METADATA* pNodeToDelete;

	if (!pLocalRealTop)
		return;

	do
	{
		pNodeToDelete = pLocalRealTop;
		pFreeList->pMetaTop = pNodeToDelete->pMetaNext;
		pLocalRealTop = (LF_STACK_METADATA*)GetRealAddr(pNodeToDelete->pMetaNext);
		InterlockedDecrement(&pFreeList->lCapacity);
		InterlockedDecrement(&pFreeList->lSize);
		delete[] pNodeToDelete;
	} while (pLocalRealTop);
}
