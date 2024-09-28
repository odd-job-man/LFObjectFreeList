#pragma once
#include <cstdint>
#include <windows.h>

class CAddressTranslator
{
	static constexpr size_t ZERO_COUNT = 17;
	static constexpr size_t NON_ZERO_COUNT = 64 - ZERO_COUNT;
	static constexpr uintptr_t REAL_ADDR_MASK = 0x00007FFFFFFFFFFF;
	static constexpr size_t COUNTER_MAX = 131071;

public:
	static bool CheckMetaCntBits()
	{
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		uintptr_t max = (uintptr_t)si.lpMaximumApplicationAddress + (uintptr_t)si.lpMinimumApplicationAddress;
		int iCnt = 0;
		for (int i = 0; i < 64; ++i)
		{
			if ((max & 1) == 0)
				++iCnt;
			max = max >> 1;
		}

		if (iCnt == ZERO_COUNT)
			return true;
		else
			return false;
	}

	static uint64_t GetCnt(uint64_t* pCnt)
	{
		uint64_t ret = InterlockedIncrement(pCnt) - 1;
		return ret % COUNTER_MAX;
	}

	static uintptr_t GetMetaAddr(size_t cnt, uintptr_t realAddr)
	{
		uintptr_t ret = (uintptr_t)(cnt << NON_ZERO_COUNT) | realAddr;
		return ret;
	}

	static uintptr_t GetRealAddr(uintptr_t metaAddr)
	{
		uintptr_t ret = metaAddr & REAL_ADDR_MASK;
		return ret;
	}

};
