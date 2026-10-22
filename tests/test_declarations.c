/*
 * Copyright (c) 2026 Romain Pelletant
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Tests schema sets containing named types but no global element. */
#include "declarations.mxsd.h"
#include <stdio.h>
#include <string.h>

/** Check a validation result. */
static int expect(mxsd_result_t result, mxsd_error_t wanted)
{
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d, got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Stream an arbitrary root assessed through xsi:type. */
static mxsd_result_t stream_code(const char *value, const char *type_namespace,
				 const char *type_name)
{
	mxsd_stream_t stream;
	mxsd_result_t result = declarations_stream_init(&stream);

	if (result.code == MXSD_OK) {
		result = mxsd_start_element(&stream, "urn:instance", "item");
	}
	if (result.code == MXSD_OK && type_name != NULL) {
		result = mxsd_xsi_type(&stream, type_namespace, type_name);
	}
	if (result.code == MXSD_OK) {
		result = mxsd_text(&stream, value, strlen(value));
	}
	if (result.code == MXSD_OK) {
		result = mxsd_end_element(&stream, "urn:instance", "item");
	}
	if (result.code == MXSD_OK) {
		result = mxsd_stream_finish(&stream);
	}
	return result;
}

/** Exercise DOM and streaming unbound-root assessment. */
int mxsd_test_declarations(void)
{
	static const char xsd[] = "http://www.w3.org/2001/XMLSchema";
	int failures = 0;

	failures += expect(
		declarations_validate("<i:item xmlns:i='urn:instance' "
				      "xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
				      "xsi:type='Code'>AB</i:item>"),
		MXSD_OK);
	failures += expect(stream_code("AB", NULL, "Code"), MXSD_OK);
	failures += expect(stream_code("Ab", NULL, "Code"), MXSD_ERR_INVALID_VALUE);
	failures += expect(stream_code("42", xsd, "int"), MXSD_OK);
	failures += expect(stream_code("text", xsd, "int"), MXSD_ERR_INVALID_VALUE);
	failures += expect(stream_code("AB", NULL, NULL), MXSD_ERR_INVALID_VALUE);
	return failures ? 1 : 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_declarations();
}
#endif
