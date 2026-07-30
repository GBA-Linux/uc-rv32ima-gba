/*
 * Copyright (c) 2023, Jisheng Zhang <jszhang@kernel.org>. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "cache.h"
#include "psram.h"

#ifdef EWRAM_DATA
#undef EWRAM_DATA
#endif

/* .ewram_data doesn't exist, yet the EWRAM_DATA macro tries to put it there; use our own */
#define EWRAM_DATA __attribute__((section(".ewram")))
#define ARM_CODE __attribute__((target("arm")))

#define CACHE_SIZE	(128 * 1024)
#define CACHE_LINE_SIZE	64
#define CACHE_WAYS	2
#define CACHE_SETS	(CACHE_SIZE / CACHE_LINE_SIZE / CACHE_WAYS)
#define CACHE_INDEX_MASK (CACHE_SETS - 1)
#define CACHE_TAG_MASK	(~((CACHE_SETS * CACHE_LINE_SIZE) - 1))

#if (CACHE_SETS & (CACHE_SETS - 1)) != 0
#error "CACHE_SETS must be a power of two"
#endif

struct cacheline {
	uint8_t data[CACHE_LINE_SIZE];
};

/*
 * These counters are diagnostic only.  64-bit increments are particularly
 * expensive on ARM7TDMI and used to be performed for every instruction fetch.
 */
static uint32_t accessed, hit;
static uint32_t tags[CACHE_SETS][CACHE_WAYS];
static EWRAM_DATA struct cacheline cachelines[CACHE_SETS][CACHE_WAYS];
static EWRAM_DATA uint64_t valid_bytes[CACHE_SETS][CACHE_WAYS];

/*
 * bit[0]: valid
 * bit[1]: dirty
 * bit[2]: for LRU
 * bit[3:15]: reserved
 * bit[16:31]: tag
 */
#define VALID		(1 << 0)
#define DIRTY		(1 << 1)
#define LRU		(1 << 2)
#define FULL		(1 << 3)
#define LRU_SFT		2
#define TAG_MSK		CACHE_TAG_MASK

/*
 * bit[0: 5]: offset
 * bit[6: 15]: index
 * bit[16:31]: tag
 */
static inline int get_index(uint32_t addr)
{
	return (addr >> 6) & CACHE_INDEX_MASK;
}

static inline uint64_t byte_mask(uint32_t offset, uint32_t size)
{
	if (size == CACHE_LINE_SIZE)
		return UINT64_MAX;
	return ((UINT64_C(1) << size) - 1) << offset;
}

/*
 * A store miss can allocate a line without reading its old contents.  Fetch
 * those contents only if a later read or writeback needs bytes which have not
 * been supplied by stores yet.
 */
static void complete_line(uint32_t *tag, uint8_t *data, uint64_t *valid)
{
	uint8_t old_data[CACHE_LINE_SIZE];
	int i;

	if (*valid == UINT64_MAX)
		return;

	psram_read(*tag & ~(CACHE_LINE_SIZE - 1), old_data, CACHE_LINE_SIZE);
	for (i = 0; i < CACHE_LINE_SIZE; i++) {
		if (!(*valid & (UINT64_C(1) << i)))
			data[i] = old_data[i];
	}
	*valid = UINT64_MAX;
	*tag |= FULL;
}

static void writeback_line(uint32_t *tag, uint8_t *data, uint64_t *valid)
{
	if (!(*tag & DIRTY))
		return;

	complete_line(tag, data, valid);
	psram_write(*tag & ~(CACHE_LINE_SIZE - 1), data, CACHE_LINE_SIZE);
}

ARM_CODE void cache_write(uint32_t ofs, void *buf, uint32_t size)
{
	if (((ofs | (CACHE_LINE_SIZE - 1)) !=
	     ((ofs + size - 1) | (CACHE_LINE_SIZE - 1))))
		printf("write cross boundary, ofs:%x size:%x\n", ofs, size);

	int ti, i, index = get_index(ofs);
	uint32_t *tp;
	uint8_t *p;
	uint64_t *valid;

	++accessed;

	for (i = 0; i < CACHE_WAYS; i++) {
		tp = &tags[index][i];
		p = cachelines[index][i].data;
		valid = &valid_bytes[index][i];
		if (*tp & VALID) {
			if ((*tp & TAG_MSK) == (ofs & TAG_MSK)) {
				++hit;
				ti = i;
				break;
			} else {
				if (i != 1)
					continue;

				ti = 1 - ((*tp & LRU) >> LRU_SFT);
				tp = &tags[index][ti];
				p = cachelines[index][ti].data;
				valid = &valid_bytes[index][ti];

				writeback_line(tp, p, valid);
				*tp = ofs & ~(CACHE_LINE_SIZE - 1);
				*tp |= VALID;
				*valid = 0;
			}
		} else {
			if (i != 1)
				continue;

			ti = i;
			*tp = ofs & ~(CACHE_LINE_SIZE - 1);
			*tp |= VALID;
			*valid = 0;
		}
	}

	tags[index][1] &= ~(LRU);
	tags[index][1] |= (ti << LRU_SFT);
	memcpy(p + (ofs & (CACHE_LINE_SIZE - 1)), buf, size);
	*valid |= byte_mask(ofs & (CACHE_LINE_SIZE - 1), size);
	if (*valid == UINT64_MAX)
		*tp |= FULL;
	*tp |= DIRTY;
}

/*
 * Resolve an address for reading.  Lines brought in by reads are always FULL,
 * so the overwhelmingly common instruction-fetch path does no 64-bit work.
 * A read from a partial store-allocated line completes it once here.
 */
static inline __attribute__((always_inline, target("arm")))
uint8_t *cache_read_ptr(uint32_t ofs)
{
	int ti, i, index = get_index(ofs);
	uint32_t *tp;
	uint8_t *p;
	uint64_t *valid;

	++accessed;

	for (i = 0; i < CACHE_WAYS; i++) {
		tp = &tags[index][i];
		p = cachelines[index][i].data;
		valid = &valid_bytes[index][i];
		if (*tp & VALID) {
			if ((*tp & TAG_MSK) == (ofs & TAG_MSK)) {
				++hit;
				ti = i;
				if (!(*tp & FULL))
					complete_line(tp, p, valid);
				break;
			} else {
				if (i != 1)
					continue;

				ti = 1 - ((*tp & LRU) >> LRU_SFT);
				tp = &tags[index][ti];
				p = cachelines[index][ti].data;
				valid = &valid_bytes[index][ti];

				writeback_line(tp, p, valid);
				psram_read(ofs & ~(CACHE_LINE_SIZE - 1), p,
					CACHE_LINE_SIZE);
				*tp = ofs & ~(CACHE_LINE_SIZE - 1);
				*tp |= VALID | FULL;
				*valid = UINT64_MAX;
			}
		} else {
			if (i != 1)
				continue;

			ti = i;
			psram_read(ofs & ~(CACHE_LINE_SIZE - 1), p,
				CACHE_LINE_SIZE);
			*tp = ofs & ~(CACHE_LINE_SIZE - 1);
			*tp |= VALID | FULL;
			*valid = UINT64_MAX;
		}
	}

	tags[index][1] &= ~(LRU);
	tags[index][1] |= (ti << LRU_SFT);
	return p + (ofs & (CACHE_LINE_SIZE - 1));
}

ARM_CODE void cache_read(uint32_t ofs, void *buf, uint32_t size)
{
	if (((ofs | (CACHE_LINE_SIZE - 1)) !=
	     ((ofs + size - 1) | (CACHE_LINE_SIZE - 1))))
		printf("read cross boundary, ofs:%x size:%x\n", ofs, size);

	uint8_t *p = cache_read_ptr(ofs);
	memcpy(buf, p, size);
}

ARM_CODE uint32_t cache_read32(uint32_t ofs)
{
	return *(uint32_t *)cache_read_ptr(ofs);
}

ARM_CODE uint16_t cache_read16(uint32_t ofs)
{
	return *(uint16_t *)cache_read_ptr(ofs);
}

ARM_CODE uint8_t cache_read8(uint32_t ofs)
{
	return *cache_read_ptr(ofs);
}

void cache_get_stat(uint64_t *phit, uint64_t *paccessed)
{
	*phit = hit;
	*paccessed = accessed;
}
