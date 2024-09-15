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

BOOL Init(FreeList* pFreeList, int iObjectSize, BOOL bPlacementNew, INIT_PROC initProc_NULLABLE, DESTRUCTOR_PROC destProc_NULLABLE)
{
	pFreeList->lCapacity = 0;
	pFreeList->lSize = 0;
	pFreeList->bPlacementNew = bPlacementNew;
	pFreeList->ullMetaAddrCnt = 0;
	pFreeList->iObjectSize = iObjectSize;
	pFreeList->initProc = initProc_NULLABLE;
	pFreeList->destProc = destProc_NULLABLE;
	pFreeList->pTop = nullptr;
	return TRUE;
}

void* Alloc(FreeList* pFreeList)
{
	void* pLocalMetaAddrTop = pFreeList->pTop;
	if (!pLocalMetaAddrTop)
	{
		InterlockedIncrement(&pFreeList->lCapacity);
		int iObjectSize = pFreeList->iObjectSize;
		void* pNode = new char[GetNodeSize(iObjectSize)];
		// �ʱ�ȭ�Լ��� ���ǵǾ��մٸ� �÷��� ������� ���ʿ��� ȣ�����ش�
		if (pFreeList->initProc)
			pFreeList->initProc(GetObjectAddr(pNode));
		return pNode;
	}

	LF_STACK_METADATA* pLocalRealAddrTop;
	void* pNewMetaAddrTop;
	do
	{
		pLocalMetaAddrTop = pFreeList->pTop;
		pLocalRealAddrTop = (LF_STACK_METADATA*)GetRealAddr(pLocalMetaAddrTop);
		pNewMetaAddrTop = pLocalRealAddrTop->pMetaNext;
	} while (InterlockedCompareExchangePointer(&pFreeList->pTop, pNewMetaAddrTop, pLocalMetaAddrTop) != pLocalMetaAddrTop);

	// bPlacementNew�� TRUE��� �翬���� �ʱ�ȭ�Լ��� ����־�����Ѵ�
	if (pFreeList->bPlacementNew)
		pFreeList->initProc(GetObjectAddr(pLocalRealAddrTop));

	InterlockedDecrement(&pFreeList->lSize);
	return GetObjectAddr(pLocalRealAddrTop);
}

void Free(FreeList* pFreeList, void* pData)
{
	void* pLocalMetaAddrTop;
	LF_STACK_METADATA* pNewRealAddrTop;
	void* pNewMetaAddrTop;

	ULONG_PTR localCnt = GetCnt(&pFreeList->ullMetaAddrCnt);

	// bPlacmentNew�� TRUE�̴��� �Ҹ��ڴ� ������ ����
	if (pFreeList->bPlacementNew && pFreeList->destProc)
		pFreeList->destProc(pData);
	do
	{
		pLocalMetaAddrTop = pFreeList->pTop;
		pNewRealAddrTop = (LF_STACK_METADATA*)((char*)pData - sizeof(ULONG_PTR));
		pNewRealAddrTop->pMetaNext = pLocalMetaAddrTop;
		pNewMetaAddrTop = GetMetaAddr(localCnt, pNewRealAddrTop);
	} while (InterlockedCompareExchangePointer((PVOID*)&pFreeList->pTop, (PVOID)pNewMetaAddrTop, (PVOID)pLocalMetaAddrTop) != pLocalMetaAddrTop);

	InterlockedIncrement(&pFreeList->lSize);
}
