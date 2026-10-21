/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Host tests for the optional Mini-XML 4 adapter.
 */

#include "device.mxsd.h"
#include <mxsd_mxml.h>
#include <stdio.h>

/** Parse one document with Mini-XML and validate the resulting tree. */
static int expect(const char *xml, mxsd_error_t wanted)
{
	mxml_options_t *options = mxmlOptionsNew();
	mxml_node_t *document;
	mxsd_result_t result;

	if (options == NULL) {
		fprintf(stderr, "could not allocate Mini-XML options\n");
		return 1;
	}
	mxmlOptionsSetTypeValue(options, MXML_TYPE_OPAQUE);
	document = mxmlLoadString(NULL, options, xml);
	mxmlOptionsDelete(options);
	if (document == NULL) {
		fprintf(stderr, "Mini-XML rejected the test document\n");
		return 1;
	}
	result = mxsd_mxml_validate(device_schema(), document);
	mxmlDelete(document);
	if (result.code != wanted) {
		fprintf(stderr, "wanted %d, got %d (%s)\n", (int)wanted, (int)result.code,
			result.message);
		return 1;
	}
	return 0;
}

/** Exercise successful validation and an adapter-visible validation error. */
int main(void)
{
	int failures = 0;

	failures +=
		expect("<device id='42'><name>sensor</name><sample>7</sample></device>", MXSD_OK);
	failures += expect("<device id='42'><sample>7</sample><name>sensor</name></device>",
			   MXSD_ERR_ELEMENT_ORDER);
	return failures == 0 ? 0 : 1;
}
