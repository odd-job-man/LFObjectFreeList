#pragma once
struct LF_STACK_METADATA
{
	void* pMetaNext;
};

struct LFStack
{
	void* pMetaTop;
	ULONG_PTR metaCounter;
	LONG lNum;
};

void InitLFStack(LFStack* pStack);
void Push_LF_STACK(LFStack* pStack, LF_STACK_METADATA* pNew);
LF_STACK_METADATA* Pop_LF_STACK(LFStack* pStack);