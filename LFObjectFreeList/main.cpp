#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include "CheckMetaCntBits.h"
#include "FreeList.h"

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