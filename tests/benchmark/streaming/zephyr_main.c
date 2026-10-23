/*
 * Copyright (c) 2026 mxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Shared offline measurement loop; no HTTP, DNS, TLS, or per-block logging. */

#include "bench.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <sys_malloc.h>

#define WORKER_STACK_SIZE 32768u

extern const unsigned char benchmark_xml[];
extern const size_t benchmark_xml_size;

K_THREAD_STACK_DEFINE(worker_stack, WORKER_STACK_SIZE);
static struct k_thread worker;
K_SEM_DEFINE(measured, 0, 1);

static bench_observer_t results[BENCHMARK_RUNS];
static uint64_t cycles[BENCHMARK_RUNS];
static int parse_failed;

uint64_t bench_ticks(void)
{
	return k_cycle_get_64();
}

/** Run only valid XML in this dedicated, freshly initialized stack. */
static void measure(void *unused1, void *unused2, void *unused3)
{
	bench_observer_t warmup;
	size_t index;

	(void)unused1;
	(void)unused2;
	(void)unused3;
	bench_observer_init(&warmup);
	warmup.started = bench_ticks();
	parse_failed =
		bench_parse(benchmark_xml, benchmark_xml_size, BENCHMARK_BLOCK_SIZE, &warmup);
	for (index = 0u; !parse_failed && index < BENCHMARK_RUNS; ++index) {
		bench_observer_init(&results[index]);
		results[index].started = bench_ticks();
		parse_failed = bench_parse(benchmark_xml, benchmark_xml_size, BENCHMARK_BLOCK_SIZE,
					   &results[index]);
		cycles[index] = bench_ticks() - results[index].started;
	}
	k_sem_give(&measured);
	/* Keep the stack inspectable; do not include reporting or negative XML tests. */
	k_thread_suspend(k_current_get());
}

int main(void)
{
	struct sys_memory_stats heap;
	size_t unused;
	size_t index;
	int selftest;

	k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack), measure, NULL,
			NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	k_sem_take(&measured, K_FOREVER);
	if (parse_failed || k_thread_stack_space_get(&worker, &unused) != 0 ||
	    malloc_runtime_stats_get(&heap) != 0) {
		printk("BENCH_ERROR measurement failed\n");
		return 1;
	}
	/* Heap peak is read before self-tests and reporting; arena bytes already belong to bss. */
	printk("BENCH_META {\"implementation\":\"%s\",\"input_bytes\":%zu,"
	       "\"block_size\":%u,\"runs\":%u,\"clock_hz\":%llu,"
	       "\"static_context_bytes\":%zu,\"worker_stack_reserved_bytes\":%zu,"
	       "\"worker_stack_peak_bytes\":%zu,\"heap_peak_bytes\":%zu,"
	       "\"heap_live_after_parse_bytes\":%zu}\n",
	       BENCHMARK_IMPLEMENTATION, benchmark_xml_size, BENCHMARK_BLOCK_SIZE, BENCHMARK_RUNS,
	       (unsigned long long)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC, bench_static_context_bytes(),
	       worker.stack_info.size, worker.stack_info.size - unused, heap.max_allocated_bytes,
	       heap.allocated_bytes);
	for (index = 0u; index < BENCHMARK_RUNS; ++index) {
		printk("BENCH_SAMPLE {\"run\":%zu,\"cycles\":%llu,\"first_cycles\":%llu,"
		       "\"first_supplied_bytes\":%zu,\"count\":%u,\"checksum\":%u}\n",
		       index, (unsigned long long)cycles[index],
		       (unsigned long long)results[index].first_ticks,
		       results[index].first_supplied, results[index].count,
		       results[index].checksum);
	}
	selftest = bench_selftest();
	printk("BENCH_DONE {\"selftest\":%d}\n", selftest);
	return selftest != 0;
}
