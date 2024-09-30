#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include "CLockFreeObjectPool.h"

#define QUEUE_TEST

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

HANDLE hThread[2];
CLockFreeQueue<uint64_t> q;

unsigned ThreadProc(void* pParam);

constexpr auto LOOP = 2;
constexpr auto THREAD_NUM = 2;

uint64_t g_enqCnt = 0;

int main()
{
    if (!CAddressTranslator::CheckMetaCntBits())
        __debugbreak();


    hThread[0] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, (void*)1, CREATE_SUSPENDED, nullptr);
    hThread[1] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, (void*)0, CREATE_SUSPENDED, nullptr);

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
