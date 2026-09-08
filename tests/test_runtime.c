/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Host tests for core mxsd validation behavior.
 */

#include "device.mxsd.h"
#include <stdio.h>
#include <string.h>

/** Validate one document and compare its result with an expected status. */
static int expect(const char *xml, mxsd_error_t wanted)
{
	mxsd_result_t r = device_validate(xml);
	if (r.code != wanted) {
		fprintf(stderr, "wanted %d, got %d (%s) at %lu\n", (int)wanted, (int)r.code,
			r.message, (unsigned long)r.offset);
		return 1;
	}
	return 0;
}

/** Exercise the event-driven API without constructing an XML DOM. */
static int test_stream(void)
{
	mxsd_stream_t stream;
	mxsd_result_t r;
	char excessive[MXSD_STREAM_TEXT_CAPACITY + 1u];
#define EVENT(call)                                                                                \
	do {                                                                                       \
		r = (call);                                                                        \
		if (r.code != MXSD_OK) {                                                           \
			fprintf(stderr, "stream event failed: %s at %lu\n", r.message,             \
				(unsigned long)r.offset);                                          \
			return 1;                                                                  \
		}                                                                                  \
	} while (0)
	EVENT(device_stream_init(&stream));
	EVENT(mxsd_start_element(&stream, NULL, "device"));
	EVENT(mxsd_attribute(&stream, NULL, "id", "42"));
	EVENT(mxsd_start_content(&stream));
	EVENT(mxsd_start_element(&stream, NULL, "name"));
	EVENT(mxsd_text(&stream, "sensor", 6));
	EVENT(mxsd_end_element(&stream, NULL, "name"));
	EVENT(mxsd_start_element(&stream, NULL, "sample"));
	EVENT(mxsd_text(&stream, "7", 1));
	EVENT(mxsd_end_element(&stream, NULL, "sample"));
	EVENT(mxsd_end_element(&stream, NULL, "device"));
	EVENT(mxsd_stream_finish(&stream));

	EVENT(device_stream_init(&stream));
	EVENT(mxsd_start_element(&stream, NULL, "device"));
	EVENT(mxsd_attribute(&stream, NULL, "id", "1"));
	r = mxsd_end_element(&stream, NULL, "device");
	if (r.code != MXSD_ERR_MISSING_ELEMENT) {
		return 1;
	}

	memset(excessive, 'x', sizeof(excessive));
	EVENT(device_stream_init(&stream));
	EVENT(mxsd_start_element(&stream, NULL, "device"));
	EVENT(mxsd_attribute(&stream, NULL, "id", "1"));
	EVENT(mxsd_start_element(&stream, NULL, "name"));
	r = mxsd_text(&stream, excessive, sizeof(excessive));
	if (r.code != MXSD_ERR_STREAM_LIMIT) {
		return 1;
	}
#undef EVENT
	return 0;
}
/** Run the core host validation test cases. */
int main(void)
{
	int n = 0;
	n += test_stream();
	n += expect("<device "
		    "id='42'><name>sensor</name><enabled>true</enabled><sample>-2</"
		    "sample><sample>7</sample></device>",
		    MXSD_OK);
	n += expect("<device><name>x</name><sample>1</sample></device>",
		    MXSD_ERR_MISSING_ATTRIBUTE);
	n += expect("<device id='1'><sample>1</sample></device>", MXSD_ERR_ELEMENT_ORDER);
	n += expect("<device id='1'><sample>1</sample><name>x</name></device>",
		    MXSD_ERR_ELEMENT_ORDER);
	n += expect("<device id='1'><name>x</name><sample>bad</sample></device>",
		    MXSD_ERR_INVALID_VALUE);
	n += expect("<device id='4294967296'><name>x</name><sample>1</sample></device>",
		    MXSD_ERR_INVALID_VALUE);
	n += expect("<device id='1'><name>x</name><sample>2147483648</sample></device>",
		    MXSD_ERR_INVALID_VALUE);
	n += expect("<device "
		    "id='1'><name>x</name><sample>1</sample><sample>2</sample><sample>3</sample></"
		    "device>",
		    MXSD_ERR_OCCURRENCE);
	n += expect("<device id='1' extra='x'><name>x</name><sample>1</sample></device>",
		    MXSD_ERR_UNEXPECTED_ATTRIBUTE);
	n += expect("<device id='1'/>", MXSD_ERR_MISSING_ELEMENT);
	n += expect("<other/>", MXSD_ERR_WRONG_ROOT);
	return n ? 1 : 0;
}
