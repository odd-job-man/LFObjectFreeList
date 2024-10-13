#pragma once
#include <Windows.h>

#define PROFILE_ACTIVATE 

#ifdef PROFILE_ACTIVATE
struct PROFILE_SAMPLE
{
	char				szName[500];		// �������� ���� �̸�.
	static constexpr int MINMAX_COUNT = 2;
	LONGLONG totalTime;			// ��ü ���ð� ī���� Time.
	LONGLONG iMin[MINMAX_COUNT];			// �ּ� ���ð� ī���� Time.	(�ʴ����� ����Ͽ� ���� / [0] �����ּ� [1]) 
	LONGLONG iMax[MINMAX_COUNT];			// �ִ� ���ð� ī���� Time.	(�ʴ����� ����Ͽ� ���� / [0] �����ִ� [1])
	LONGLONG			callNum;			// ���� ȣ�� Ƚ��.
	bool				bUsed;				// ���������� ��� ����. (�迭�ÿ���)
	PROFILE_SAMPLE();
	void Reset();
};

struct PROFILE_SAMPLE_DESCRIPTOR
{
	PROFILE_SAMPLE* pProfileSampleArr_;
	size_t validSampleNumber_;
	static constexpr int PROFILE_SAMPLE_LENGTH = 100;
	PROFILE_SAMPLE_DESCRIPTOR();
	~PROFILE_SAMPLE_DESCRIPTOR();
	void Reset();
};

class PROFILE_REQUEST
{
	LARGE_INTEGER start_;
	LARGE_INTEGER end_;
	const int testIndex_;
	bool bBeginSuccess;
public:
	PROFILE_REQUEST(const int testIndex, const char* pFuncName, const char* pTagName);
	~PROFILE_REQUEST();
};



#define ProfileBegin(num,Tag)\
	thread_local static int test_Index##num = PROFILER::CurrentTestIdxCnt++;\
	PROFILE_REQUEST pr##num{test_Index##num,__FUNCSIG__,Tag};\


namespace PROFILER
{
	static constexpr int ThreadNum = 50;
	static constexpr unsigned int RELEASE_FLAG = 0x80000000;

	inline LARGE_INTEGER _freq;
	inline PROFILE_SAMPLE_DESCRIPTOR* DESC_ARR[ThreadNum + 1];
	inline long tlsIdx;
	inline long PROFILE_DESC_IDX_ALLOCATOR;
	inline thread_local int currentThreadIdx = 0;
	inline unsigned long state = 0;
	inline thread_local long CurrentTestIdxCnt = 0;

	void Init();
	void Clear();
	void Reset();
	void ProfileDataOutText(const char* szFileName);

	__forceinline static int AllocSampleArrayIndex()
	{
		return InterlockedIncrement(&PROFILE_DESC_IDX_ALLOCATOR);
	}

};
#define PROFILE(num,Tag) ProfileBegin(num,Tag)
#endif

#ifndef  PROFILE_ACTIVATE
#define PROFILE(num,Tag)
#endif 

