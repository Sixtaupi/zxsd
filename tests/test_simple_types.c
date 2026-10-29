/*
 * Copyright (c) 2026 Sixtaupi
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Tests for XSD 1.0 list, union and built-in simple types. */
#include "simple_types.zxsd.h"
#include <stdio.h>

/** Assert that a generated validator accepts or rejects one document. */
#define CHECK(call, wanted)                                                                        \
	do {                                                                                       \
		zxsd_result_t r_ = (call);                                                         \
		if (r_.code != (wanted)) {                                                         \
			fprintf(stderr, "%s:%d wanted %d got %d\n", __FILE__, __LINE__,            \
				(int)(wanted), (int)r_.code);                                      \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

/** Run the complete-simple-type conformance cases. */
int zxsd_test_simple_types(void)
{
	int failures = 0;
	CHECK(simple_types_list_validate("<list> 1\t -2 </list>"), ZXSD_OK);
	CHECK(simple_types_list_validate("<list>1 2 3</list>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_union_validate("<union>-4</union>"), ZXSD_OK);
	CHECK(simple_types_union_validate("<union>a:b</union>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_inlineUnion_validate("<inlineUnion>yes</inlineUnion>"), ZXSD_OK);
	CHECK(simple_types_inlineUnion_validate("<inlineUnion>false</inlineUnion>"), ZXSD_OK);
	CHECK(simple_types_inlineUnion_validate("<inlineUnion>no</inlineUnion>"),
	      ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_collapsed_validate("<collapsed> a\t b </collapsed>"), ZXSD_OK);
	CHECK(simple_types_price_validate("<price>12.30</price>"), ZXSD_OK);
	CHECK(simple_types_price_validate("<price>123.45</price>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_price_validate("<price>1.234</price>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_hugeBound_validate("<hugeBound>-999999999999999998</hugeBound>"),
	      ZXSD_OK);
	CHECK(simple_types_hugeBound_validate("<hugeBound>-999999999999999999</hugeBound>"),
	      ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_hugeBound_validate("<hugeBound>999999999999999999</hugeBound>"),
	      ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_afterJanuary_validate("<afterJanuary>--02</afterJanuary>"), ZXSD_OK);
	CHECK(simple_types_afterJanuary_validate("<afterJanuary>--01</afterJanuary>"),
	      ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_longerThanMonth_validate("<longerThanMonth>P32D</longerThanMonth>"),
	      ZXSD_OK);
	CHECK(simple_types_longerThanMonth_validate("<longerThanMonth>P27D</longerThanMonth>"),
	      ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_language_validate("<language>fr-FR</language>"), ZXSD_OK);
	CHECK(simple_types_language_validate("<language>fr_XX</language>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_name_validate("<name>a:b</name>"), ZXSD_OK);
	CHECK(simple_types_name_validate("<name>一00</name>"), ZXSD_OK);
	CHECK(simple_types_ncname_validate("<ncname>a:b</ncname>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_nmtoken_validate("<nmtoken>a:b-1</nmtoken>"), ZXSD_OK);
	CHECK(simple_types_nmtokens_validate("<nmtokens>a b:c</nmtokens>"), ZXSD_OK);
	CHECK(simple_types_id_validate("<id>node_1</id>"), ZXSD_OK);
	CHECK(simple_types_idref_validate("<idref>node_1</idref>"), ZXSD_ERR_IDENTITY_CONSTRAINT);
	CHECK(simple_types_idrefs_validate("<idrefs>a b</idrefs>"), ZXSD_ERR_IDENTITY_CONSTRAINT);
	CHECK(simple_types_entity_validate("<entity>logo</entity>"), ZXSD_OK);
	CHECK(simple_types_entities_validate("<entities>logo icon</entities>"), ZXSD_OK);
	CHECK(simple_types_qname_validate("<qname xmlns:p='urn:test'>p:item</qname>"), ZXSD_OK);
	CHECK(simple_types_qname_validate("<qname>p:item</qname>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_uri_validate("<uri>https://example.com/a</uri>"), ZXSD_OK);
	CHECK(simple_types_uri_validate("<uri>A&amp;B</uri>"), ZXSD_OK);
	CHECK(simple_types_fixedText_validate("<fixedText>A&amp;B</fixedText>"), ZXSD_OK);
	CHECK(simple_types_fixedText_validate("<fixedText><![CDATA[A&B]]></fixedText>"), ZXSD_OK);
	CHECK(simple_types_fixedText_validate("<fixedText>A<!-- split -->&amp;B</fixedText>"),
	      ZXSD_OK);
	CHECK(simple_types_fixedText_validate("<fixedText><![CDATA[A&amp;B]]></fixedText>"),
	      ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_duration_validate("<duration>P1Y2M3DT4H5M6.5S</duration>"), ZXSD_OK);
	CHECK(simple_types_date_validate("<date>2024-02-29Z</date>"), ZXSD_OK);
	CHECK(simple_types_date_validate("<date>2023-02-29</date>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_date_validate("<date>2000-02-29</date>"), ZXSD_OK);
	CHECK(simple_types_date_validate("<date>1900-02-29</date>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_date_validate("<date>2024-04-30</date>"), ZXSD_OK);
	CHECK(simple_types_date_validate("<date>2024-04-31</date>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_date_validate("<date>2024-01-00</date>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_dateTime_validate("<dateTime>2024-01-02T03:04:05+02:00</dateTime>"),
	      ZXSD_OK);
	CHECK(simple_types_time_validate("<time>23:59:59Z</time>"), ZXSD_OK);
	CHECK(simple_types_year_validate("<year>2024</year>"), ZXSD_OK);
	CHECK(simple_types_yearMonth_validate("<yearMonth>2024-12</yearMonth>"), ZXSD_OK);
	CHECK(simple_types_month_validate("<month>--12--</month>"), ZXSD_OK);
	CHECK(simple_types_monthDay_validate("<monthDay>--12-31</monthDay>"), ZXSD_OK);
	CHECK(simple_types_day_validate("<day>---31</day>"), ZXSD_OK);
	CHECK(simple_types_hex_validate("<hex>0aFF</hex>"), ZXSD_OK);
	CHECK(simple_types_hex_validate("<hex>0x</hex>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_base64_validate("<base64>YWJjZA==</base64>"), ZXSD_OK);
	CHECK(simple_types_float_validate("<float>-1.25E3</float>"), ZXSD_OK);
	CHECK(simple_types_double_validate("<double>NaN</double>"), ZXSD_OK);
	CHECK(simple_types_long_validate("<long>-9223372036854775808</long>"), ZXSD_OK);
	CHECK(simple_types_long_validate("<long>9223372036854775808</long>"),
	      ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_short_validate("<short>-32768</short>"), ZXSD_OK);
	CHECK(simple_types_byte_validate("<byte>128</byte>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_nonPositive_validate("<nonPositive>0</nonPositive>"), ZXSD_OK);
	CHECK(simple_types_negative_validate("<negative>0</negative>"), ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_nonNegative_validate("<nonNegative>0</nonNegative>"), ZXSD_OK);
	CHECK(simple_types_positive_validate("<positive>1</positive>"), ZXSD_OK);
	CHECK(simple_types_unsignedLong_validate(
		      "<unsignedLong>18446744073709551615</unsignedLong>"),
	      ZXSD_OK);
	CHECK(simple_types_unsignedShort_validate("<unsignedShort>65536</unsignedShort>"),
	      ZXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_unsignedByte_validate("<unsignedByte>255</unsignedByte>"), ZXSD_OK);
	return failures ? 1 : 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_simple_types();
}
#endif
