/*
 * Copyright (c) 2026 Sixtaupi
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Tests schema sets containing named types but no global element. */
#include "declarations.zxsd.h"
#include <stdio.h>
#include <string.h>

/** Check a validation result. */
static int expect(zxsd_result_t result, zxsd_error_t wanted)
{
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d, got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Stream an arbitrary root assessed through xsi:type. */
static zxsd_result_t stream_code(const char *value, const char *type_namespace,
				 const char *type_name)
{
	zxsd_stream_t stream;
	zxsd_result_t result = declarations_stream_init(&stream);

	if (result.code == ZXSD_OK) {
		result = zxsd_start_element(&stream, "urn:instance", "item");
	}
	if (result.code == ZXSD_OK && type_name != NULL) {
		result = zxsd_xsi_type(&stream, type_namespace, type_name);
	}
	if (result.code == ZXSD_OK) {
		result = zxsd_text(&stream, value, strlen(value));
	}
	if (result.code == ZXSD_OK) {
		result = zxsd_end_element(&stream, "urn:instance", "item");
	}
	if (result.code == ZXSD_OK) {
		result = zxsd_stream_finish(&stream);
	}
	return result;
}

/** Exercise DOM and streaming unbound-root assessment. */
int zxsd_test_declarations(void)
{
	static const char xsd[] = "http://www.w3.org/2001/XMLSchema";
	int failures = 0;

	failures += expect(
		declarations_validate("<i:item xmlns:i='urn:instance' "
				      "xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
				      "xsi:type='Code'>AB</i:item>"),
		ZXSD_OK);
	failures += expect(stream_code("AB", NULL, "Code"), ZXSD_OK);
	failures += expect(stream_code("Ab", NULL, "Code"), ZXSD_ERR_INVALID_VALUE);
	failures += expect(stream_code("42", xsd, "int"), ZXSD_OK);
	failures += expect(stream_code("text", xsd, "int"), ZXSD_ERR_INVALID_VALUE);
	failures += expect(stream_code("AB", NULL, NULL), ZXSD_ERR_INVALID_VALUE);
	return failures ? 1 : 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_declarations();
}
#endif
