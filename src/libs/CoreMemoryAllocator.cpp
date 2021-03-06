/*
 * Arctic MMORPG Server Software
 * Copyright (c) 2008-2014 Arctic Server Team
 * See COPYING for license details.
 */

#include <new>

#ifdef WIN32
#ifdef SCRIPTLIB

__declspec(dllimport) void* AllocateMemory(size_t iSize);
__declspec(dllimport) void FreeMemory(void* pPointer);

void * ::operator new(size_t iSize)
{
	return AllocateMemory(iSize);
}

void ::operator delete(void* pPointer)
{
	FreeMemory(pPointer);
}

#endif		// SCRIPTLIB
#endif		// WIN32

