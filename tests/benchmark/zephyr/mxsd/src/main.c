/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "benchmark.bind.h"
#include "benchmark.mxsd.h"

#include <mxsd.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stdio.h>

#define BENCHMARK_RUNS   20u
#define INPUT_BLOCK_SIZE 4096u
#define SAMPLE_COUNT     BENCHMARK_SAMPLE_COUNT

extern const unsigned char benchmark_xml[];
extern const size_t benchmark_xml_size;

static mxsd_xml_reader_t reader;
static mxsd_stream_t writer_validator;
static benchmark_Sample_t samples[SAMPLE_COUNT];
static char labels[SAMPLE_COUNT][16];

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

static int validate_once(void)
{
	mxsd_result_t result = mxsd_xml_reader_init(&reader, benchmark_schema());
	size_t offset = 0u;

	while (result.code == MXSD_OK && offset < benchmark_xml_size) {
		size_t length = benchmark_xml_size - offset;

		if (length > INPUT_BLOCK_SIZE) {
			length = INPUT_BLOCK_SIZE;
		}
		result = mxsd_xml_reader_feed(&reader, benchmark_xml + offset, length);
		offset += length;
	}
	if (result.code == MXSD_OK) {
		result = mxsd_xml_reader_finish(&reader);
	}
	mxsd_xml_reader_dispose(&reader);
	return result.code == MXSD_OK ? 0 : -1;
}

static int serialize_once(size_t *bytes, int validate)
{
	benchmark_samples_t document = {
		.sample = samples,
		.sample_count = SAMPLE_COUNT,
	};
	counting_sink_t sink = {0};
	mxsd_result_t result = {MXSD_OK, 0u, NULL};

	if (validate) {
		result = benchmark_stream_init(&writer_validator);
	}
	if (result.code == MXSD_OK) {
		result = benchmark_samples_write(&document, count_output, &sink,
						 validate ? &writer_validator : NULL);
	}
	if (validate) {
		mxsd_stream_dispose(&writer_validator);
	}
	*bytes = sink.bytes;
	return result.code == MXSD_OK ? 0 : -1;
}

static void report(const char *name, uint64_t total_cycles)
{
	uint64_t average = total_cycles / BENCHMARK_RUNS;

	printk("%s: %llu cycles/run\n", name, average);
}

int main(void)
{
	uint64_t validation_cycles = 0u;
	uint64_t binding_cycles = 0u;
	uint64_t serialization_cycles = 0u;
	size_t output_bytes = 0u;
	size_t index;

	for (index = 0u; index < SAMPLE_COUNT; ++index) {
		(void)snprintf(labels[index], sizeof(labels[index]), "sensor_%05lu",
			       (unsigned long)index);
		samples[index].id = (uint32_t)index;
		samples[index].label = labels[index];
		samples[index].enabled = (index % 2u) != 0u;
		samples[index].reading = (int32_t)(index % 2001u) - 1000;
	}

	if (validate_once() != 0 || serialize_once(&output_bytes, 0) != 0 ||
	    serialize_once(&output_bytes, 1) != 0) {
		printk("benchmark warm-up failed\n");
		return 1;
	}
	for (index = 0u; index < BENCHMARK_RUNS; ++index) {
		uint64_t started = k_cycle_get_64();

		if (validate_once() != 0) {
			printk("validation failed\n");
			return 1;
		}
		validation_cycles += k_cycle_get_64() - started;

		started = k_cycle_get_64();
		if (serialize_once(&output_bytes, 0) != 0) {
			printk("binding serialization failed\n");
			return 1;
		}
		binding_cycles += k_cycle_get_64() - started;

		started = k_cycle_get_64();
		if (serialize_once(&output_bytes, 1) != 0) {
			printk("serialization failed\n");
			return 1;
		}
		serialization_cycles += k_cycle_get_64() - started;
	}

	printk("mxsd qemu_cortex_a53, samples %u, input %zu, output %zu\n",
	       (unsigned int)SAMPLE_COUNT, benchmark_xml_size, output_bytes);
	printk("stream context: %zu bytes; history capacity: %u\n", sizeof(mxsd_stream_t),
	       (unsigned int)MXSD_STREAM_CHILD_CAPACITY);
	report("validation", validation_cycles);
	report("binding serialization", binding_cycles);
	report("serialization", serialization_cycles);
	return 0;
}
