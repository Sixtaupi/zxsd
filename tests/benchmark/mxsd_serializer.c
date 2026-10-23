/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "benchmark.bind.h"
#include "benchmark.mxsd.h"

#include <mxsd.h>

#include <stdio.h>

#define SAMPLE_COUNT 1024u

typedef struct {
	size_t bytes;
} counting_sink_t;

static int count_output(void *context, const char *data, size_t length)
{
	counting_sink_t *sink = context;

	(void)data;
	sink->bytes += length;
	return 0;
}

int main(void)
{
	static benchmark_Sample_t samples[SAMPLE_COUNT];
	static char labels[SAMPLE_COUNT][16];
	benchmark_samples_t document = {0};
	counting_sink_t sink = {0};
	mxsd_stream_t validator;
	mxsd_result_t result;
	size_t index;

	for (index = 0u; index < SAMPLE_COUNT; ++index) {
		(void)snprintf(labels[index], sizeof(labels[index]), "sensor_%05lu",
			       (unsigned long)index);
		samples[index].id = (uint32_t)index;
		samples[index].label = labels[index];
		samples[index].enabled = (index % 2u) != 0u;
		samples[index].reading = (int32_t)(index % 2001u) - 1000;
	}
	document.sample = samples;
	document.sample_count = SAMPLE_COUNT;

	result = benchmark_stream_init(&validator);
	if (result.code == MXSD_OK) {
		result = benchmark_samples_write(&document, count_output, &sink, &validator);
	}
	mxsd_stream_dispose(&validator);
	if (result.code != MXSD_OK) {
		fprintf(stderr, "serialization failed: %s\n", mxsd_error_string(result.code));
		return 1;
	}
	printf("%zu\n", sink.bytes);
	return 0;
}
