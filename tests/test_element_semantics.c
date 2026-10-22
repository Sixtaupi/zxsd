/* Copyright (c) 2026 Romain Pelletant
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file @brief Tests for advanced XSD element semantics. */
#include "element_semantics.mxsd.h"
#include <stdio.h>

/** Compare one advanced-element validation result. */
static int expect(mxsd_result_t result, mxsd_error_t wanted)
{
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Exercise a transitive substitution group through the streaming API. */
static mxsd_result_t stream_substitution(void)
{
	static const char ns[] = "urn:mxsd:elements";
	mxsd_stream_t stream;
	mxsd_result_t result = element_semantics_container_stream_init(&stream);

#define STREAM_EVENT(call)                                                                         \
	do {                                                                                       \
		if (result.code == MXSD_OK) {                                                      \
			result = (call);                                                           \
		}                                                                                  \
	} while (0)
	STREAM_EVENT(mxsd_start_element(&stream, ns, "container"));
	STREAM_EVENT(mxsd_start_element(&stream, ns, "member"));
	STREAM_EVENT(mxsd_start_element(&stream, ns, "name"));
	STREAM_EVENT(mxsd_text(&stream, "x", 1u));
	STREAM_EVENT(mxsd_end_element(&stream, ns, "name"));
	STREAM_EVENT(mxsd_start_element(&stream, ns, "count"));
	STREAM_EVENT(mxsd_text(&stream, "2", 1u));
	STREAM_EVENT(mxsd_end_element(&stream, ns, "count"));
	STREAM_EVENT(mxsd_end_element(&stream, ns, "member"));
	STREAM_EVENT(mxsd_end_element(&stream, ns, "container"));
	STREAM_EVENT(mxsd_stream_finish(&stream));
#undef STREAM_EVENT
	return result;
}

/** Exercise substitution, xsi semantics, defaults, fixed and form. */
int mxsd_test_element_semantics(void)
{
	const char *ns =
		"xmlns:e='urn:mxsd:elements' xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'";
	int failures = 0;
	(void)ns;
	failures += expect(stream_substitution(), MXSD_OK);
	failures += expect(element_semantics_container_validate(
				   "<e:container "
				   "xmlns:e='urn:mxsd:elements'><e:member><e:name>x</"
				   "e:name><e:count>2</e:count></e:member></e:container>"),
			   MXSD_OK);
	failures += expect(
		element_semantics_container_validate("<e:container "
						     "xmlns:e='urn:mxsd:elements'><e:head><e:name>"
						     "x</e:name></e:head></e:container>"),
		MXSD_ERR_INVALID_VALUE);
	failures += expect(
		element_semantics_blockedContainer_validate(
			"<e:blockedContainer "
			"xmlns:e='urn:mxsd:elements'><e:blockedMember><e:name>x</"
			"e:name><e:count>2</e:count></e:blockedMember></e:blockedContainer>"),
		MXSD_ERR_ELEMENT_ORDER);
	failures += expect(
		element_semantics_dynamic_validate(
			"<e:dynamic xmlns:e='urn:mxsd:elements' "
			"xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			"xsi:type='e:Derived'><e:name>x</e:name><e:count>3</e:count></e:dynamic>"),
		MXSD_OK);
	failures += expect(
		element_semantics_dynamic_validate(
			"<e:dynamic xmlns:e='urn:mxsd:elements'><e:name>x</e:name></e:dynamic>"),
		MXSD_ERR_INVALID_VALUE);
	failures += expect(
		element_semantics_blocked_validate(
			"<e:blocked xmlns:e='urn:mxsd:elements' "
			"xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
			"xsi:type='e:Derived'><e:name>x</e:name><e:count>3</e:count></e:blocked>"),
		MXSD_ERR_INVALID_VALUE);
	failures += expect(
		element_semantics_nullable_validate(
			"<e:nullable xmlns:e='urn:mxsd:elements' "
			"xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' xsi:nil='true'/>"),
		MXSD_OK);
	failures += expect(
		element_semantics_notNullable_validate(
			"<e:notNullable xmlns:e='urn:mxsd:elements' "
			"xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' xsi:nil='true'/>"),
		MXSD_ERR_INVALID_VALUE);
	failures += expect(element_semantics_nullable_validate(
				   "<e:nullable xmlns:e='urn:mxsd:elements' "
				   "xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
				   "xsi:nil='true'>1</e:nullable>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(
		element_semantics_defaulted_validate("<e:defaulted xmlns:e='urn:mxsd:elements'/>"),
		MXSD_OK);
	failures += expect(element_semantics_defaulted_validate(
				   "<e:defaulted xmlns:e='urn:mxsd:elements'></e:defaulted>"),
			   MXSD_OK);
	failures += expect(element_semantics_fixed_validate(
				   "<e:fixed xmlns:e='urn:mxsd:elements'>7</e:fixed>"),
			   MXSD_OK);
	failures += expect(element_semantics_fixed_validate(
				   "<e:fixed xmlns:e='urn:mxsd:elements'>8</e:fixed>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(element_semantics_attributes_validate(
				   "<e:attributes xmlns:e='urn:mxsd:elements' version='2'/>"),
			   MXSD_OK);
	failures += expect(element_semantics_attributes_validate(
				   "<e:attributes xmlns:e='urn:mxsd:elements' version='3'/>"),
			   MXSD_ERR_INVALID_VALUE);
	failures +=
		expect(element_semantics_forms_validate(
			       "<e:forms xmlns:e='urn:mxsd:elements'><local>ok</local></e:forms>"),
		       MXSD_OK);
	return failures ? 1 : 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_element_semantics();
}
#endif
