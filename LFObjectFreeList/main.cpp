#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include "CLockFreeObjectPool.h"

#define POOL_BUCKET_TEST

#ifdef FREELIST_TEST
constexpr int THREAD_NUM = 4;

constexpr int TEST_NUM = 10000000;

int arr[TEST_NUM];


struct Data
{
    int data;

    Data() {};
};

CLockFreeObjectPool<Data,false> fr;

unsigned int WINAPI ThreadProc(LPVOID pParam)
{
    for (int i = 0; i < TEST_NUM / THREAD_NUM; i++)
    {
        Data* pd = fr.Alloc();
        pd->data = i;
        arr[pd->data]++;
    }

    return 0;
}

Data* pArr[TEST_NUM];

int wmain()
{
    CAddressTranslator::CheckMetaCntBits();
    for (int i = 0; i < TEST_NUM; ++i)
    {
        pArr[i] = fr.Alloc();
    }
    
    for (int i = 0; i < TEST_NUM; ++i)
    {
        fr.Free(pArr[i]);
    }

    HANDLE threads[THREAD_NUM];

    for (int i = 0; i < THREAD_NUM; i++)
    {
        threads[i] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, nullptr, CREATE_SUSPENDED, nullptr);
    }

    for (int i = 0; i < THREAD_NUM; i++)
    {
        ResumeThread(threads[i]);
    }

    WaitForMultipleObjects(THREAD_NUM, threads, TRUE, INFINITE);


    for (int i = 0; i < TEST_NUM; i++)
    {
        if (arr[i] != 1)
        {
            __debugbreak();
        }
    }

    for (int i = 0; i < TEST_NUM; ++i)
    {
        fr.Free(pArr[i]);
    }

    //Clear(&fr);

    printf("ZZANG~");
}
#endif

#ifdef STACK_TEST
#include <windows.h>
#include <process.h>
#include "LFStack.h"
#include "CLockFreeStack.h"

struct Data
{
    int a;
    double b;
    uint64_t c;

    Data() {};

    Data(const Data& other)
        :a{ 1 }, b{ 2 }, c{ 3 }
    {
    }

    Data(Data&& other)
        :a{ 3 }, b{ 2 }, c{ 1 }
    {
        printf("move\n");
    }
};

CLockFreeStack<int> stack;

unsigned _stdcall ThreadProc(void* pParam)
{
    while (true)
    {
        for (int i = 0; i < 200000; ++i)
        {
            stack.Push(i);
        }

        for (int i = 0; i < 200000; ++i)
        {
            std::optional<int> popRet = stack.Pop();
            if (!popRet.has_value())
                __debugbreak();

        }
    }
}


int main()
{
    CAddressTranslator::CheckMetaCntBits();

    HANDLE hThreadArr[2];
    hThreadArr[0] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
    //hThreadArr[1] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, nullptr, 0, nullptr);

    WaitForMultipleObjects(1, hThreadArr, TRUE, INFINITE);
}
#endif


#ifdef QUEUE_TEST
#include "CLockFreeQueue.h"
#include "CLockFreeObjectPoolBucket.h"

HANDLE hThread[2];
CLockFreeQueue<uint64_t> q;
//CTlsObjectPool<uint64_t, true> p;

unsigned ThreadProc(void* pParam);

constexpr auto LOOP = 2;
constexpr auto THREAD_NUM = 4;

uint64_t g_enqCnt = 0;

int main()
{
    if (!CAddressTranslator::CheckMetaCntBits())
        __debugbreak();

    hThread[0] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, (void*)1, CREATE_SUSPENDED, nullptr);
    hThread[1] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, (void*)0, CREATE_SUSPENDED, nullptr);
    hThread[2] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, (void*)0, CREATE_SUSPENDED, nullptr);
    hThread[3] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, (void*)0, CREATE_SUSPENDED, nullptr);

    for (int i = 0; i < THREAD_NUM; ++i)
    {
        ResumeThread(hThread[i]);
    }

    WaitForMultipleObjects(2, hThread, TRUE, INFINITE);
}

unsigned ThreadProc(void* pParam)
{
    while (true)
    {
        for (int i = 0; i < LOOP; ++i)
        {
            q.Enqueue(InterlockedIncrement(&g_enqCnt) - 1);
        }

        for (int i = 0; i < LOOP; ++i)
        {
            auto deqRet = q.Dequeue();
            if (!deqRet.has_value())
                __debugbreak();
        }
    }
}
#endif


#ifdef TLS_POOL_TEST
//#define BUCKET
#include <Psapi.h>
#include <iostream>
#define STACK
#include "CLockFreeObjectPool.h"
#include "CTlsObjectPool.h"
#include "MultithreadProfiler.h"

constexpr auto LOOP = 100;
constexpr auto THREAD_NUM = 4;
#define Tls
#ifdef ORIGINAL
CLockFreeObjectPool<int, true>g_pool;
#else
CTlsObjectPool<int, true> g_pool;
#endif

unsigned ThreadProc(void* pParam)
{
    int* testArr[LOOP];
    while (true)
    {
        for (int i = 0; i < LOOP; ++i)
        {
#ifdef ORIGINAL
		    PROFILE(1, "Alloc From Original Pool")
#else
#ifdef QUEUE
		    PROFILE(1, "Alloc From Tls Bucket Pool Q")
#else
            PROFILE(1, "Alloc From Tls Bucket Pool Stack")
#endif
#endif
            testArr[i] = g_pool.Alloc(i);
        }

        for (int i = 0; i < LOOP; ++i)
        {
#ifdef ORIGINAL
		    PROFILE(1,"Free To Original Pool")
#else
#ifdef QUEUE
	    	PROFILE(1, "Free From Tls Bucket Pool Q");
#else
            PROFILE(1, "Free From Tls Bucket Pool Stack")
#endif
#endif
            g_pool.Free(testArr[i]);
        }
    }
}

unsigned __stdcall Monitoring(void* pParam)
{
    PROCESS_MEMORY_COUNTERS_EX pmcEx;
    // 현재 프로세스 핸들 가져오기
    HANDLE hProcess = GetCurrentProcess();

    while (1)
    {
        Sleep(1000);
        GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmcEx, sizeof(pmcEx));
        std::cout << "Private Bytes (User-mode allocation): " << pmcEx.PrivateUsage / 1024 << " KB" << std::endl;
        if (GetAsyncKeyState(VK_BACK) & 0x01)
        {
            PROFILER::Reset();
            std::cout << "Profiler Reset!" << std::endl;
        }

        if (GetAsyncKeyState(VK_RETURN) & 0x01)
        {
            PROFILER::ProfileDataOutText("Profile.txt");
            std::cout << "Profiler DataTextOut!" << std::endl;
        }
    }
}


int main()
{
    PROFILER::Init();
    HANDLE hThread[THREAD_NUM];
    for (int i = 0; i < THREAD_NUM; ++i)
    {
        hThread[i] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, (void*)0, CREATE_SUSPENDED, nullptr);
    }

    for (int i = 0; i < THREAD_NUM; ++i)
    {
        ResumeThread(hThread[i]);
    }

    HANDLE hMontoring = (HANDLE)_beginthreadex(nullptr, 0, Monitoring, nullptr, 0, nullptr);
    WaitForMultipleObjects(THREAD_NUM, hThread, TRUE, INFINITE);
}
#endif



#include <iostream>
#include "CLockFreeQueue.h"

constexpr auto THREAD_NUM = 4;
constexpr auto Q_NUM = 2;
CLockFreeQueue<uint64_t> g_Q[Q_NUM];

unsigned __stdcall ThreadProc(void* pParam)
{
    uint64_t idx = (uint64_t)pParam;
    while (1)
    {
        for (int i = 0; i < 1; ++i)
        {
            g_Q[idx].Enqueue(idx);
        }
        for (int i = 0; i < 1; ++i)
        {
            auto ret = g_Q[idx].Dequeue().value();
            if (ret != idx)
            {
                 __debugbreak();
            }
        }
    }
    return 0;
}


int main()
{
    HANDLE hThread[THREAD_NUM];
    for (int i = 0; i < THREAD_NUM; ++i)
    {
        hThread[i] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, (void*)(i % Q_NUM), CREATE_SUSPENDED, nullptr);
    }

    for (int i = 0; i < THREAD_NUM; ++i)
    {
        ResumeThread(hThread[i]);
    }

    WaitForMultipleObjects(THREAD_NUM, hThread, TRUE, INFINITE);
    return 0;
}
