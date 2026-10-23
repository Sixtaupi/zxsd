/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Check the real weather schema with bounded fragmented input. */

#include "met_weather.mxsd.h"

#include <stdio.h>
#include <string.h>

static mxsd_xml_reader_t reader;

/** Feed a literal through small fragments without retaining the whole response. */
static mxsd_result_t feed(const char *text)
{
	size_t offset = 0u;
	size_t length = strlen(text);
	mxsd_result_t result = reader.result;

	while (result.code == MXSD_OK && offset < length) {
		size_t count = length - offset;

		if (count > 7u) {
			count = 7u;
		}
		result = mxsd_xml_reader_feed(&reader, text + offset, count);
		offset += count;
	}
	return result;
}

/** Exercise product/time repetition and repeated optional terms inside location. */
static mxsd_result_t weather(int invalid)
{
	mxsd_result_t result = mxsd_xml_reader_init(&reader, met_weather_schema());
	size_t index;

	if (result.code != MXSD_OK) {
		return result;
	}
	result = feed("<weatherdata created='2026-10-24T12:00:00Z'><product class='pointData'>");
	for (index = 0u; index < 512u && result.code == MXSD_OK; ++index) {
		result = feed("<time from='2026-10-24T12:00:00Z' to='2026-10-24T13:00:00Z'>"
			      "<location latitude='45.7578' longitude='4.8320' altitude='90'>");
		if (result.code == MXSD_OK) {
			result = feed(invalid ? "<temperature unit='celsius' value='bad'/>"
					      : "<temperature unit='celsius' value='12.5'/>");
		}
		if (result.code == MXSD_OK) {
			/* humidity followed by temperature starts a second sequence iteration. */
			result = feed("<humidity unit='percent' value='65'/>"
				      "<temperature unit='celsius' value='13'/></location></time>");
		}
	}
	if (result.code == MXSD_OK) {
		result = feed("</product></weatherdata>");
	}
	if (result.code == MXSD_OK) {
		result = mxsd_xml_reader_finish(&reader);
	}
	mxsd_xml_reader_dispose(&reader);
	return result;
}

int main(void)
{
	mxsd_result_t result = weather(0);

	if (result.code != MXSD_OK) {
		fprintf(stderr, "weather: %s at %lu\n", result.message,
			(unsigned long)result.offset);
		return 1;
	}
	printf("weather: 512 forecasts, %lu input bytes, %lu reader bytes, history capacity %u\n",
	       (unsigned long)reader.offset, (unsigned long)sizeof(reader),
	       (unsigned int)MXSD_STREAM_CHILD_CAPACITY);
	result = weather(1);
	return result.code == MXSD_ERR_INVALID_VALUE ? 0 : 1;
}
