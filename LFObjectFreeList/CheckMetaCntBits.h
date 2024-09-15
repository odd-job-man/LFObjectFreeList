#pragma once
#define ZERO_COUNT 17
#define NON_ZERO_COUNT (64 - ZERO_COUNT)
#define REAL_ADDR_MASK 0x00007FFFFFFFFFFF
#define COUNTER_MAX 131071
#include <windows.h>
BOOL CheckMetaCntBits();

__forceinline ULONGLONG GetCnt(ULONGLONG* pUllCnt)
{
	ULONGLONG ullRet = InterlockedIncrement(pUllCnt) - 1;
	return ullRet % COUNTER_MAX;
}

__forceinline void* GetMetaAddr(ULONG_PTR cnt, void* pRealAddr)
{
	void* pRet = (void*)((cnt << NON_ZERO_COUNT) | (ULONG_PTR)pRealAddr);
	return pRet;
}

__forceinline void* GetRealAddr(void* pMetaAddr)
{
	void* pRet = (void*)((ULONG_PTR)pMetaAddr & REAL_ADDR_MASK);
	return pRet;
}


