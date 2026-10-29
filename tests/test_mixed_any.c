/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Runtime tests for mixed complex content and xs:anyType.
 */

#include "mixed_any.zxsd.h"
#include <stdio.h>

/** Validate one instance and compare its status with the expected result. */
static int expect(zxsd_result_t result, zxsd_error_t expected)
{
	if (result.code != expected) {
		fprintf(stderr, "wanted %d, got %d (%s) at %lu\n", (int)expected, (int)result.code,
			result.message, (unsigned long)result.offset);
	}
	return result.code == expected ? 0 : 1;
}

/** Exercise mixed content through the fixed-memory streaming API. */
static int test_streaming(void)
{
	zxsd_stream_t stream;
	zxsd_result_t result;

	result = mixed_any_p_stream_init(&stream);
	if (result.code != ZXSD_OK || zxsd_start_element(&stream, NULL, "p").code != ZXSD_OK ||
	    zxsd_text(&stream, "before ", 7).code != ZXSD_OK ||
	    zxsd_start_element(&stream, NULL, "em").code != ZXSD_OK ||
	    zxsd_text(&stream, "middle", 6).code != ZXSD_OK ||
	    zxsd_end_element(&stream, NULL, "em").code != ZXSD_OK ||
	    zxsd_text(&stream, " after", 6).code != ZXSD_OK ||
	    zxsd_end_element(&stream, NULL, "p").code != ZXSD_OK ||
	    zxsd_stream_finish(&stream).code != ZXSD_OK) {
		return 1;
	}

	result = mixed_any_anything_stream_init(&stream);
	if (result.code != ZXSD_OK ||
	    zxsd_start_element(&stream, NULL, "anything").code != ZXSD_OK ||
	    zxsd_start_element(&stream, NULL, "unknown").code != ZXSD_OK ||
	    zxsd_text(&stream, "free", 4).code != ZXSD_OK ||
	    zxsd_end_element(&stream, NULL, "unknown").code != ZXSD_OK ||
	    zxsd_end_element(&stream, NULL, "anything").code != ZXSD_OK ||
	    zxsd_stream_finish(&stream).code != ZXSD_OK) {
		return 1;
	}
	return 0;
}

/** Run mixed-content and anyType validation cases. */
int zxsd_test_mixed_any(void)
{
	int failures = test_streaming();

	failures += expect(mixed_any_p_validate("<p>before <em>middle</em> after</p>"), ZXSD_OK);
	failures += expect(mixed_any_p_validate("<p>text only</p>"), ZXSD_OK);
	failures += expect(mixed_any_p_validate("<p/>"), ZXSD_OK);
	failures += expect(mixed_any_p_validate("<p>text<other/>text</p>"),
			   ZXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(mixed_any_plain_validate("<plain>text</plain>"),
			   ZXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(
		mixed_any_anything_validate(
			"<anything free='yes'>text<unknown a='1'><nested/></unknown></anything>"),
		ZXSD_OK);
	failures += expect(mixed_any_implicit_validate("<implicit>text<unknown a='1'/></implicit>"),
			   ZXSD_OK);
	failures += expect(mixed_any_fixedAnything_validate("<fixedAnything>abc</fixedAnything>"),
			   ZXSD_OK);
	failures += expect(mixed_any_fixedAnything_validate("<fixedAnything>wrong</fixedAnything>"),
			   ZXSD_ERR_INVALID_VALUE);
	failures += expect(mixed_any_fixedMixed_validate("<fixedMixed>abc</fixedMixed>"), ZXSD_OK);
	failures += expect(mixed_any_fixedMixed_validate("<fixedMixed><em>abc</em></fixedMixed>"),
			   ZXSD_ERR_INVALID_VALUE);
	failures += expect(mixed_any_anything_validate("<anything><p><other/></p></anything>"),
			   ZXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(mixed_any_strict_validate("<strict id='7'><child>x</child></strict>"),
			   ZXSD_OK);
	failures +=
		expect(mixed_any_strict_validate("<strict id='7'>text<child>x</child></strict>"),
		       ZXSD_ERR_ELEMENT_ORDER);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xsi:type='Code'>42</anything>"),
		       ZXSD_OK);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xsi:type='Mixed'>text<em>x</em></anything>"),
		       ZXSD_OK);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xmlns:xs='http://www.w3.org/2001/XMLSchema' "
			       "xsi:type='xs:boolean'>1</anything>"),
		       ZXSD_OK);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xmlns:xs='http://www.w3.org/2001/XMLSchema' "
			       "xsi:type='xs:decimal'>1.5</anything>"),
		       ZXSD_OK);
	failures += expect(mixed_any_simple_validate(
				   "<simple xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
				   "xsi:type='AnySimpleCarrier' kind='text'>value</simple>"),
			   ZXSD_OK);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xmlns:xs='http://www.w3.org/2001/XMLSchema' "
			       "xsi:type='xs:anySimpleType'>value</anything>"),
		       ZXSD_OK);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xmlns:xs='http://www.w3.org/2001/XMLSchema' "
			       "xsi:type='xs:anyType'>value</anything>"),
		       ZXSD_OK);
	return failures ? 1 : 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_mixed_any();
}
#endif
