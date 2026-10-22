/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Runtime tests for complete XSD 1.0 regular expressions. */

#include "regex.mxsd.h"
#include <stdio.h>

static int expect(mxsd_result_t result, mxsd_error_t expected)
{
	if (result.code != expected) {
		fprintf(stderr, "wanted %d, got %d (%s) at %lu\n", (int)expected, (int)result.code,
			result.message, (unsigned long)result.offset);
	}
	return result.code == expected ? 0 : 1;
}

int mxsd_test_regex(void)
{
	int failures = 0;

	failures += expect(regex_choice_validate("<choice>dog-X</choice>"), MXSD_OK);
	failures += expect(regex_choice_validate("<choice>cat-42</choice>"), MXSD_OK);
	failures += expect(regex_choice_validate("<choice>cat-&#52;&#x32;</choice>"), MXSD_OK);
	failures +=
		expect(regex_choice_validate("<choice>xcat-42</choice>"), MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_choice_validate("<choice>dog-Y</choice>"), MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_consonants_validate("<consonants>rhythm</consonants>"), MXSD_OK);
	failures += expect(regex_consonants_validate("<consonants>apple</consonants>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_word_validate("<word>Éclair</word>"), MXSD_OK);
	failures += expect(regex_word_validate("<word>𐐀𐐨</word>"), MXSD_OK);
	failures += expect(regex_word_validate("<word>éclair</word>"), MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_greek_validate("<greek>Ωμέγα</greek>"), MXSD_OK);
	failures += expect(regex_greek_validate("<greek>Greek</greek>"), MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_name_validate("<name>node-1</name>"), MXSD_OK);
	failures += expect(regex_name_validate("<name>\xe3\x80\x87</name>"), MXSD_OK);
	failures += expect(regex_name_validate("<name>1node</name>"), MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_name_validate("<name>A\xcd\x86</name>"), MXSD_ERR_INVALID_VALUE);
	/* U+0370 is accepted by XML 1.0 Fifth Edition names, but is not a Unicode
	 * letter and therefore is outside the XSD 1.0 \i/\c escapes. */
	failures += expect(regex_name_validate("<name>\xcd\xb0</name>"), MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_not_digits_validate("<not-digits>letters</not-digits>"), MXSD_OK);
	failures += expect(regex_not_digits_validate("<not-digits>a1</not-digits>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_repeat_validate("<repeat>abab</repeat>"), MXSD_OK);
	failures += expect(regex_repeat_validate("<repeat>ababab</repeat>"), MXSD_OK);
	failures += expect(regex_repeat_validate("<repeat>ab</repeat>"), MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_color_validate("<color>red</color>"), MXSD_OK);
	failures += expect(regex_color_validate("<color>blue</color>"), MXSD_OK);
	failures += expect(regex_color_validate("<color>green</color>"), MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_blue_validate("<blue>blue</blue>"), MXSD_OK);
	failures += expect(regex_blue_validate("<blue>red</blue>"), MXSD_ERR_INVALID_VALUE);
	failures +=
		expect(regex_empty_alternative_validate("<empty-alternative></empty-alternative>"),
		       MXSD_OK);
	failures += expect(
		regex_empty_alternative_validate("<empty-alternative>none</empty-alternative>"),
		MXSD_OK);
	failures += expect(
		regex_empty_alternative_validate("<empty-alternative>other</empty-alternative>"),
		MXSD_ERR_INVALID_VALUE);
	failures += expect(
		regex_negative_class_validate("<negative-class>letters</negative-class>"), MXSD_OK);
	failures += expect(regex_negative_class_validate("<negative-class>a1</negative-class>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(
		regex_literal_operators_validate("<literal-operators>^cost$?</literal-operators>"),
		MXSD_OK);
	failures += expect(
		regex_literal_operators_validate("<literal-operators>cost</literal-operators>"),
		MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_current_greek_block_validate(
				   "<current-greek-block>\xce\xa9</current-greek-block>"),
			   MXSD_OK);
	failures += expect(regex_xsd10_digit_validate("<xsd10-digit>\xe1\x8d\xa9</xsd10-digit>"),
			   MXSD_OK);
	failures += expect(regex_xsd10_digit_validate("<xsd10-digit>A</xsd10-digit>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(regex_xsd10_digit_validate("<xsd10-digit>\xe0\xaf\xa6</xsd10-digit>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(
		regex_xsd10_non_digit_validate("<xsd10-non-digit>\xe0\xaf\xa6</xsd10-non-digit>"),
		MXSD_OK);
	failures += expect(regex_xsd10_word_validate("<xsd10-word>\xc8\xbf</xsd10-word>"),
			   MXSD_ERR_INVALID_VALUE);
	failures +=
		expect(regex_attribute_entity_validate("<attribute-entity value='&#x20;&#x20;b'/>"),
		       MXSD_OK);
	return failures ? 1 : 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_regex();
}
#endif
