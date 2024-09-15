#include <windows.h>
#include "CheckMetaCntBits.h"
BOOL CheckMetaCntBits()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	ULONG_PTR max = (ULONG_PTR)si.lpMaximumApplicationAddress + (ULONG_PTR)si.lpMinimumApplicationAddress;
	int iCnt = 0;
	for (int i = 0; i < 64; ++i)
	{
		if ((max & 1) == 0)
			++iCnt;
		max = max >> 1;
	}

	if (iCnt == ZERO_COUNT)
		return TRUE;
	else
		return FALSE;
}
