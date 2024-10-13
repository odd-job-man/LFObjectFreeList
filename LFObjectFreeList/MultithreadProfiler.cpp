#define PROFILE_ACTIVATE
#include "MultithreadProfiler.h"
#include <cstdio>
#include <ctime>


#pragma warning(disable : 4302)
#pragma warning(disable : 4311)
#pragma warning(disable : 4312)
#pragma warning(disable : 26495)

void PROFILER::Init()
{
	tlsIdx = TlsAlloc();
	memset(DESC_ARR, 0, sizeof(DESC_ARR));
	if (tlsIdx == TLS_OUT_OF_INDEXES)
	{
		DWORD dwErrorCode = GetLastError();
		__debugbreak();
	}
	QueryPerformanceFrequency(&_freq);
	atexit(Clear);
}

void PROFILER::Clear()
{
	TlsFree(tlsIdx);
	for (int i = 1; DESC_ARR[i] != nullptr; ++i)
	{
		delete DESC_ARR[i]->pProfileSampleArr_;
		DESC_ARR[i]->validSampleNumber_ = 0;
		DESC_ARR[i]->pProfileSampleArr_ = nullptr;
	}
}

void PROFILER::Reset()
{
	while (InterlockedCompareExchange(&state, RELEASE_FLAG | 0, 0) != 0)
	{
		YieldProcessor();
	}

	for(int i = 1; DESC_ARR[i] != nullptr; ++i)
	{
		DESC_ARR[i]->Reset();
	}

	InterlockedAnd((LONG*)&state, ~RELEASE_FLAG);
}


void PROFILER::ProfileDataOutText(const char* szFileName)
{
	//g_profileArray 배열의 정보들을 파일로 출력한다.
	while (InterlockedCompareExchange(&state, RELEASE_FLAG | 0, 0) != 0)
	{
		YieldProcessor();
	}

	FILE* pFile;
	errno_t err = fopen_s(&pFile, szFileName, "a");
	if (err != 0)
	{
		__debugbreak();
	}
	
	__time64_t now;
	_time64(&now);

	tm LocalTime;
	_localtime64_s(&LocalTime, &now);
	fprintf(pFile, "%4d:%02d:%02d:%02d:%02d\n", LocalTime.tm_year + 1900, LocalTime.tm_mon, LocalTime.tm_mday, LocalTime.tm_hour, LocalTime.tm_min);
	fprintf(pFile, "---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	fprintf(pFile, "| %-100s | %-15s | %-15s | %-15s | %-15s | %-15s | %-10s |\n", "Name", "TotalTime(ns)", "Correction(ns)", "Average (ns)", "Min (ns)", "Max (ns)", "Call");
	fprintf(pFile, "---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");

	for (int i = 1; DESC_ARR[i] != nullptr; ++i)
	{
		PROFILE_SAMPLE_DESCRIPTOR* pPSD = DESC_ARR[i];
		for (int j = 0; j < pPSD->validSampleNumber_; ++j)
		{
			for (int k = 0; k < 2; ++k)
			{
				if (pPSD->pProfileSampleArr_[j].iMin[k] == UINT_MAX)
				{
					pPSD->pProfileSampleArr_[j].iMin[k] = 0;
				}
			}
		}
	}


	for (int i = 1; DESC_ARR[i] != nullptr; ++i)
	{
		PROFILE_SAMPLE_DESCRIPTOR* pPSD = DESC_ARR[i];
		for (int j = 0; j < pPSD->validSampleNumber_; ++j)
		{
			__int64 totalTime = pPSD->pProfileSampleArr_[j].totalTime;
			__int64 sum = (totalTime - (pPSD->pProfileSampleArr_[j].iMax[0] + pPSD->pProfileSampleArr_[j].iMax[1] + pPSD->pProfileSampleArr_[j].iMin[0] + pPSD->pProfileSampleArr_[j].iMin[1]));
			float _average = sum / (float)(pPSD->pProfileSampleArr_[j].callNum - 4);

			__int64 printMin = (pPSD->pProfileSampleArr_[j].iMin[0] == 0) ? pPSD->pProfileSampleArr_[j].iMin[1] : pPSD->pProfileSampleArr_[j].iMin[0];
			__int64 printMax = (pPSD->pProfileSampleArr_[j].iMax[0] == 0) ? pPSD->pProfileSampleArr_[j].iMax[1] : pPSD->pProfileSampleArr_[j].iMax[0];

			fprintf(pFile, "| %-100s | %-15lld | %-15lld | %-15f | %-15lld | %-15lld | %-10lld |\n",
				pPSD->pProfileSampleArr_[j].szName, totalTime, sum, _average, printMin, printMax, pPSD->pProfileSampleArr_[j].callNum);

		}
		fprintf(pFile, "---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	}

	fputc('\n', pFile);
	fputc('\n', pFile);
	fclose(pFile);
	InterlockedAnd((LONG*)&state, ~RELEASE_FLAG);
}

PROFILE_SAMPLE_DESCRIPTOR::PROFILE_SAMPLE_DESCRIPTOR()
	:validSampleNumber_{ 0 }
{
	pProfileSampleArr_ = new PROFILE_SAMPLE[PROFILE_SAMPLE_LENGTH];
}

PROFILE_SAMPLE_DESCRIPTOR::~PROFILE_SAMPLE_DESCRIPTOR()
{
	delete[] pProfileSampleArr_;
}

void PROFILE_SAMPLE_DESCRIPTOR::Reset()
{
	for (size_t i = 0; i < validSampleNumber_; ++i)
	{
		pProfileSampleArr_[i].Reset();
	}
}

PROFILE_SAMPLE::PROFILE_SAMPLE()
	:totalTime{ 0 }, callNum{ 0 }, bUsed{ false }
{
	szName[0] = 0;
	for (int i = 0; i < MINMAX_COUNT; ++i)
	{
		iMin[i] = UINT_MAX;
		iMax[i] = 0;
	}
}

void PROFILE_SAMPLE::Reset()
{
	totalTime = 0;
	callNum = 0;
	for (int i = 0; i < MINMAX_COUNT; ++i)
	{
		iMin[i] = UINT_MAX;
		iMax[i] = 0;
	}
}

PROFILE_REQUEST::PROFILE_REQUEST(const int testIndex, const char* pFuncName, const char* pTagName)
	:testIndex_{ testIndex }
{
	// Reset에서 플래그를 선점햇다면 프로파일링 시작 안한다
	if ((InterlockedIncrement(&PROFILER::state) & PROFILER::RELEASE_FLAG) != PROFILER::RELEASE_FLAG)
	{
		int idx = (int)TlsGetValue(PROFILER::tlsIdx);
		if (idx == 0)
		{
			idx = PROFILER::AllocSampleArrayIndex();
			TlsSetValue(PROFILER::tlsIdx, (LPVOID)idx);
			PROFILER::DESC_ARR[idx] = new PROFILE_SAMPLE_DESCRIPTOR;
		}

		PROFILE_SAMPLE* pSample = PROFILER::DESC_ARR[idx]->pProfileSampleArr_ + testIndex;
		if (pSample->bUsed == false)
		{
			strcat_s(pSample->szName, 500, pFuncName);
			strcat_s(pSample->szName, 500- strlen(pFuncName), " ");
			strcat_s(pSample->szName, 500- strlen(pFuncName) - strlen(" "), pTagName);
			pSample->bUsed = true;
			++PROFILER::DESC_ARR[idx]->validSampleNumber_;
		}
		bBeginSuccess = true;
		QueryPerformanceCounter(&start_);
	}
	else
	{
		bBeginSuccess = false;
	}
}

PROFILE_REQUEST::~PROFILE_REQUEST()
{
	QueryPerformanceCounter(&end_);

	// 프로파일링 시작에 실패햇다면 측정결과를 반영하지 않는다
	if (bBeginSuccess == false)
	{
		InterlockedDecrement(&PROFILER::state);
		return;
	}

	// 프로파일링 시작에 성공햇다면 어차피 state의 최상위 비트를 제외한 값이 모두 0이될떄까지 Reset은 진행되지 못하므로 진행한다
	LONGLONG elapsedTime = end_.QuadPart - start_.QuadPart;
	PROFILE_SAMPLE* pSample = PROFILER::DESC_ARR[(int)TlsGetValue(PROFILER::tlsIdx)]->pProfileSampleArr_ + testIndex_;
	++pSample->callNum;
	pSample->totalTime += elapsedTime;

	// iMin, iMax 갱신
	if (pSample->iMin[0] > elapsedTime)
	{
		pSample->iMin[1] = pSample->iMin[0];
		pSample->iMin[0] = elapsedTime;
	}
	else
	{
		if (pSample->iMin[1] > elapsedTime)
		{
			pSample->iMin[1] = elapsedTime;
		}
	}

	if (pSample->iMax[0] < elapsedTime)
	{
		pSample->iMax[1] = pSample->iMax[0];
		pSample->iMax[0] = elapsedTime;
	}
	else
	{
		if (pSample->iMax[1] < elapsedTime)
		{
			pSample->iMax[1] = elapsedTime;
		}
	}

	InterlockedDecrement(&PROFILER::state);
}

#pragma warning(default: 4302)
#pragma warning(default: 4311)
#pragma warning(default: 4312)
#pragma warning(default: 26495)
