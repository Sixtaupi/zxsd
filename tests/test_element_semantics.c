/* Copyright (c) 2026 Sixtaupi
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file @brief Tests for advanced XSD element semantics. */
#include "element_semantics.zxsd.h"
#include <stdio.h>

/** Compare one advanced-element validation result. */
static int expect(zxsd_result_t result, zxsd_error_t wanted)
{
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Exercise a transitive substitution group through the streaming API. */
static zxsd_result_t stream_substitution(void)
{
	static const char ns[] = "urn:zxsd:elements";
	zxsd_stream_t stream;
	zxsd_result_t result = element_semantics_container_stream_init(&stream);

#define STREAM_EVENT(call)                                                                         \
	do {                                                                                       \
		if (result.code == ZXSD_OK) {                                                      \
			result = (call);                                                           \
		}                                                                                  \
	} while (0)
	STREAM_EVENT(zxsd_start_element(&stream, ns, "container"));
	STREAM_EVENT(zxsd_start_element(&stream, ns, "member"));
	STREAM_EVENT(zxsd_start_element(&stream, ns, "name"));
	STREAM_EVENT(zxsd_text(&stream, "x", 1u));
	STREAM_EVENT(zxsd_end_element(&stream, ns, "name"));
	STREAM_EVENT(zxsd_start_element(&stream, ns, "count"));
	STREAM_EVENT(zxsd_text(&stream, "2", 1u));
	STREAM_EVENT(zxsd_end_element(&stream, ns, "count"));
	STREAM_EVENT(zxsd_end_element(&stream, ns, "member"));
	STREAM_EVENT(zxsd_end_element(&stream, ns, "container"));
	STREAM_EVENT(zxsd_stream_finish(&stream));
#undef STREAM_EVENT
	return result;
}

/** Exercise substitution, xsi semantics, defaults, fixed and form. */
int zxsd_test_element_semantics(void)
{
	const char *ns =
		"xmlns:e='urn:zxsd:elements' xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'";
	int failures = 0;
	(void)ns;
	failures += expect(stream_substitution(), ZXSD_OK);
	failures += expect(element_semantics_container_validate(
				   "<e:container "
				   "xmlns:e='urn:zxsd:elements'><e:member><e:name>x</"
				   "e:name><e:count>2</e:count></e:member></e:container>"),
			   ZXSD_OK);
	failures += expect(
		element_semantics_container_validate("<e:container "
						     "xmlns:e='urn:zxsd:elements'><e:head><e:name>"
						     "x</e:name></e:head></e:container>"),
		ZXSD_ERR_INVALID_VALUE);
	failures += expect(
		element_semantics_blockedContainer_validate(
			"<e:blockedContainer "
			"xmlns:e='urn:zxsd:elements'><e:blockedMember><e:name>x</"
			"e:name><e:count>2</e:count></e:blockedMember></e:blockedContainer>"),
		ZXSD_ERR_ELEMENT_ORDER);
	failures += expect(
		element_semantics_dynamic_validate(
			"<e:dynamic xmlns:e='urn:zxsd:elements' "
			"xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			"xsi:type='e:Derived'><e:name>x</e:name><e:count>3</e:count></e:dynamic>"),
		ZXSD_OK);
	failures += expect(
		element_semantics_dynamic_validate(
			"<e:dynamic xmlns:e='urn:zxsd:elements'><e:name>x</e:name></e:dynamic>"),
		ZXSD_ERR_INVALID_VALUE);
	failures += expect(
		element_semantics_blocked_validate(
			"<e:blocked xmlns:e='urn:zxsd:elements' "
			"xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			"xsi:type='e:Derived'><e:name>x</e:name><e:count>3</e:count></e:blocked>"),
		ZXSD_ERR_INVALID_VALUE);
	failures += expect(
		element_semantics_nullable_validate(
			"<e:nullable xmlns:e='urn:zxsd:elements' "
			"xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' xsi:nil='true'/>"),
		ZXSD_OK);
	failures += expect(
		element_semantics_notNullable_validate(
			"<e:notNullable xmlns:e='urn:zxsd:elements' "
			"xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' xsi:nil='true'/>"),
		ZXSD_ERR_INVALID_VALUE);
	failures += expect(element_semantics_nullable_validate(
				   "<e:nullable xmlns:e='urn:zxsd:elements' "
				   "xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
				   "xsi:nil='true'>1</e:nullable>"),
			   ZXSD_ERR_INVALID_VALUE);
	failures += expect(
		element_semantics_defaulted_validate("<e:defaulted xmlns:e='urn:zxsd:elements'/>"),
		ZXSD_OK);
	failures += expect(element_semantics_defaulted_validate(
				   "<e:defaulted xmlns:e='urn:zxsd:elements'></e:defaulted>"),
			   ZXSD_OK);
	failures += expect(element_semantics_fixed_validate(
				   "<e:fixed xmlns:e='urn:zxsd:elements'>7</e:fixed>"),
			   ZXSD_OK);
	failures += expect(element_semantics_fixed_validate(
				   "<e:fixed xmlns:e='urn:zxsd:elements'>8</e:fixed>"),
			   ZXSD_ERR_INVALID_VALUE);
	failures += expect(element_semantics_attributes_validate(
				   "<e:attributes xmlns:e='urn:zxsd:elements' version='2'/>"),
			   ZXSD_OK);
	failures += expect(element_semantics_attributes_validate(
				   "<e:attributes xmlns:e='urn:zxsd:elements' version='3'/>"),
			   ZXSD_ERR_INVALID_VALUE);
	failures +=
		expect(element_semantics_forms_validate(
			       "<e:forms xmlns:e='urn:zxsd:elements'><local>ok</local></e:forms>"),
		       ZXSD_OK);
	return failures ? 1 : 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_element_semantics();
}
#endif
