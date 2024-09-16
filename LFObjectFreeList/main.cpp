#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include "CheckMetaCntBits.h"
#include "FreeList.h"
#define STACK_TEST

#ifdef FREELIST_TEST
constexpr int THREAD_NUM = 4;

constexpr int TEST_NUM = 10000000;

int arr[TEST_NUM];


struct Data
{
    int data;
};

FreeList fr;

unsigned int WINAPI ThreadProc(LPVOID pParam)
{
    for (int i = 0; i < TEST_NUM / THREAD_NUM; i++)
    {
        Data* pd = (Data*)Alloc(&fr);
        arr[pd->data]++;
    }

    return 0;
}

Data* pArr[TEST_NUM];

int wmain()
{

    CheckMetaCntBits();
    Init(&fr, sizeof(Data), FALSE, nullptr, nullptr);
    for (int i = 0; i < TEST_NUM; ++i)
    {
        pArr[i] = (Data*)Alloc(&fr);
        pArr[i]->data = i;
    }
    
    for (int i = 0; i < TEST_NUM; ++i)
    {
        Free(&fr, pArr[i]);
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
        Free(&fr, pArr[i]);
    }

    Clear(&fr);

    printf("ZZANG~");
}
#endif

#ifdef STACK_TEST
#include <windows.h>
#include <process.h>
#include "LFStack.h"
#include "CheckMetaCntBits.h"
LFStack g_lfStack;
FreeList g_freeList;
unsigned _stdcall ThreadProc(void* pParam)
{
    while (true)
    {
        for (int i = 0; i < 200000; ++i)
        {
            LF_STACK_METADATA* pPush = (LF_STACK_METADATA*)Alloc(&g_freeList);
            Push_LF_STACK(&g_lfStack, pPush);
        }

        for (int i = 0; i < 200000; ++i)
        {
            LF_STACK_METADATA* pRet = Pop_LF_STACK(&g_lfStack);
            Free(&g_freeList, pRet);
        }
    }
}


int main()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    CheckMetaCntBits();
    HANDLE hThreadArr[2];
    InitLFStack(&g_lfStack);
    Init(&g_freeList, sizeof(LF_STACK_METADATA), FALSE, nullptr, nullptr);

    hThreadArr[0] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
    hThreadArr[1] = (HANDLE)_beginthreadex(nullptr, 0, ThreadProc, nullptr, 0, nullptr);

    WaitForMultipleObjects(2, hThreadArr, TRUE, INFINITE);
}
#endif