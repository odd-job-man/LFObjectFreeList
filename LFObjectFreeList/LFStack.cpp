#include <windows.h>
#include "LFStack.h"
#include "CheckMetaCntBits.h"

void InitLFStack(LFStack* pStack)
{
	// 스택 최하단에서는 항상 nullptr을 가리키도록 함
	InterlockedExchangePointer((PVOID*)&pStack->pMetaTop, nullptr);
	InterlockedExchange(&pStack->lNum, 0);
	InterlockedExchange(&pStack->metaCounter, 0);
}

void Push_LF_STACK(LFStack* pStack, LF_STACK_METADATA* pNew)
{
	void* pLocalMetaTop;
	void* pNewMetaTop;
	do
	{
		pLocalMetaTop = pStack->pMetaTop;
		pNew->pMetaNext = pLocalMetaTop;
		pNewMetaTop = GetMetaAddr(GetCnt(&pStack->metaCounter), pNew);
	} while (InterlockedCompareExchangePointer((PVOID*)&pStack->pMetaTop, (PVOID)pNewMetaTop, (PVOID)pLocalMetaTop) != pLocalMetaTop);

	InterlockedIncrement(&pStack->lNum);
}

LF_STACK_METADATA* Pop_LF_STACK(LFStack* pStack)
{
	void* pLocalMetaTop;
	LF_STACK_METADATA* pLocalRealTop;
	void* pNewMetaTop;
	do
	{
		pLocalMetaTop = pStack->pMetaTop;
		if (!pLocalMetaTop)
			return nullptr;

		pLocalRealTop = (LF_STACK_METADATA*)GetRealAddr(pLocalMetaTop);
		pNewMetaTop = pLocalRealTop->pMetaNext;
	} while ((LF_STACK_METADATA*)InterlockedCompareExchangePointer((PVOID*)&pStack->pMetaTop, (PVOID)pNewMetaTop, (PVOID)pLocalMetaTop) != pLocalMetaTop);
	InterlockedDecrement(&pStack->lNum);
	return pLocalRealTop;
}