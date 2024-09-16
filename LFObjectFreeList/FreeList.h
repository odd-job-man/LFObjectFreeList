#pragma once
typedef void(*INIT_PROC)(void*);
typedef void(*DESTRUCTOR_PROC)(void*);

struct FreeList
{
	void* pMetaTop;
	LONG lCapacity;
	LONG lSize;
	BOOL bPlacementNew;
	ULONGLONG ullMetaAddrCnt;
	LONG lObjectSize;
	INIT_PROC initProc;
	DESTRUCTOR_PROC destProc;
};

BOOL Init(FreeList* pFreeList, LONG lObjectSize, BOOL bPlacementNew, INIT_PROC initProc_NULLABLE, DESTRUCTOR_PROC destProc_NULLABLE);
void* Alloc(FreeList* pFreeList);
void Free(FreeList* pFreeList, void* pData);
void Clear(FreeList* pFreeList);


