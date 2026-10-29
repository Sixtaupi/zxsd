/*
 * Copyright (c) 2026 zxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file @brief Offline tests of the real MET schema and the sample's typed callback. */

#include "weather_summary.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(value)                                                                               \
	do {                                                                                       \
		if (!(value)) {                                                                    \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value);                \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

static met_weather_weatherdata_product_time_reader_t reader;
static met_weather_timeType_t forecast;
static unsigned char storage[4096];

/** Feed fragments without keeping the complete HTTP response. */
static zxsd_result_t feed(const char *xml)
{
	size_t length = strlen(xml);
	size_t offset = 0u;
	zxsd_result_t result = reader.stream.xml.result;

	while (offset < length && result.code == ZXSD_OK) {
		size_t count = length - offset > 7u ? 7u : length - offset;

		result = met_weather_weatherdata_product_time_reader_feed(&reader, xml + offset,
									  count);
		offset += count;
	}
	return result;
}

/** Select a complete forecast, then continue validating hundreds of later forecasts. */
static void weather(int invalid_later)
{
	struct weather_summary summary = {0};
	zxsd_bind_arena_t arena;
	zxsd_result_t result;
	size_t index;
	size_t iterations = ZXSD_MAX_INPUT > 262144u ? 512u : 4u;

	zxsd_bind_arena_init(&arena, storage, sizeof(storage));
	result = met_weather_weatherdata_product_time_reader_init(
		&reader, &forecast, &arena, weather_summary_capture, &summary, NULL);
	CHECK(result.code == ZXSD_OK);
	result = feed("<weatherdata created='2026-10-24T12:00:00Z'><product class='pointData'>");
	CHECK(result.code == ZXSD_OK);
	/* An incomplete first forecast must not produce a summary. */
	result = feed("<time from='2026-10-24T12:00:00Z' to='2026-10-24T12:00:00Z'>"
		      "<location><pressure unit='hPa' value='1010'/></location></time>");
	CHECK(result.code == ZXSD_OK && !summary.complete && arena.used == 0u);
	result = feed(
		"<time from='2026-10-24T13:00:00Z' to='2026-10-24T13:00:00Z'>"
		"<location latitude='45.7578' longitude='4.8320'>"
		"<pressure unit='hPa' value='1013.5'/><cloudiness percent='25'/>"
		"<temperature unit='celsius' value='12.5'/><humidity unit='percent' value='65'/>"
		"<windSpeed mps='3.2' name='Light &amp; gentle'/></location></time>");
	CHECK(result.code == ZXSD_OK && summary.complete && arena.used == 0u);
	CHECK(strcmp(summary.time, "2026-10-24T13:00:00Z") == 0);
	CHECK(strcmp(summary.temperature, "12.5") == 0 &&
	      strcmp(summary.temperature_unit, "celsius") == 0);
	CHECK(strcmp(summary.humidity, "65") == 0 && strcmp(summary.humidity_unit, "percent") == 0);
	CHECK(strcmp(summary.wind_speed, "3.2") == 0 &&
	      strcmp(summary.wind_name, "Light & gentle") == 0);
	CHECK(strcmp(summary.pressure, "1013.5") == 0 && strcmp(summary.pressure_unit, "hPa") == 0);
	CHECK(strcmp(summary.cloudiness, "25") == 0);
	for (index = 0u; index < iterations && result.code == ZXSD_OK; ++index) {
		result = feed(
			"<time from='2026-10-24T14:00:00Z' to='2026-10-24T14:00:00Z'><location>");
		if (result.code == ZXSD_OK) {
			result = feed(invalid_later && index + 1u == iterations
					      ? "<temperature unit='celsius' value='invalid'/>"
					      : "<temperature unit='celsius' value='13'/>");
		}
		if (result.code == ZXSD_OK) {
			result = feed("</location></time>");
		}
		CHECK(arena.used == 0u);
	}
	if (result.code == ZXSD_OK) {
		result = feed("</product></weatherdata>");
	}
	if (result.code == ZXSD_OK) {
		result = met_weather_weatherdata_product_time_reader_finish(&reader);
	}
	CHECK(result.code == (invalid_later ? ZXSD_ERR_INVALID_VALUE : ZXSD_OK));
	CHECK(strcmp(summary.temperature, "12.5") == 0);
	met_weather_weatherdata_product_time_reader_dispose(&reader);
	CHECK(arena.used == 0u && forecast.location == NULL);
}

/** The entire selected forecast must fit; neither arrays nor display values are truncated. */
static void limits(void)
{
	struct weather_summary summary = {0};
	zxsd_bind_arena_t arena;
	zxsd_result_t result;

	zxsd_bind_arena_init(&arena, storage, 32u);
	CHECK(met_weather_weatherdata_product_time_reader_init(
		      &reader, &forecast, &arena, weather_summary_capture, &summary, NULL)
		      .code == ZXSD_OK);
	result = feed("<weatherdata created='2026-10-24T12:00:00Z'><product class='pointData'>"
		      "<time from='2026-10-24T12:00:00Z' to='2026-10-24T13:00:00Z'><location/>");
	CHECK(result.code == ZXSD_ERR_RESOURCE_LIMIT && !summary.complete && arena.used == 0u);
	met_weather_weatherdata_product_time_reader_dispose(&reader);
}

/** Shared entry point used by both CTest and the Zephyr extern wrapper. */
int zxsd_test_weather_binding(void)
{
	failures = 0;
	weather(0);
	weather(1);
	limits();
	return failures != 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_weather_binding();
}
#endif
