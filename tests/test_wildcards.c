/*
 * Copyright (c) 2026 Romain Pelletant
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Tests for recursive particles and XML Schema wildcards. */
#include "wildcards.mxsd.h"
#include <stdio.h>

/** Check a validation status. */
static int expect(mxsd_result_t result, mxsd_error_t wanted)
{
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d, got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Stream the nested particle fixture in either valid or invalid order. */
static mxsd_result_t stream_nested(int valid_order)
{
	static const char *const valid_names[] = {"a", "b", "c", "y", "x", "a"};
	static const char *const invalid_names[] = {"x"};
	const char *const *names = valid_order ? valid_names : invalid_names;
	size_t count = valid_order ? sizeof(valid_names) / sizeof(valid_names[0]) : 1u;
	mxsd_stream_t stream;
	mxsd_result_t result;
	size_t index;

	result = wildcards_nested_stream_init(&stream);
	if (result.code == MXSD_OK) {
		result = mxsd_start_element(&stream, "urn:mxsd:wildcards", "nested");
	}
	for (index = 0u; index < count && result.code == MXSD_OK; ++index) {
		result = mxsd_start_element(&stream, "urn:mxsd:wildcards", names[index]);
		if (result.code == MXSD_OK) {
			result = mxsd_text(&stream,
					   index == 1u   ? "2"
					   : index == 2u ? "true"
							 : "x",
					   index == 2u ? 4u : 1u);
		}
		if (result.code == MXSD_OK) {
			result = mxsd_end_element(&stream, "urn:mxsd:wildcards", names[index]);
		}
	}
	if (result.code == MXSD_OK) {
		result = mxsd_end_element(&stream, "urn:mxsd:wildcards", "nested");
	}
	if (result.code == MXSD_OK) {
		result = mxsd_stream_finish(&stream);
	}
	return result;
}

/** Exercise nested occurrence models and wildcard processing modes. */
int mxsd_test_wildcards(void)
{
	int failures = 0;
	failures += expect(
		wildcards_nested_validate("<nested "
					  "xmlns='urn:mxsd:wildcards'><a>one</a><b>2</b><c>true</"
					  "c><y>Y</y><x>X</x><a>two</a></nested>"),
		MXSD_OK);
	failures += expect(
		wildcards_nested_validate("<nested xmlns='urn:mxsd:wildcards'><x>X</x></nested>"),
		MXSD_ERR_ELEMENT_ORDER);
	failures += expect(stream_nested(1), MXSD_OK);
	failures += expect(stream_nested(0), MXSD_ERR_ELEMENT_ORDER);
	failures += expect(wildcards_strict_validate(
				   "<strict xmlns='urn:mxsd:wildcards'><known>3</known></strict>"),
			   MXSD_OK);
	failures += expect(
		wildcards_strict_validate("<strict xmlns='urn:mxsd:wildcards'><unknown/></strict>"),
		MXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(
		wildcards_strict_validate("<strict xmlns='urn:mxsd:wildcards' "
					  "xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
					  "xmlns:xs='http://www.w3.org/2001/XMLSchema'><unknown "
					  "xsi:type='xs:int'>3</unknown></strict>"),
		MXSD_OK);
	failures +=
		expect(wildcards_lax_validate(
			       "<w:lax xmlns:w='urn:mxsd:wildcards' xmlns:f='urn:foreign' "
			       "f:flag='yes' f:一00='yes'><f:item><f:nested/></f:item></w:lax>"),
		       MXSD_OK);
	failures += expect(
		wildcards_lax_validate("<lax xmlns='urn:mxsd:wildcards'><local xmlns=''/></lax>"),
		MXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(
		wildcards_skip_validate("<skip xmlns='urn:mxsd:wildcards'><anything "
					"xmlns='urn:any'><deep>text</deep></anything></skip>"),
		MXSD_OK);
	failures += expect(wildcards_skip_validate(
				   "<skip xmlns='urn:mxsd:wildcards'><!--before--><anything "
				   "xmlns='urn:any'><?inside test?><deep>text</deep></anything>"
				   "<!--after--></skip>"),
			   MXSD_OK);
	/* Skipped content may hold CDATA sections containing markup characters. */
	failures += expect(wildcards_skip_validate(
				   "<skip xmlns='urn:mxsd:wildcards'><anything xmlns='urn:any'>"
				   "a<![CDATA[</anything><x>]]><deep><![CDATA[]]></deep></anything>"
				   "</skip>"),
			   MXSD_OK);
	failures += expect(wildcards_skip_validate("<skip xmlns='urn:mxsd:wildcards'><anything "
						   "xmlns='urn:any'><![CDATA[open</anything>"
						   "</skip>"),
			   MXSD_ERR_MALFORMED_XML);
	failures += expect(wildcards_attributeStrict_validate(
				   "<attributeStrict xmlns='urn:mxsd:wildcards' "
				   "xmlns:w='urn:mxsd:wildcards' w:externalFlag='4'/>"),
			   MXSD_OK);
	failures += expect(wildcards_attributeStrict_validate(
				   "<attributeStrict xmlns='urn:mxsd:wildcards' "
				   "xmlns:w='urn:mxsd:wildcards' w:externalFlag='bad'/>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(
		wildcards_wildcardUnion_validate(
			"<wildcardUnion xmlns='urn:mxsd:wildcards' xmlns:w='urn:mxsd:wildcards' "
			"w:value='yes'/>"),
		MXSD_OK);
	failures += expect(wildcards_wildcardUnion_validate(
				   "<wildcardUnion xmlns='urn:mxsd:wildcards' value='no'/>"),
			   MXSD_ERR_UNEXPECTED_ATTRIBUTE);
	failures += expect(wildcards_wildcardIntersection_validate(
				   "<wildcardIntersection xmlns='urn:mxsd:wildcards' "
				   "xmlns:b='urn:b' b:value='yes'/>"),
			   MXSD_OK);
	failures += expect(wildcards_wildcardIntersection_validate(
				   "<wildcardIntersection xmlns='urn:mxsd:wildcards' value='no'/>"),
			   MXSD_ERR_UNEXPECTED_ATTRIBUTE);
	return failures ? 1 : 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_wildcards();
}
#endif
