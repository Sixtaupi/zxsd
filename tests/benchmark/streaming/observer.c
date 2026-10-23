/*
 * Copyright (c) 2026 mxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Identical consumption cost and canonical checksum for both bindings. */

#include "bench.h"

#include <string.h>

/** Incorporate one byte into the canonical FNV-1a checksum. */
static void hash_byte(bench_observer_t *observer, unsigned char value)
{
	observer->checksum = (observer->checksum ^ value) * UINT32_C(16777619);
}

/** Encode integers explicitly, independently of host byte order or struct padding. */
static void hash_u32(bench_observer_t *observer, uint32_t value)
{
	unsigned int byte;

	for (byte = 0u; byte < 4u; ++byte) {
		hash_byte(observer, (unsigned char)(value >> (byte * 8u)));
	}
}

void bench_observer_init(bench_observer_t *observer)
{
	memset(observer, 0, sizeof(*observer));
	observer->checksum = UINT32_C(2166136261);
}

void bench_consume(bench_observer_t *observer, uint32_t id, const char *label, int enabled,
		   int32_t reading)
{
	size_t length = strlen(label);
	size_t index;

	if (observer->count == 0u) {
		observer->first_ticks = bench_ticks() - observer->started;
		observer->first_supplied = observer->supplied;
	}
	hash_u32(observer, id);
	hash_u32(observer, (uint32_t)length);
	for (index = 0u; index < length; ++index) {
		hash_byte(observer, (unsigned char)label[index]);
	}
	hash_byte(observer, enabled != 0);
	hash_u32(observer, (uint32_t)reading);
	observer->count++;
}
