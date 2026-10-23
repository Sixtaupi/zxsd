/*
 * Copyright (c) 2026 mxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Common value sink and protocol for validating streaming bindings. */

#ifndef MXSD_BENCH_STREAM_H
#define MXSD_BENCH_STREAM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bounded result sink; no array proportional to the document's sample count. */
typedef struct {
	uint64_t started;      /**< Clock before parser construction/initialization. */
	uint64_t first_ticks;  /**< Time to first complete, provisionally valid sample. */
	size_t supplied;       /**< End of the input block currently being consumed. */
	size_t first_supplied; /**< Input supplied when the first sample was delivered. */
	uint32_t count;        /**< Number of complete samples delivered. */
	uint32_t checksum;     /**< FNV-1a over canonical values, not the XML text. */
} bench_observer_t;

/** @brief Read the benchmark clock; target cycles in Zephyr, fake ticks in unit tests. */
uint64_t bench_ticks(void);

/** @brief Initialize the common sink before starting a complete parse. */
void bench_observer_init(bench_observer_t *observer);

/**
 * @brief Consume the same typed values from either implementation.
 * @param[in,out] observer Common bounded sink.
 * @param[in] id Native unsigned 32-bit identifier.
 * @param[in] label Decoded UTF-8 label, valid during this call.
 * @param[in] enabled Native boolean, encoded canonically as zero or one.
 * @param[in] reading Native signed 32-bit reading.
 */
void bench_consume(bench_observer_t *observer, uint32_t id, const char *label, int enabled,
		   int32_t reading);

/**
 * @brief Validate the full document and deliver typed samples as they close.
 * @param[in] xml Complete offline fixture, supplied in bounded fragments.
 * @param[in] length Fixture length.
 * @param[in] block_size Common maximum feed length; must be positive.
 * @param[in,out] observer Initialized common sink.
 * @return Zero only after full document validation; nonzero on any error.
 */
int bench_parse(const unsigned char *xml, size_t length, size_t block_size,
		bench_observer_t *observer);

/** @brief Report statically retained parser/object/arena storage, excluding stack and heap. */
size_t bench_static_context_bytes(void);

/** @brief Run common positive/negative XML checks; return zero on success. */
int bench_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
