/*
 * Copyright (c) 2026 mxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Reject nonvalidating or value-discarding benchmark adapters. */

#include "bench.h"

#include <string.h>

#define HEAD "<b:samples xmlns:b='urn:mxsd:benchmark'>"
#define TAIL "</b:samples>"
#define ROW                                                                                        \
	"<b:sample id='0007'><b:label>sensor_A</b:label>"                                          \
	"<b:enabled>1</b:enabled><b:reading> -12 </b:reading></b:sample>"

int bench_selftest(void)
{
	static const char valid[] = HEAD ROW ROW TAIL;
	static const char *const invalid[] = {
		HEAD TAIL, /* minOccurs */
		HEAD "<b:sample id='7'><b:label>!</b:label><b:enabled>true</b:enabled>"
		     "<b:reading>1</b:reading></b:sample>" TAIL, /* pattern facet */
		HEAD "<b:sample id='7'><b:label></b:label><b:enabled>true</b:enabled>"
		     "<b:reading>1</b:reading></b:sample>" TAIL, /* length facet */
		HEAD "<b:sample id='4294967296'><b:label>a</b:label><b:enabled>true</b:enabled>"
		     "<b:reading>1</b:reading></b:sample>" TAIL, /* native uint32 bound */
		HEAD "<b:sample id='7'><b:label>a</b:label><b:enabled>maybe</b:enabled>"
		     "<b:reading>1</b:reading></b:sample>" TAIL,
		HEAD "<b:sample id='7'><b:label>a</b:label><b:enabled>true</b:enabled>"
		     "<b:reading>2147483648</b:reading></b:sample>" TAIL,
		HEAD "<b:sample id='7'><b:label>a</b:label><b:enabled>true</b:enabled>"
		     "</b:sample>" TAIL,         /* required reading */
		HEAD ROW "<b:unexpected/>" TAIL, /* error after a provisional delivery */
		HEAD ROW,                        /* missing root end tag */
		"<samples xmlns='wrong'/>",
		"",
	};
	static const size_t blocks[] = {1u, 7u, 4096u};
	bench_observer_t expected;
	bench_observer_t actual;
	size_t block;
	size_t index;

	bench_observer_init(&expected);
	bench_consume(&expected, 7u, "sensor_A", 1, -12);
	bench_consume(&expected, 7u, "sensor_A", 1, -12);
	for (block = 0u; block < sizeof(blocks) / sizeof(blocks[0]); ++block) {
		bench_observer_init(&actual);
		actual.started = bench_ticks();
		if (bench_parse((const unsigned char *)valid, strlen(valid), blocks[block],
				&actual) ||
		    actual.count != 2u || actual.checksum != expected.checksum ||
		    (blocks[block] == 1u && actual.first_supplied >= strlen(valid))) {
			return -1;
		}
		for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
			bench_observer_init(&actual);
			if (bench_parse((const unsigned char *)invalid[index],
					strlen(invalid[index]), blocks[block], &actual) == 0) {
				return (int)index + 1;
			}
		}
	}
	return 0;
}
