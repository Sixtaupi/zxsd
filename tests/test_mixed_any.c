/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Runtime tests for mixed complex content and xs:anyType.
 */

#include "mixed_any.mxsd.h"
#include <stdio.h>

/** Validate one instance and compare its status with the expected result. */
static int expect(mxsd_result_t result, mxsd_error_t expected)
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
	mxsd_stream_t stream;
	mxsd_result_t result;

	result = mixed_any_p_stream_init(&stream);
	if (result.code != MXSD_OK || mxsd_start_element(&stream, NULL, "p").code != MXSD_OK ||
	    mxsd_text(&stream, "before ", 7).code != MXSD_OK ||
	    mxsd_start_element(&stream, NULL, "em").code != MXSD_OK ||
	    mxsd_text(&stream, "middle", 6).code != MXSD_OK ||
	    mxsd_end_element(&stream, NULL, "em").code != MXSD_OK ||
	    mxsd_text(&stream, " after", 6).code != MXSD_OK ||
	    mxsd_end_element(&stream, NULL, "p").code != MXSD_OK ||
	    mxsd_stream_finish(&stream).code != MXSD_OK) {
		return 1;
	}

	result = mixed_any_anything_stream_init(&stream);
	if (result.code != MXSD_OK ||
	    mxsd_start_element(&stream, NULL, "anything").code != MXSD_OK ||
	    mxsd_start_element(&stream, NULL, "unknown").code != MXSD_OK ||
	    mxsd_text(&stream, "free", 4).code != MXSD_OK ||
	    mxsd_end_element(&stream, NULL, "unknown").code != MXSD_OK ||
	    mxsd_end_element(&stream, NULL, "anything").code != MXSD_OK ||
	    mxsd_stream_finish(&stream).code != MXSD_OK) {
		return 1;
	}
	return 0;
}

/** Run mixed-content and anyType validation cases. */
int mxsd_test_mixed_any(void)
{
	int failures = test_streaming();

	failures += expect(mixed_any_p_validate("<p>before <em>middle</em> after</p>"), MXSD_OK);
	failures += expect(mixed_any_p_validate("<p>text only</p>"), MXSD_OK);
	failures += expect(mixed_any_p_validate("<p/>"), MXSD_OK);
	failures += expect(mixed_any_p_validate("<p>text<other/>text</p>"),
			   MXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(mixed_any_plain_validate("<plain>text</plain>"),
			   MXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(
		mixed_any_anything_validate(
			"<anything free='yes'>text<unknown a='1'><nested/></unknown></anything>"),
		MXSD_OK);
	failures += expect(mixed_any_implicit_validate("<implicit>text<unknown a='1'/></implicit>"),
			   MXSD_OK);
	failures += expect(mixed_any_fixedAnything_validate("<fixedAnything>abc</fixedAnything>"),
			   MXSD_OK);
	failures += expect(mixed_any_fixedAnything_validate("<fixedAnything>wrong</fixedAnything>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(mixed_any_fixedMixed_validate("<fixedMixed>abc</fixedMixed>"), MXSD_OK);
	failures += expect(mixed_any_fixedMixed_validate("<fixedMixed><em>abc</em></fixedMixed>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(mixed_any_anything_validate("<anything><p><other/></p></anything>"),
			   MXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(mixed_any_strict_validate("<strict id='7'><child>x</child></strict>"),
			   MXSD_OK);
	failures +=
		expect(mixed_any_strict_validate("<strict id='7'>text<child>x</child></strict>"),
		       MXSD_ERR_ELEMENT_ORDER);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xsi:type='Code'>42</anything>"),
		       MXSD_OK);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xsi:type='Mixed'>text<em>x</em></anything>"),
		       MXSD_OK);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xmlns:xs='http://www.w3.org/2001/XMLSchema' "
			       "xsi:type='xs:boolean'>1</anything>"),
		       MXSD_OK);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xmlns:xs='http://www.w3.org/2001/XMLSchema' "
			       "xsi:type='xs:decimal'>1.5</anything>"),
		       MXSD_OK);
	failures += expect(mixed_any_simple_validate(
				   "<simple xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
				   "xsi:type='AnySimpleCarrier' kind='text'>value</simple>"),
			   MXSD_OK);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xmlns:xs='http://www.w3.org/2001/XMLSchema' "
			       "xsi:type='xs:anySimpleType'>value</anything>"),
		       MXSD_OK);
	failures +=
		expect(mixed_any_anything_validate(
			       "<anything xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			       "xmlns:xs='http://www.w3.org/2001/XMLSchema' "
			       "xsi:type='xs:anyType'>value</anything>"),
		       MXSD_OK);
	return failures ? 1 : 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_mixed_any();
}
#endif
