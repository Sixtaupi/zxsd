/*
 * Copyright (c) 2026 Sixtaupi
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Tests for recursive particles and XML Schema wildcards. */
#include "wildcards.zxsd.h"
#include <stdio.h>

/** Check a validation status. */
static int expect(zxsd_result_t result, zxsd_error_t wanted)
{
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d, got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Stream the nested particle fixture in either valid or invalid order. */
static zxsd_result_t stream_nested(int valid_order)
{
	static const char *const valid_names[] = {"a", "b", "c", "y", "x", "a"};
	static const char *const invalid_names[] = {"x"};
	const char *const *names = valid_order ? valid_names : invalid_names;
	size_t count = valid_order ? sizeof(valid_names) / sizeof(valid_names[0]) : 1u;
	zxsd_stream_t stream;
	zxsd_result_t result;
	size_t index;

	result = wildcards_nested_stream_init(&stream);
	if (result.code == ZXSD_OK) {
		result = zxsd_start_element(&stream, "urn:zxsd:wildcards", "nested");
	}
	for (index = 0u; index < count && result.code == ZXSD_OK; ++index) {
		result = zxsd_start_element(&stream, "urn:zxsd:wildcards", names[index]);
		if (result.code == ZXSD_OK) {
			result = zxsd_text(&stream,
					   index == 1u   ? "2"
					   : index == 2u ? "true"
							 : "x",
					   index == 2u ? 4u : 1u);
		}
		if (result.code == ZXSD_OK) {
			result = zxsd_end_element(&stream, "urn:zxsd:wildcards", names[index]);
		}
	}
	if (result.code == ZXSD_OK) {
		result = zxsd_end_element(&stream, "urn:zxsd:wildcards", "nested");
	}
	if (result.code == ZXSD_OK) {
		result = zxsd_stream_finish(&stream);
	}
	return result;
}

/** Exercise nested occurrence models and wildcard processing modes. */
int zxsd_test_wildcards(void)
{
	int failures = 0;
	failures += expect(
		wildcards_nested_validate("<nested "
					  "xmlns='urn:zxsd:wildcards'><a>one</a><b>2</b><c>true</"
					  "c><y>Y</y><x>X</x><a>two</a></nested>"),
		ZXSD_OK);
	failures += expect(
		wildcards_nested_validate("<nested xmlns='urn:zxsd:wildcards'><x>X</x></nested>"),
		ZXSD_ERR_ELEMENT_ORDER);
	failures += expect(stream_nested(1), ZXSD_OK);
	failures += expect(stream_nested(0), ZXSD_ERR_ELEMENT_ORDER);
	failures += expect(wildcards_strict_validate(
				   "<strict xmlns='urn:zxsd:wildcards'><known>3</known></strict>"),
			   ZXSD_OK);
	failures += expect(
		wildcards_strict_validate("<strict xmlns='urn:zxsd:wildcards'><unknown/></strict>"),
		ZXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(
		wildcards_strict_validate("<strict xmlns='urn:zxsd:wildcards' "
					  "xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
					  "xmlns:xs='http://www.w3.org/2001/XMLSchema'><unknown "
					  "xsi:type='xs:int'>3</unknown></strict>"),
		ZXSD_OK);
	failures +=
		expect(wildcards_lax_validate(
			       "<w:lax xmlns:w='urn:zxsd:wildcards' xmlns:f='urn:foreign' "
			       "f:flag='yes' f:一00='yes'><f:item><f:nested/></f:item></w:lax>"),
		       ZXSD_OK);
	failures += expect(
		wildcards_lax_validate("<lax xmlns='urn:zxsd:wildcards'><local xmlns=''/></lax>"),
		ZXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(
		wildcards_skip_validate("<skip xmlns='urn:zxsd:wildcards'><anything "
					"xmlns='urn:any'><deep>text</deep></anything></skip>"),
		ZXSD_OK);
	failures += expect(wildcards_skip_validate(
				   "<skip xmlns='urn:zxsd:wildcards'><!--before--><anything "
				   "xmlns='urn:any'><?inside test?><deep>text</deep></anything>"
				   "<!--after--></skip>"),
			   ZXSD_OK);
	/* Skipped content may hold CDATA sections containing markup characters. */
	failures += expect(wildcards_skip_validate(
				   "<skip xmlns='urn:zxsd:wildcards'><anything xmlns='urn:any'>"
				   "a<![CDATA[</anything><x>]]><deep><![CDATA[]]></deep></anything>"
				   "</skip>"),
			   ZXSD_OK);
	failures += expect(wildcards_skip_validate("<skip xmlns='urn:zxsd:wildcards'><anything "
						   "xmlns='urn:any'><![CDATA[open</anything>"
						   "</skip>"),
			   ZXSD_ERR_MALFORMED_XML);
	failures += expect(wildcards_attributeStrict_validate(
				   "<attributeStrict xmlns='urn:zxsd:wildcards' "
				   "xmlns:w='urn:zxsd:wildcards' w:externalFlag='4'/>"),
			   ZXSD_OK);
	failures += expect(wildcards_attributeStrict_validate(
				   "<attributeStrict xmlns='urn:zxsd:wildcards' "
				   "xmlns:w='urn:zxsd:wildcards' w:externalFlag='bad'/>"),
			   ZXSD_ERR_INVALID_VALUE);
	failures += expect(
		wildcards_wildcardUnion_validate(
			"<wildcardUnion xmlns='urn:zxsd:wildcards' xmlns:w='urn:zxsd:wildcards' "
			"w:value='yes'/>"),
		ZXSD_OK);
	failures += expect(wildcards_wildcardUnion_validate(
				   "<wildcardUnion xmlns='urn:zxsd:wildcards' value='no'/>"),
			   ZXSD_ERR_UNEXPECTED_ATTRIBUTE);
	failures += expect(wildcards_wildcardIntersection_validate(
				   "<wildcardIntersection xmlns='urn:zxsd:wildcards' "
				   "xmlns:b='urn:b' b:value='yes'/>"),
			   ZXSD_OK);
	failures += expect(wildcards_wildcardIntersection_validate(
				   "<wildcardIntersection xmlns='urn:zxsd:wildcards' value='no'/>"),
			   ZXSD_ERR_UNEXPECTED_ATTRIBUTE);
	return failures ? 1 : 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_wildcards();
}
#endif
