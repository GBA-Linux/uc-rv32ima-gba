#include <stdio.h>
#include <string.h>
#include <gba_timers.h>

#include "../../host.h"

extern struct MiniRV32IMAState core;
extern void DumpState(struct MiniRV32IMAState *core);

#define TIMER_DIV_64	1

static u32 lastTimerTicks;
static uint64_t timerEpoch;

uint64_t GetTimeMicroseconds()
{
	u16 highBefore, highAfter, low;
	u32 ticks;
	uint64_t extendedTicks;

	/*
	 * Timer 0 runs at 16.777216 MHz / 64 = 262144 Hz and timer 1
	 * supplies its upper 16 bits.  Re-read around a timer 0 rollover
	 * so the combined value cannot tear.
	 */
	do {
		highBefore = REG_TM1CNT_L;
		low = REG_TM0CNT_L;
		highAfter = REG_TM1CNT_L;
	} while (highBefore != highAfter);

	ticks = ((u32)highAfter << 16) | low;
	if (ticks < lastTimerTicks)
		timerEpoch += UINT64_C(1) << 32;
	lastTimerTicks = ticks;
	extendedTicks = timerEpoch | ticks;

	/* 1000000 / 262144 = 15625 / 4096 exactly */
	return (extendedTicks * UINT64_C(15625)) >> 12;
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
	/* set up timers to emulate CLINT mtime counter */
	REG_TM0CNT_H = 0;
	REG_TM1CNT_H = 0;
	REG_TM0CNT_L = 0;
	REG_TM1CNT_L = 0;
	lastTimerTicks = timerEpoch = 0;
	REG_TM1CNT_H = TIMER_COUNT | TIMER_START;
	REG_TM0CNT_H = TIMER_DIV_64 | TIMER_START;

	return 0;
}

int psram_read(u32 addr, void *buf, int len)
{
	H_ReadMemBuf(buf, addr, len);
	return 0;
}

int psram_write(u32 addr, void *buf, int len)
{
	H_WriteMemBuf(buf, addr, len);
	return 0;
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
