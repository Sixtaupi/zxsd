/*
 * Copyright (c) 2026 Romain Pelletant
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Tests for XSD 1.0 notation declarations and NOTATION values. */

#include "notation.mxsd.h"

#include <stdio.h>
#include <string.h>

static int expect(mxsd_result_t result, mxsd_error_t expected)
{
	if (result.code != expected) {
		fprintf(stderr, "wanted %d, got %d (%s)\n", (int)expected, (int)result.code,
			result.message);
		return 1;
	}
	return 0;
}

int mxsd_test_notation(void)
{
	const mxsd_schema_t *schema = notation_format_schema();
	int failures = 0;

	failures += expect(
		notation_format_validate("<f:format xmlns:f='urn:mxsd:notation-test' "
					 "xmlns:value='urn:mxsd:formats'>value:jpeg</f:format>"),
		MXSD_OK);
	failures += expect(
		notation_format_validate("<f:format xmlns:f='urn:mxsd:notation-test' "
					 "xmlns:value='urn:mxsd:formats'>value:png</f:format>"),
		MXSD_OK);
	failures += expect(
		notation_format_validate("<f:format xmlns:f='urn:mxsd:notation-test' "
					 "xmlns:value='urn:mxsd:formats'>value:gif</f:format>"),
		MXSD_ERR_INVALID_VALUE);
	failures += expect(
		notation_format_validate(
			"<f:format xmlns:f='urn:mxsd:notation-test'>missing:jpeg</f:format>"),
		MXSD_ERR_INVALID_VALUE);

	if (schema->notation_count != 2u || strcmp(schema->notations[0].name, "jpeg") != 0 ||
	    strcmp(schema->notations[0].public_identifier, "image/jpeg") != 0 ||
	    strcmp(schema->notations[0].system_identifier, "viewer-jpeg") != 0 ||
	    schema->notations[1].system_identifier != NULL) {
		fprintf(stderr, "generated notation metadata is incorrect\n");
		failures++;
	}
	return failures ? 1 : 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_notation();
}
#endif
