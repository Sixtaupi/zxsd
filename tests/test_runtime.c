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

struct xml_event_counts {
	size_t starts;
	size_t attributes;
	size_t texts;
	size_t ends;
};

/** Count decoded incremental-reader events without retaining transient values. */
static void count_xml_event(void *user_data, mxsd_xml_event_t event, const char *namespace_uri,
			    const char *local_name, const char *value, size_t value_length)
{
	struct xml_event_counts *counts = user_data;

	(void)namespace_uri;
	(void)local_name;
	(void)value;
	(void)value_length;
	if (event == MXSD_XML_EVENT_START_ELEMENT) {
		counts->starts++;
	} else if (event == MXSD_XML_EVENT_ATTRIBUTE) {
		counts->attributes++;
	} else if (event == MXSD_XML_EVENT_TEXT) {
		counts->texts++;
	} else if (event == MXSD_XML_EVENT_END_ELEMENT) {
		counts->ends++;
	}
}

/** Exercise raw XML parsing with every possible small fragment size. */
static int test_xml_reader(void)
{
	static mxsd_xml_reader_t reader;
	static const char xml[] = "<?xml version='1.0'?><!-- split anywhere -->"
				  "<device xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
				  "xsi:noNamespaceSchemaLocation='device.xsd' id='&#52;2'>"
				  "<name>sen<![CDATA[sor]]>&amp;</name><sample>7</sample></device>";
	size_t chunk;

	for (chunk = 1u; chunk <= 17u; ++chunk) {
		struct xml_event_counts counts = {0};
		size_t position = 0u;
		mxsd_result_t result = mxsd_xml_reader_init(&reader, device_schema());

		if (result.code != MXSD_OK) {
			return 1;
		}
		mxsd_xml_reader_set_event_callback(&reader, count_xml_event, &counts);
		while (position < sizeof(xml) - 1u) {
			size_t length = sizeof(xml) - 1u - position;

			if (length > chunk) {
				length = chunk;
			}
			result = mxsd_xml_reader_feed(&reader, xml + position, length);
			if (result.code != MXSD_OK) {
				fprintf(stderr, "XML reader chunk %lu failed: %s at %lu\n",
					(unsigned long)chunk, result.message,
					(unsigned long)result.offset);
				return 1;
			}
			position += length;
		}
		result = mxsd_xml_reader_finish(&reader);
		if (result.code != MXSD_OK || result.offset != sizeof(xml) - 1u ||
		    counts.starts != 3u || counts.ends != 3u || counts.attributes == 0u ||
		    counts.texts == 0u) {
			fprintf(stderr, "XML reader finish failed: %s at %lu\n", result.message,
				(unsigned long)result.offset);
			return 1;
		}
		mxsd_xml_reader_dispose(&reader);
	}

	if (mxsd_xml_reader_init(&reader, device_schema()).code != MXSD_OK ||
	    mxsd_xml_reader_feed(&reader, "<device id='1'><name>x", 22u).code != MXSD_OK ||
	    mxsd_xml_reader_finish(&reader).code != MXSD_ERR_MALFORMED_XML) {
		return 1;
	}
	if (mxsd_xml_reader_feed(&reader, "/></device>", 10u).code != MXSD_ERR_MALFORMED_XML) {
		return 1;
	}
	return 0;
}
/** Run the core host validation test cases. */
int mxsd_test_runtime(void)
{
	int n = 0;
	n += test_stream();
	n += test_xml_reader();
	n += expect("<device "
		    "id='42'><name>sensor</name><enabled>true</enabled><sample>-2</"
		    "sample><sample>7</sample></device>",
		    MXSD_OK);
	n += expect("<!--before--><?test value?><device id='42'><!--inside--><name>sensor</name>"
		    "<?next value?><sample>7</sample></device><!--after-->",
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

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_runtime();
}
#endif
