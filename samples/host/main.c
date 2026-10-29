/*
 * Copyright (c) 2026 zxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Cross-platform host example for a generated zxsd validator.
 */

#include <stdio.h>

#include "sensor.zxsd.h"

/** Print a validation result and check it against the expected status. */
static int validate_document(const char *label, const char *xml, zxsd_error_t expected)
{
	zxsd_result_t result = sensor_validate(xml);

	printf("%s: %s", label, result.message);
	if (result.code != ZXSD_OK) {
		printf(" (offset=%lu, schema item=%s)", (unsigned long)result.offset,
		       result.path != NULL ? result.path : "unknown");
	}
	putchar('\n');

	return result.code == expected ? 0 : 1;
}

/** Validate the same schema from parser-style events. */
static int validate_events(void)
{
	zxsd_stream_t stream;
	zxsd_result_t result;
#define SEND(event)                                                                                \
	do {                                                                                       \
		result = (event);                                                                  \
		if (result.code != ZXSD_OK)                                                        \
			goto done;                                                                 \
	} while (0)
	SEND(sensor_stream_init(&stream));
	SEND(zxsd_start_element(&stream, NULL, "sensor"));
	SEND(zxsd_attribute(&stream, NULL, "id", "12"));
	SEND(zxsd_start_element(&stream, NULL, "name"));
	SEND(zxsd_text(&stream, "outside", 7u));
	SEND(zxsd_end_element(&stream, NULL, "name"));
	SEND(zxsd_start_element(&stream, NULL, "enabled"));
	SEND(zxsd_text(&stream, "true", 4u));
	SEND(zxsd_end_element(&stream, NULL, "enabled"));
	SEND(zxsd_start_element(&stream, NULL, "sample"));
	SEND(zxsd_text(&stream, "18", 2u));
	SEND(zxsd_end_element(&stream, NULL, "sample"));
	SEND(zxsd_end_element(&stream, NULL, "sensor"));
	result = zxsd_stream_finish(&stream);
done:
	printf("event stream: %s\n", result.message);
#undef SEND
	return result.code == ZXSD_OK ? 0 : 1;
}

/** Run one valid and one invalid XML validation example. */
int main(void)
{
	static const char valid_xml[] = "<sensor id='12'>"
					"<name>outside</name>"
					"<enabled>true</enabled>"
					"<sample>18</sample>"
					"<sample>19</sample>"
					"</sensor>";
	static const char invalid_xml[] = "<sensor id='12'>"
					  "<name>outside</name>"
					  "<enabled>sometimes</enabled>"
					  "<sample>18</sample>"
					  "</sensor>";
	int failures = 0;

	failures += validate_document("valid document", valid_xml, ZXSD_OK);
	failures += validate_document("invalid document", invalid_xml, ZXSD_ERR_INVALID_VALUE);
	failures += validate_events();
	return failures == 0 ? 0 : 1;
}
