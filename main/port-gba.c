#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "host.h"

extern struct MiniRV32IMAState core;
extern void DumpState(struct MiniRV32IMAState *core);

size_t GetTimeMicroseconds()
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return tv.tv_usec + ((size_t)(tv.tv_sec)) * 1000000LL;
}

int ReadKBByte(void)
{
	/* TODO: stub */
	return 0xffffffff;
}

int IsKBHit(void)
{
	/* TODO: stub */
	return 0;
}

int psram_init(void)
{
	/* nothing to initialize on GBA w/ link cable */
	return 0;
}

int psram_read(u32 addr, void *buf, int len)
{
	H_ReadMemBuf(buf, addr, len);
}

int psram_write(u32 addr, void *buf, int len)
{
	H_WriteMemBuf(buf, addr, len);
}

int load_images(int ram_size, int *kern_len)
{
	(void)ram_size;
	(void)kern_len;
	/*
	 * they're already loaded into the host-emulated
	 * memory by the host before we start.  nothing to
	 * do here.
	 */
	return 0;
}
