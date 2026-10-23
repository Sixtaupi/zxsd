/*
 * Copyright (c) 2026 mxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Offline functional check; deliberately not a host timing benchmark. */

#include "bench.h"

#include <stdio.h>

uint64_t bench_ticks(void)
{
	static uint64_t ticks;

	return ++ticks;
}

int main(void)
{
	int result = bench_selftest();

	if (result != 0) {
		fprintf(stderr, "streaming binding self-test failed: %d\n", result);
	}
	return result != 0;
}
