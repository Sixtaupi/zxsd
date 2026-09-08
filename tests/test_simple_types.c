/*
 * Copyright (c) 2026 Romain Pelletant
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Tests for XSD 1.0 list, union and built-in simple types. */
#include "simple_types.mxsd.h"
#include <stdio.h>

/** Assert that a generated validator accepts or rejects one document. */
#define CHECK(call, wanted)                                                                        \
	do {                                                                                       \
		mxsd_result_t r_ = (call);                                                         \
		if (r_.code != (wanted)) {                                                         \
			fprintf(stderr, "%s:%d wanted %d got %d\n", __FILE__, __LINE__,            \
				(int)(wanted), (int)r_.code);                                      \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

/** Run the complete-simple-type conformance cases. */
int main(void)
{
	int failures = 0;
	CHECK(simple_types_list_validate("<list> 1\t -2 </list>"), MXSD_OK);
	CHECK(simple_types_list_validate("<list>1 2 3</list>"), MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_union_validate("<union>-4</union>"), MXSD_OK);
	CHECK(simple_types_union_validate("<union>a:b</union>"), MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_inlineUnion_validate("<inlineUnion>yes</inlineUnion>"), MXSD_OK);
	CHECK(simple_types_inlineUnion_validate("<inlineUnion>false</inlineUnion>"), MXSD_OK);
	CHECK(simple_types_inlineUnion_validate("<inlineUnion>no</inlineUnion>"),
	      MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_collapsed_validate("<collapsed> a\t b </collapsed>"), MXSD_OK);
	CHECK(simple_types_language_validate("<language>fr-FR</language>"), MXSD_OK);
	CHECK(simple_types_language_validate("<language>fr_XX</language>"), MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_name_validate("<name>a:b</name>"), MXSD_OK);
	CHECK(simple_types_ncname_validate("<ncname>a:b</ncname>"), MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_nmtoken_validate("<nmtoken>a:b-1</nmtoken>"), MXSD_OK);
	CHECK(simple_types_nmtokens_validate("<nmtokens>a b:c</nmtokens>"), MXSD_OK);
	CHECK(simple_types_id_validate("<id>node_1</id>"), MXSD_OK);
	CHECK(simple_types_idref_validate("<idref>node_1</idref>"), MXSD_OK);
	CHECK(simple_types_idrefs_validate("<idrefs>a b</idrefs>"), MXSD_OK);
	CHECK(simple_types_entity_validate("<entity>logo</entity>"), MXSD_OK);
	CHECK(simple_types_entities_validate("<entities>logo icon</entities>"), MXSD_OK);
	CHECK(simple_types_qname_validate("<qname xmlns:p='urn:test'>p:item</qname>"), MXSD_OK);
	CHECK(simple_types_qname_validate("<qname>p:item</qname>"), MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_uri_validate("<uri>https://example.com/a</uri>"), MXSD_OK);
	CHECK(simple_types_duration_validate("<duration>P1Y2M3DT4H5M6.5S</duration>"), MXSD_OK);
	CHECK(simple_types_date_validate("<date>2024-02-29Z</date>"), MXSD_OK);
	CHECK(simple_types_date_validate("<date>2023-02-29</date>"), MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_dateTime_validate("<dateTime>2024-01-02T03:04:05+02:00</dateTime>"),
	      MXSD_OK);
	CHECK(simple_types_time_validate("<time>23:59:59Z</time>"), MXSD_OK);
	CHECK(simple_types_year_validate("<year>2024</year>"), MXSD_OK);
	CHECK(simple_types_yearMonth_validate("<yearMonth>2024-12</yearMonth>"), MXSD_OK);
	CHECK(simple_types_month_validate("<month>--12--</month>"), MXSD_OK);
	CHECK(simple_types_monthDay_validate("<monthDay>--12-31</monthDay>"), MXSD_OK);
	CHECK(simple_types_day_validate("<day>---31</day>"), MXSD_OK);
	CHECK(simple_types_hex_validate("<hex>0aFF</hex>"), MXSD_OK);
	CHECK(simple_types_hex_validate("<hex>0x</hex>"), MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_base64_validate("<base64>YWJjZA==</base64>"), MXSD_OK);
	CHECK(simple_types_float_validate("<float>-1.25E3</float>"), MXSD_OK);
	CHECK(simple_types_double_validate("<double>NaN</double>"), MXSD_OK);
	CHECK(simple_types_long_validate("<long>-9223372036854775808</long>"), MXSD_OK);
	CHECK(simple_types_long_validate("<long>9223372036854775808</long>"),
	      MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_short_validate("<short>-32768</short>"), MXSD_OK);
	CHECK(simple_types_byte_validate("<byte>128</byte>"), MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_nonPositive_validate("<nonPositive>0</nonPositive>"), MXSD_OK);
	CHECK(simple_types_negative_validate("<negative>0</negative>"), MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_nonNegative_validate("<nonNegative>0</nonNegative>"), MXSD_OK);
	CHECK(simple_types_positive_validate("<positive>1</positive>"), MXSD_OK);
	CHECK(simple_types_unsignedLong_validate(
		      "<unsignedLong>18446744073709551615</unsignedLong>"),
	      MXSD_OK);
	CHECK(simple_types_unsignedShort_validate("<unsignedShort>65536</unsignedShort>"),
	      MXSD_ERR_INVALID_VALUE);
	CHECK(simple_types_unsignedByte_validate("<unsignedByte>255</unsignedByte>"), MXSD_OK);
	CHECK(simple_types_notation_validate("<notation xmlns:p='urn:test'>p:item</notation>"),
	      MXSD_OK);
	return failures ? 1 : 0;
}
