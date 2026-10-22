/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Runtime tests for recursive named complex types.
 */

#include "recursive.mxsd.h"
#include <stdio.h>
#include <string.h>

#define RECURSIVE_XML_CAPACITY ((MXSD_MAX_DEPTH * 48u) + 64u)

/** Validate one instance and compare its status with the expected result. */
static int expect(mxsd_result_t result, mxsd_error_t expected)
{
	if (result.code != expected) {
		fprintf(stderr, "wanted %d, got %d (%s) at %lu\n", (int)expected, (int)result.code,
			result.message, (unsigned long)result.offset);
	}
	return result.code == expected ? 0 : 1;
}

/** Append a literal while constructing a bounded recursive test document. */
static int append(char **cursor, size_t *remaining, const char *text)
{
	size_t length = strlen(text);

	if (length >= *remaining) {
		return 0;
	}
	memcpy(*cursor, text, length);
	*cursor += length;
	*remaining -= length;
	**cursor = '\0';
	return 1;
}

/** Construct a document containing the requested number of Node instances. */
static int build_nodes(char *xml, size_t capacity, size_t node_count)
{
	char *cursor = xml;
	size_t remaining = capacity;
	size_t i;

	if (node_count == 0u || !append(&cursor, &remaining, "<node><value>x</value>")) {
		return 0;
	}
	for (i = 1u; i < node_count; ++i) {
		if (!append(&cursor, &remaining, "<child><value>x</value>")) {
			return 0;
		}
	}
	for (i = 1u; i < node_count; ++i) {
		if (!append(&cursor, &remaining, "</child>")) {
			return 0;
		}
	}
	return append(&cursor, &remaining, "</node>");
}

/** Stream a recursive document without retaining it. */
static mxsd_result_t stream_nodes(size_t node_count)
{
	mxsd_stream_t stream;
	mxsd_result_t result;
	size_t i;

	result = recursive_node_stream_init(&stream);
	if (result.code != MXSD_OK) {
		return result;
	}
	result = mxsd_start_element(&stream, NULL, "node");
	for (i = 0u; i < node_count && result.code == MXSD_OK; ++i) {
		result = mxsd_start_element(&stream, NULL, "value");
		if (result.code == MXSD_OK) {
			result = mxsd_text(&stream, "x", 1u);
		}
		if (result.code == MXSD_OK) {
			result = mxsd_end_element(&stream, NULL, "value");
		}
		if (i + 1u < node_count && result.code == MXSD_OK) {
			result = mxsd_start_element(&stream, NULL, "child");
		}
	}
	if (result.code != MXSD_OK) {
		return result;
	}
	for (i = 1u; i < node_count && result.code == MXSD_OK; ++i) {
		result = mxsd_end_element(&stream, NULL, "child");
	}
	if (result.code == MXSD_OK) {
		result = mxsd_end_element(&stream, NULL, "node");
	}
	if (result.code == MXSD_OK) {
		result = mxsd_stream_finish(&stream);
	}
	return result;
}

/** Run direct, mutual, bounded-depth, and streaming recursion cases. */
int mxsd_test_recursive(void)
{
	char xml[RECURSIVE_XML_CAPACITY];
	int failures = 0;

	failures +=
		expect(recursive_node_validate("<node><value>root</value><child><value>leaf</value>"
					       "</child></node>"),
		       MXSD_OK);
	failures += expect(recursive_node_validate("<node><value>root</value><child/></node>"),
			   MXSD_ERR_MISSING_ELEMENT);
	failures +=
		expect(recursive_left_validate("<left><right><left><right/></left></right></left>"),
		       MXSD_OK);
	failures += expect(recursive_globalNode_validate(
				   "<globalNode><value>root</value><globalNode><value>leaf</value>"
				   "</globalNode></globalNode>"),
			   MXSD_OK);

	if (!build_nodes(xml, sizeof(xml), MXSD_MAX_DEPTH - 1u)) {
		return 1;
	}
	failures += expect(recursive_node_validate(xml), MXSD_OK);
	if (!build_nodes(xml, sizeof(xml), MXSD_MAX_DEPTH)) {
		return 1;
	}
	failures += expect(recursive_node_validate(xml), MXSD_ERR_EXCESSIVE_NESTING);

	failures += expect(stream_nodes(MXSD_MAX_DEPTH - 1u), MXSD_OK);
	failures += expect(stream_nodes(MXSD_MAX_DEPTH), MXSD_ERR_EXCESSIVE_NESTING);
	return failures ? 1 : 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_recursive();
}
#endif
