/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Incremental content models, occurrence boundaries, and fallback limits. */

#include "stream_models.mxsd.h"

#include <stdio.h>
#include <string.h>

/** Check one result and include the failing root or case in diagnostics. */
static int expect(mxsd_result_t result, mxsd_error_t wanted, const char *context)
{
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "%s: wanted %d, got %d (%s) at %lu\n", context, (int)wanted,
		(int)result.code, result.message, (unsigned long)result.offset);
	return 1;
}

/** Feed small fragments so cursor transitions also cross raw-input boundaries. */
static mxsd_result_t read_xml(const mxsd_schema_t *schema, const char *xml)
{
	static mxsd_xml_reader_t reader;
	size_t offset = 0u;
	size_t length = strlen(xml);
	mxsd_result_t result = mxsd_xml_reader_init(&reader, schema);

	while (result.code == MXSD_OK && offset < length) {
		size_t count = length - offset;

		if (count > 7u) {
			count = 7u;
		}
		result = mxsd_xml_reader_feed(&reader, xml + offset, count);
		offset += count;
	}
	if (result.code == MXSD_OK) {
		result = mxsd_xml_reader_finish(&reader);
	}
	mxsd_xml_reader_dispose(&reader);
	return result;
}

/** Compare all words of up to six children with the independent document validator. */
static int compare_words(const mxsd_schema_t *schema)
{
	char xml[256];
	size_t length;
	size_t words = 1u;

	for (length = 0u; length <= 6u; ++length, words *= 3u) {
		size_t word;

		for (word = 0u; word < words; ++word) {
			size_t value = word;
			size_t offset =
				(size_t)snprintf(xml, sizeof(xml), "<%s>", schema->root_name);
			size_t index;
			mxsd_result_t document;
			mxsd_result_t stream;

			for (index = 0u; index < length; ++index) {
				xml[offset++] = '<';
				xml[offset++] = (char)('a' + value % 3u);
				xml[offset++] = '/';
				xml[offset++] = '>';
				value /= 3u;
			}
			(void)snprintf(xml + offset, sizeof(xml) - offset, "</%s>",
				       schema->root_name);
			document = mxsd_validate(schema, xml);
			stream = read_xml(schema, xml);
			if ((document.code == MXSD_OK) != (stream.code == MXSD_OK)) {
				fprintf(stderr,
					"content-model disagreement: %s (DOM %d, stream %d)\n", xml,
					(int)document.code, (int)stream.code);
				return 1;
			}
		}
	}
	return 0;
}

/** Emit an empty string element, optionally in a foreign namespace. */
static mxsd_result_t child(mxsd_stream_t *stream, const char *ns, const char *name)
{
	mxsd_result_t result = mxsd_start_element(stream, ns, name);

	return result.code == MXSD_OK ? mxsd_end_element(stream, ns, name) : result;
}

/** Exercise repetitions larger than history capacity, including substitution and skip. */
static int long_stream(const mxsd_schema_t *schema, const char *ns, const char *name)
{
	static mxsd_stream_t stream;
	mxsd_result_t result = mxsd_stream_init(&stream, schema);
	size_t index;

	if (result.code == MXSD_OK) {
		result = mxsd_start_element(&stream, NULL, schema->root_name);
	}
	for (index = 0u; index < 2048u && result.code == MXSD_OK; ++index) {
		result = child(&stream, ns, name != NULL ? name : (index % 2u ? "b" : "a"));
	}
	if (result.code == MXSD_OK) {
		result = mxsd_end_element(&stream, NULL, schema->root_name);
	}
	if (result.code == MXSD_OK) {
		result = mxsd_stream_finish(&stream);
	}
	mxsd_stream_dispose(&stream);
	return expect(result, MXSD_OK, schema->root_name);
}

/** Verify rejection on the first forbidden event and sticky code, path, and offset. */
static int sticky_order(void)
{
	static mxsd_stream_t stream;
	mxsd_result_t result = stream_models_repeated_stream_init(&stream);
	mxsd_result_t first;
	mxsd_result_t later;

	if (result.code == MXSD_OK) {
		result = mxsd_start_element(&stream, NULL, "repeated");
	}
	if (result.code == MXSD_OK) {
		result = child(&stream, NULL, "a");
	}
	if (result.code == MXSD_OK) {
		result = child(&stream, NULL, "b");
	}
	if (expect(result, MXSD_OK, "valid prefix")) {
		return 1;
	}
	first = mxsd_start_element(&stream, NULL, "a");
	later = mxsd_end_element(&stream, NULL, "repeated");
	if (expect(first, MXSD_ERR_ELEMENT_ORDER, "backwards sequence") ||
	    later.code != first.code || later.path != first.path || later.offset != first.offset) {
		return 1;
	}
	later = mxsd_stream_finish(&stream);
	mxsd_stream_dispose(&stream);
	return later.code != first.code || later.path != first.path || later.offset != first.offset;
}

/** Check the history fallback at its exact capacity and its first rejected token. */
static int history_boundary(void)
{
#if MXSD_STREAM_MODEL_CAPACITY == 0
	static mxsd_stream_t stream;
	mxsd_result_t result;
	size_t index;
	int overflow;

	for (overflow = 0; overflow <= 1; ++overflow) {
		result = stream_models_nested_stream_init(&stream);
		if (result.code == MXSD_OK) {
			result = mxsd_start_element(&stream, NULL, "nested");
		}
		for (index = 0u; index < MXSD_STREAM_CHILD_CAPACITY && result.code == MXSD_OK;
		     ++index) {
			result = child(&stream, NULL, "a");
		}
		if (expect(result, MXSD_OK, "history exact capacity")) {
			return 1;
		}
		if (overflow) {
			result = mxsd_start_element(&stream, NULL, "a");
			if (expect(result, MXSD_ERR_STREAM_LIMIT, "history first rejected child")) {
				return 1;
			}
		} else {
			result = mxsd_end_element(&stream, NULL, "nested");
			if (expect(result, MXSD_OK, "history close at capacity")) {
				return 1;
			}
		}
		result = mxsd_stream_finish(&stream);
		mxsd_stream_dispose(&stream);
		if (expect(result, overflow ? MXSD_ERR_STREAM_LIMIT : MXSD_OK, "history finish")) {
			return 1;
		}
	}
#endif
	return 0;
}

#if MXSD_STREAM_MODEL_CAPACITY > 0
/** Check independently specified occurrence boundaries, not greedy DOM partitioning. */
static int nested_cases(void)
{
	const struct {
		const mxsd_schema_t *schema;
		const char *xml;
		mxsd_error_t result;
	} cases[] = {
		{stream_models_partitioned_schema(), "<partitioned><a/></partitioned>",
		 MXSD_ERR_ELEMENT_ORDER},
		{stream_models_partitioned_schema(), "<partitioned><a/><a/></partitioned>",
		 MXSD_OK},
		{stream_models_partitioned_schema(), "<partitioned><a/><a/><a/></partitioned>",
		 MXSD_OK},
		{stream_models_partitioned_schema(), "<partitioned><a/><a/><a/><a/></partitioned>",
		 MXSD_OK},
		{stream_models_partitioned_schema(),
		 "<partitioned><a/><a/><a/><a/><a/></partitioned>", MXSD_OK},
		{stream_models_partitioned_schema(),
		 "<partitioned><a/><a/><a/><a/><a/><a/></partitioned>", MXSD_OK},
		{stream_models_partitioned_schema(),
		 "<partitioned><a/><a/><a/><a/><a/><a/><a/></partitioned>", MXSD_ERR_ELEMENT_ORDER},
		{stream_models_nested_schema(), "<nested><b/><a/></nested>",
		 MXSD_ERR_ELEMENT_ORDER},
		{stream_models_nested_schema(), "<nested><b/></nested>", MXSD_ERR_ELEMENT_ORDER},
		{stream_models_nested_schema(), "<nested><b/><c/><b/><c/><a/></nested>", MXSD_OK},
		{stream_models_complexRepeated_schema(), "<complexRepeated><a/></complexRepeated>",
		 MXSD_ERR_ELEMENT_ORDER},
		{stream_models_complexRepeated_schema(),
		 "<complexRepeated><a/><a/></complexRepeated>", MXSD_OK},
		{stream_models_complexRepeated_schema(),
		 "<complexRepeated><a/><a/><b/><c/></complexRepeated>", MXSD_OK},
		{stream_models_complexRepeated_schema(), "<complexRepeated><c/></complexRepeated>",
		 MXSD_ERR_ELEMENT_ORDER},
		{stream_models_nullableHuge_schema(), "<nullableHuge/>", MXSD_OK},
		{stream_models_nullableHuge_schema(), "<nullableHuge><a/><b/></nullableHuge>",
		 MXSD_OK},
		{stream_models_nestedTree_schema(),
		 "<nestedTree><b/><node><b/><node><a/></node><c/></node><c/></nestedTree>",
		 MXSD_OK},
		{stream_models_nestedTree_schema(),
		 "<nestedTree><b/><node><a/></node></nestedTree>", MXSD_ERR_ELEMENT_ORDER},
		{stream_models_nestedDynamic_schema(),
		 "<nestedDynamic xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
		 "xsi:type='NestedDerived'><b/><c/><a/></nestedDynamic>",
		 MXSD_OK},
		{stream_models_nestedDynamic_schema(),
		 "<nestedDynamic xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
		 "xsi:type='NestedDerived'><a/><a/><a/></nestedDynamic>",
		 MXSD_ERR_ELEMENT_ORDER},
		{stream_models_nestedDynamic_schema(),
		 "<nestedDynamic xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
		 "xsi:nil='true'/>",
		 MXSD_OK},
	};
	size_t index;
	int failures = 0;

	for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
		failures += expect(read_xml(cases[index].schema, cases[index].xml),
				   cases[index].result, cases[index].xml);
	}
	return failures;
}

/** Repeated groups use a constant pool, release every frame, and never use history. */
static int nested_long_stream(void)
{
	static mxsd_stream_t stream;
	mxsd_result_t result = stream_models_nested_stream_init(&stream);
	size_t index;

	if (result.code == MXSD_OK) {
		result = mxsd_start_element(&stream, NULL, "nested");
	}
	for (index = 0u; index < 2048u && result.code == MXSD_OK; ++index) {
		result = child(&stream, NULL, "b");
		if (result.code == MXSD_OK) {
			result = child(&stream, NULL, "c");
		}
	}
	if (result.code == MXSD_OK) {
		result = mxsd_end_element(&stream, NULL, "nested");
	}
	if (result.code == MXSD_OK) {
		result = mxsd_stream_finish(&stream);
	}
	if (expect(result, MXSD_OK, "long nested repetition")) {
		mxsd_stream_dispose(&stream);
		return 1;
	}
#if MXSD_STREAM_MODEL_CAPACITY > 0
	for (index = 0u; index < MXSD_STREAM_MODEL_CAPACITY; ++index) {
		if (stream.model_nodes[index].references != 0u) {
			fprintf(stderr, "nested model pool retained a closed frame\n");
			return 1;
		}
	}
	if (stream.frames[0].child_particle_table_count != 0u) {
		return 1;
	}
#endif
	mxsd_stream_dispose(&stream);
	return 0;
}

/** Enumerate a small regular language independently of either runtime matcher. */
static int compare_language(const mxsd_schema_t *schema, const char *const *terms,
			    size_t term_count, size_t minimum, size_t maximum)
{
	size_t length;
	size_t words = 1u;

	for (length = 0u; length <= 7u; ++length, words *= 3u) {
		size_t word;

		for (word = 0u; word < words; ++word) {
			unsigned char reachable[7][8] = {{0}};
			char text[8];
			char xml[256];
			size_t index, count, position;
			size_t value = word;
			size_t offset =
				(size_t)snprintf(xml, sizeof(xml), "<%s>", schema->root_name);
			int accepted = 0;
			mxsd_result_t result;

			for (index = 0u; index < length; ++index) {
				text[index] = (char)('a' + value % 3u);
				value /= 3u;
				offset += (size_t)snprintf(xml + offset, sizeof(xml) - offset,
							   "<%c/>", text[index]);
			}
			text[length] = '\0';
			(void)snprintf(xml + offset, sizeof(xml) - offset, "</%s>",
				       schema->root_name);
			reachable[0][0] = 1u;
			for (count = 0u; count <= maximum; ++count) {
				if (count >= minimum && reachable[count][length]) {
					accepted = 1;
				}
				if (count == maximum) {
					break;
				}
				for (position = 0u; position <= length; ++position) {
					for (index = 0u; index < term_count; ++index) {
						size_t size = strlen(terms[index]);

						if (reachable[count][position] &&
						    size <= length - position &&
						    memcmp(text + position, terms[index], size) ==
							    0) {
							reachable[count + 1u][position + size] = 1u;
						}
					}
				}
			}
			result = read_xml(schema, xml);
			if ((result.code == MXSD_OK) != accepted) {
				fprintf(stderr,
					"nested language disagreement: %s (wanted %d, got %d)\n",
					xml, accepted, (int)result.code);
				return 1;
			}
		}
	}
	return 0;
}
#endif

/** Check the direct-leaf descriptor form without a surrounding sequence group. */
static int direct_leaf(void)
{
	mxsd_schema_t schema = *stream_models_repeated_schema();
	mxsd_complex_t complex = *schema.root_complex;
	mxsd_particle_t particle = complex.particle->children[0];
	int failures = 0;

	particle.min_occurs = 1u;
	particle.max_occurs = 2u;
	complex.particle = &particle;
	schema.root_complex = &complex;
	failures += expect(read_xml(&schema, "<repeated><a/><a/></repeated>"), MXSD_OK,
			   "direct leaf exact max");
	failures += expect(read_xml(&schema, "<repeated><a/><a/><a/></repeated>"),
			   MXSD_ERR_ELEMENT_ORDER, "direct leaf first rejected occurrence");
	failures += expect(read_xml(&schema, "<repeated/>"), MXSD_ERR_MISSING_ELEMENT,
			   "direct leaf minimum");
	return failures;
}

/** Count accepted starts to verify an early cursor failure does not notify its observer. */
static void count_start(void *user_data, mxsd_xml_event_t event, const char *namespace_uri,
			const char *local_name, const char *value, size_t value_length)
{
	size_t *starts = user_data;

	(void)namespace_uri;
	(void)local_name;
	(void)value;
	(void)value_length;
	if (event == MXSD_XML_EVENT_START_ELEMENT) {
		(*starts)++;
	}
}

/** Verify rejection precedes notification and retains the offending raw byte offset. */
static int observer_order(void)
{
	static const char xml[] = "<repeated><a/><b/><a/></repeated>";
	static mxsd_xml_reader_t reader;
	size_t starts = 0u;
	mxsd_result_t result = mxsd_xml_reader_init(&reader, stream_models_repeated_schema());

	if (result.code != MXSD_OK) {
		return 1;
	}
	mxsd_xml_reader_set_event_callback(&reader, count_start, &starts);
	result = mxsd_xml_reader_feed(&reader, xml, sizeof(xml) - 1u);
	mxsd_xml_reader_dispose(&reader);
	if (expect(result, MXSD_ERR_ELEMENT_ORDER, "observer order") || starts != 3u ||
	    result.offset != strlen("<repeated><a/><b/>")) {
		return 1;
	}
#if MXSD_STREAM_MODEL_CAPACITY > 0
	{
		static const char nested[] = "<nested><b/><a/></nested>";
		mxsd_result_t later;

		starts = 0u;
		result = mxsd_xml_reader_init(&reader, stream_models_nested_schema());
		if (result.code != MXSD_OK) {
			return 1;
		}
		mxsd_xml_reader_set_event_callback(&reader, count_start, &starts);
		result = mxsd_xml_reader_feed(&reader, nested, sizeof(nested) - 1u);
		later = mxsd_xml_reader_finish(&reader);
		mxsd_xml_reader_dispose(&reader);
		if (expect(result, MXSD_ERR_ELEMENT_ORDER, "nested observer order") ||
		    starts != 2u || result.offset != strlen("<nested><b/>") ||
		    later.code != result.code || later.path != result.path ||
		    later.offset != result.offset) {
			return 1;
		}
	}
#endif
	return 0;
}

/** Run cursor, fragment, repetition, dynamic-type, and bounded-history regressions. */
int mxsd_test_stream_models(void)
{
	const mxsd_schema_t *schemas[] = {
		stream_models_sequence_schema(),
		stream_models_optionalSequence_schema(),
		stream_models_repeatingSequence_schema(),
		stream_models_optionalRepeatingSequence_schema(),
		stream_models_all_schema(),
		stream_models_optionalAll_schema(),
		stream_models_choice_schema(),
		stream_models_repeatedChoice_schema(),
		stream_models_unboundedChoice_schema(),
		stream_models_repeated_schema(),
		stream_models_unboundedSequence_schema(),
		stream_models_nested_schema(),
		stream_models_nullableRepeated_schema(),
		stream_models_nestedDynamic_schema(),
	};
	size_t index;
	int failures = 0;

	for (index = 0u; index < sizeof(schemas) / sizeof(schemas[0]); ++index) {
		failures += compare_words(schemas[index]);
	}
	failures += long_stream(stream_models_repeated_schema(), NULL, "a");
	failures += long_stream(stream_models_unboundedChoice_schema(), NULL, NULL);
	failures += long_stream(stream_models_unboundedSequence_schema(), NULL, NULL);
	failures += long_stream(stream_models_substitutions_schema(), NULL, "member");
	failures += long_stream(stream_models_wildcard_schema(), "urn:foreign", "item");
	failures += sticky_order();
	failures += history_boundary();
	failures += direct_leaf();
	failures += observer_order();
#if MXSD_STREAM_MODEL_CAPACITY > 0
	static const char *const complex_terms[] = {"a", "aa", "b", "bc"};
	static const char *const nullable_terms[] = {"a", "b", "c", "ac", "bc"};

	failures += nested_cases();
	failures += nested_long_stream();
	failures += long_stream(stream_models_nestedUnbounded_schema(), NULL, "a");
	failures += long_stream(stream_models_nullableHuge_schema(), NULL, NULL);
	failures += long_stream(stream_models_nestedSubstitutions_schema(), NULL, "member");
	failures += long_stream(stream_models_nestedWildcard_schema(), "urn:foreign", "item");
	failures += compare_language(stream_models_complexRepeated_schema(), complex_terms,
				     sizeof(complex_terms) / sizeof(complex_terms[0]), 2u, 6u);
	failures += compare_language(stream_models_nullableRepeated_schema(), nullable_terms,
				     sizeof(nullable_terms) / sizeof(nullable_terms[0]), 0u, 4u);
#endif
	failures += expect(read_xml(stream_models_wildcard_schema(),
				    "<wildcard xmlns:p='urn:foreign' xmlns:q='urn:foreign'>"
				    "<p:item/><q:item><q:nested/></q:item></wildcard>"),
			   MXSD_OK, "wildcard namespace aliases and nested skip");
	failures += expect(read_xml(stream_models_strictWildcard_schema(),
				    "<strictWildcard><number>7</number></strictWildcard>"),
			   MXSD_OK, "strict wildcard known declaration");
	failures += expect(read_xml(stream_models_strictWildcard_schema(),
				    "<strictWildcard><unknown/></strictWildcard>"),
			   MXSD_ERR_UNEXPECTED_ELEMENT, "strict wildcard unknown declaration");
	failures += expect(read_xml(stream_models_laxWildcard_schema(),
				    "<laxWildcard><unknown><nested/></unknown></laxWildcard>"),
			   MXSD_OK, "lax wildcard unknown declaration");
	failures += expect(read_xml(stream_models_laxWildcard_schema(),
				    "<laxWildcard><number>bad</number></laxWildcard>"),
			   MXSD_ERR_INVALID_VALUE, "lax wildcard invalid known declaration");
	failures +=
		expect(read_xml(stream_models_repeated_schema(),
				"<repeated xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'"
				" xsi:type='Derived'><a/><a/><b/></repeated>"),
		       MXSD_OK, "dynamic type valid");
	failures +=
		expect(read_xml(stream_models_repeated_schema(),
				"<repeated xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'"
				" xsi:type='Derived'><a/><a/><a/></repeated>"),
		       MXSD_ERR_ELEMENT_ORDER, "dynamic type maxOccurs");
	failures +=
		expect(read_xml(stream_models_repeated_schema(),
				"<repeated xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'"
				" xsi:nil='true'/>"),
		       MXSD_OK, "nilled required model");
	return failures ? 1 : 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_stream_models();
}
#endif
