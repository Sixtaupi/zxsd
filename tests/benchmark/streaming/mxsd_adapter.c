/*
 * Copyright (c) 2026 mxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Real mxsd typed streaming binding, not an XML event observer. */

#include "bench.h"
#include "benchmark.bind.h"

static benchmark_samples_sample_reader_t reader;
static benchmark_Sample_t sample;
static mxsd_bind_arena_t arena;
static unsigned char arena_storage[256];

/** Consume every selected sample through the generated typed callback. */
static mxsd_bind_action_t received(void *context, const benchmark_Sample_t *value)
{
	bench_consume(context, value->id, value->label, value->enabled, value->reading);
	return MXSD_BIND_CONTINUE;
}

int bench_parse(const unsigned char *xml, size_t length, size_t block_size,
		bench_observer_t *observer)
{
	mxsd_result_t result;
	size_t offset = 0u;

	if (block_size == 0u) {
		return -1;
	}
	mxsd_bind_arena_init(&arena, arena_storage, sizeof(arena_storage));
	result = benchmark_samples_sample_reader_init(&reader, &sample, &arena, received, observer,
						      NULL);
	while (result.code == MXSD_OK && offset < length) {
		size_t count = length - offset;

		if (count > block_size) {
			count = block_size;
		}
		observer->supplied = offset + count;
		result = benchmark_samples_sample_reader_feed(&reader, xml + offset, count);
		offset += count;
	}
	if (result.code == MXSD_OK) {
		result = benchmark_samples_sample_reader_finish(&reader);
	}
	benchmark_samples_sample_reader_dispose(&reader);
	return result.code == MXSD_OK ? 0 : -1;
}

size_t bench_static_context_bytes(void)
{
	return sizeof(reader) + sizeof(sample) + sizeof(arena) + sizeof(arena_storage);
}
