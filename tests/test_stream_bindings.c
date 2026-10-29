/*
 * Copyright (c) 2026 zxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file @brief Fragmented typed subtree delivery and complete-document validation. */

#include "stream_bindings.bind.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(value)                                                                               \
	do {                                                                                       \
		if (!(value)) {                                                                    \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value);                \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

/** Callback state owns only copied values, never transient arena pointers. */
struct received {
	size_t count;
	size_t peak;
	zxsd_bind_arena_t *arena;
	zxsd_bind_action_t action;
	char label[64];
};

/** Validate every C representation as soon as one complete row is delivered. */
static zxsd_bind_action_t row_received(void *context, const stream_bindings_Row_t *row)
{
	struct received *received = context;

	received->count++;
	if (received->arena->used > received->peak) {
		received->peak = received->arena->used;
	}
	CHECK(row->id == 7u);
	CHECK(row->has_enabled && row->enabled);
	CHECK(row->has_kind && row->kind == STREAM_BINDINGS_KIND_LIVE);
	CHECK(row->label != NULL);
	if (row->origin != NULL) {
		CHECK(strcmp(row->origin, "north") == 0);
	}
	if (row->label != NULL) {
		(void)snprintf(received->label, sizeof(received->label), "%s", row->label);
	}
	if (row->number_count != 0u) {
		size_t index;

		CHECK(row->number_count == 5u);
		for (index = 0u; index < row->number_count; ++index) {
			CHECK(row->number[index] == (int32_t)index - 2);
		}
		CHECK(row->measure != NULL && row->measure->value == INT64_MIN);
		CHECK(row->measure != NULL && strcmp(row->measure->unit, "m") == 0);
		CHECK(row->child_count == 3u);
		CHECK(row->child_count == 3u && strcmp(row->child[0].label, "one") == 0);
		CHECK(row->child_count == 3u && strcmp(row->child[1].label, "two") == 0);
		CHECK(row->child_count == 3u && strcmp(row->child[2].label, "three") == 0);
	}
	return received->action;
}

/** A split may occur in any UTF-8 scalar, XML entity, tag, attribute, or number. */
static const char document[] =
	"<s:doc xmlns:s='urn:stream'><s:meta valid='yes'>ignored<x xmlns='other'/></s:meta>"
	"<s:row id='7' rank='-128' s:origin=' north '><s:label>  été &amp; &#x1F321; <![CDATA[ & "
	"sun ]]> </s:label>"
	"<s:number>-2</s:number><s:number>-1</s:number><s:number>0</s:number>"
	"<s:number>1</s:number><s:number>2</s:number>"
	"<s:measure>-9223372036854775808</s:measure>"
	"<s:child id='1'><s:label>one</s:label></s:child>"
	"<s:child id='2'><s:label>two</s:label></s:child>"
	"<s:child id='3'><s:label>three</s:label></s:child>"
	"</s:row><s:check>255</s:check></s:doc>";

/** Feed the same document at every possible two-fragment boundary. */
static void arbitrary_splits(void)
{
	static stream_bindings_doc_row_reader_t reader;
	static unsigned char storage[4096];
	stream_bindings_Row_t row;
	size_t split;

	for (split = 0u; split <= sizeof(document) - 1u; ++split) {
		zxsd_bind_arena_t arena;
		struct received received = {0};
		zxsd_result_t result;

		zxsd_bind_arena_init(&arena, storage, sizeof(storage));
		arena.used = 3u;
		received.arena = &arena;
		result = stream_bindings_doc_row_reader_init(&reader, &row, &arena, row_received,
							     &received, NULL);
		if (result.code == ZXSD_OK) {
			result = stream_bindings_doc_row_reader_feed(&reader, document, split);
		}
		if (result.code == ZXSD_OK) {
			result = stream_bindings_doc_row_reader_feed(&reader, document + split,
								     sizeof(document) - 1u - split);
		}
		if (result.code == ZXSD_OK) {
			result = stream_bindings_doc_row_reader_finish(&reader);
		}
		CHECK(result.code == ZXSD_OK);
		CHECK(received.count == 1u);
		CHECK(strcmp(received.label, "été & 🌡 & sun") == 0);
		CHECK(arena.used == 3u && row.label == NULL);
		stream_bindings_doc_row_reader_dispose(&reader);
	}
}

/** Return a one-byte-at-a-time result, checking sticky errors and rollback. */
static zxsd_result_t read_document(const char *xml, size_t capacity, struct received *received)
{
	static stream_bindings_doc_row_reader_t reader;
	static unsigned char storage[4096];
	stream_bindings_Row_t row;
	zxsd_bind_arena_t arena;
	zxsd_result_t result;
	size_t index;

	zxsd_bind_arena_init(&arena, storage, capacity);
	received->arena = &arena;
	result = stream_bindings_doc_row_reader_init(&reader, &row, &arena, row_received, received,
						     NULL);
	for (index = 0u; xml[index] != '\0' && result.code == ZXSD_OK; ++index) {
		result = stream_bindings_doc_row_reader_feed(&reader, xml + index, 1u);
	}
	if (result.code == ZXSD_OK) {
		result = stream_bindings_doc_row_reader_finish(&reader);
	}
	if (result.code != ZXSD_OK) {
		zxsd_result_t later = stream_bindings_doc_row_reader_feed(&reader, "<", 1u);

		CHECK(later.code == result.code && later.offset == result.offset &&
		      later.path == result.path);
		later = stream_bindings_doc_row_reader_finish(&reader);
		CHECK(later.code == result.code && later.offset == result.offset &&
		      later.path == result.path);
	}
	CHECK(arena.used == 0u && row.label == NULL);
	stream_bindings_doc_row_reader_dispose(&reader);
	received->arena = NULL;
	return result;
}

/** Verify local subtree success never bypasses later full-document validation. */
static void validation_and_limits(void)
{
	const char *valid =
		"<doc xmlns='urn:stream'><row id='7'><label/></row><check>0</check></doc>";
	const char *invalid =
		"<doc xmlns='urn:stream'><row id='7'><label/></row><check>256</check></doc>";
	struct received received = {0};
	zxsd_result_t result;
	size_t capacity;

	result = read_document(invalid, 256u, &received);
	CHECK(result.code != ZXSD_OK && received.count == 1u);
	CHECK(strcmp(received.label, "unknown") == 0);
	received = (struct received){.action = ZXSD_BIND_VALIDATE_ONLY};
	result = read_document(invalid, 256u, &received);
	CHECK(result.code != ZXSD_OK && received.count == 1u);
	received = (struct received){.action = ZXSD_BIND_CALLBACK_ERROR};
	result = read_document(valid, 256u, &received);
	CHECK(result.code == ZXSD_ERR_OUTPUT && received.count == 1u);
	for (capacity = 0u; capacity < 256u; ++capacity) {
		received = (struct received){0};
		result = read_document(valid, capacity, &received);
		if (result.code == ZXSD_OK) {
			CHECK(received.count == 1u && capacity > 0u);
			break;
		}
		CHECK(result.code == ZXSD_ERR_RESOURCE_LIMIT && received.count == 0u);
	}
	CHECK(capacity < 256u);
	received = (struct received){0};
	CHECK(read_document("<doc xmlns='urn:stream'><check>0</check></doc>", 0u, &received).code ==
	      ZXSD_OK);
	CHECK(received.count == 0u);
}

/** A scalar selection is delivered as a generated structure with a value member. */
static zxsd_bind_action_t label_received(void *context,
					 const stream_bindings_doc_row_label_t *label)
{
	size_t *count = context;

	CHECK(strcmp(label->value, "a & b") == 0);
	(*count)++;
	return ZXSD_BIND_CONTINUE;
}

/** Skip unsupported ancestor layouts, but reject a derived selected complex type. */
static void scalar_and_dynamic(void)
{
	static stream_bindings_doc_row_label_reader_t scalar;
	static unsigned char storage[256];
	stream_bindings_doc_row_label_t label;
	zxsd_bind_arena_t arena;
	size_t count = 0u;
	const char *xml =
		"<doc xmlns='urn:stream' xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
		"xmlns:s='urn:stream'><row id='7' xsi:type='s:Derived'>"
		"<label> a &amp; b </label><extra>ignored</extra></row><check>0</check></doc>";
	zxsd_result_t result;
	struct received received = {0};

	zxsd_bind_arena_init(&arena, storage, sizeof(storage));
	result = stream_bindings_doc_row_label_reader_init(&scalar, &label, &arena, label_received,
							   &count, NULL);
	CHECK(result.code == ZXSD_OK);
	result = stream_bindings_doc_row_label_reader_feed(&scalar, xml, strlen(xml));
	CHECK(result.code == ZXSD_OK);
	CHECK(stream_bindings_doc_row_label_reader_finish(&scalar).code == ZXSD_OK);
	CHECK(count == 1u && arena.used == 0u);
	stream_bindings_doc_row_label_reader_dispose(&scalar);
	CHECK(read_document(xml, 256u, &received).code == ZXSD_ERR_UNSUPPORTED);
	CHECK(received.count == 0u);
}

/** Explicit application-owned identity scratch, including zero-heap runtime builds. */
struct scratch_usage {
	size_t allocations;
	size_t releases;
};

/** Allocate only through the explicitly supplied test harness allocator. */
static void *scratch_allocate(void *context, size_t size)
{
	struct scratch_usage *usage = context;

	usage->allocations++;
	return malloc(size);
}

/** Verify identity scratch is released exactly once after a failed final check. */
static void scratch_release(void *context, void *pointer)
{
	struct scratch_usage *usage = context;

	usage->releases++;
	free(pointer);
}

/** Even validation-only mode must enforce document-wide keys after provisional delivery. */
static void identities(void)
{
	static stream_bindings_uniqueDoc_row_reader_t reader;
	static unsigned char storage[256];
	stream_bindings_Row_t row;
	zxsd_bind_arena_t arena;
	struct scratch_usage usage = {0};
	zxsd_allocator_t scratch = {scratch_allocate, scratch_release, &usage};
	struct received received = {.action = ZXSD_BIND_VALIDATE_ONLY, .arena = &arena};
	const char *xml = "<uniqueDoc xmlns='urn:stream'><row id='7'><label/></row>"
			  "<row id='7'><label/></row><check>0</check></uniqueDoc>";

	zxsd_bind_arena_init(&arena, storage, sizeof(storage));
	CHECK(stream_bindings_uniqueDoc_row_reader_init(&reader, &row, &arena, row_received,
							&received, NULL)
		      .code == ZXSD_ERR_RESOURCE_LIMIT);
	stream_bindings_uniqueDoc_row_reader_dispose(&reader);
	CHECK(stream_bindings_uniqueDoc_row_reader_init(&reader, &row, &arena, row_received,
							&received, &scratch)
		      .code == ZXSD_OK);
	CHECK(stream_bindings_uniqueDoc_row_reader_feed(&reader, xml, strlen(xml)).code == ZXSD_OK);
	CHECK(received.count == 1u);
	CHECK(stream_bindings_uniqueDoc_row_reader_finish(&reader).code ==
	      ZXSD_ERR_IDENTITY_CONSTRAINT);
	CHECK(arena.used == 0u && row.label == NULL);
	stream_bindings_uniqueDoc_row_reader_dispose(&reader);
	CHECK(usage.allocations >= 1u && usage.releases == usage.allocations);
}

/** Repeat more selected subtrees than fit at once, using the same tiny arena. */
static void reuse_and_equivalence(void)
{
	static stream_bindings_doc_row_reader_t reader;
	static unsigned char storage[256];
	stream_bindings_Row_t row;
	zxsd_bind_arena_t arena;
	struct received received = {.arena = &arena};
	const char *item = "<row id='7'><label/></row>";
	size_t index;
	size_t iterations = ZXSD_MAX_INPUT > 65536u ? 2048u : 32u;

	zxsd_bind_arena_init(&arena, storage, sizeof(storage));
	CHECK(stream_bindings_doc_row_reader_init(&reader, &row, &arena, row_received, &received,
						  NULL)
		      .code == ZXSD_OK);
	CHECK(stream_bindings_doc_row_reader_feed(&reader, "<doc xmlns='urn:stream'>", 24u).code ==
	      ZXSD_OK);
	for (index = 0u; index < iterations; ++index) {
		CHECK(stream_bindings_doc_row_reader_feed(&reader, item, strlen(item)).code ==
		      ZXSD_OK);
		CHECK(arena.used == 0u);
	}
	CHECK(stream_bindings_doc_row_reader_feed(&reader, "<check>0</check></doc>", 22u).code ==
	      ZXSD_OK);
	CHECK(stream_bindings_doc_row_reader_finish(&reader).code == ZXSD_OK);
	CHECK(received.count == iterations);
	stream_bindings_doc_row_reader_dispose(&reader);
	{
		static stream_bindings_row_reader_t whole;
		static unsigned char expected_storage[256];
		stream_bindings_Row_t expected;
		zxsd_bind_arena_t expected_arena;
		const char *root = "<row xmlns='urn:stream' id='7'><label/></row>";

		zxsd_bind_arena_init(&expected_arena, expected_storage, sizeof(expected_storage));
		CHECK(stream_bindings_row_read(root, &expected, &expected_arena).code == ZXSD_OK);
		received = (struct received){.arena = &arena};
		CHECK(stream_bindings_row_reader_init(&whole, &row, &arena, row_received, &received,
						      NULL)
			      .code == ZXSD_OK);
		CHECK(stream_bindings_row_reader_feed(&whole, root, strlen(root)).code == ZXSD_OK);
		CHECK(stream_bindings_row_reader_finish(&whole).code == ZXSD_OK);
		CHECK(expected.id == 7u && expected.enabled &&
		      expected.kind == STREAM_BINDINGS_KIND_LIVE);
		CHECK(received.count == 1u && strcmp(received.label, expected.label) == 0);
		stream_bindings_row_reader_dispose(&whole);
	}
}

/** Run portable binding-stream tests, also callable by the Zephyr extern wrapper. */
int zxsd_test_stream_bindings(void)
{
	failures = 0;
	arbitrary_splits();
	validation_and_limits();
	scalar_and_dynamic();
	identities();
	reuse_and_equivalence();
	return failures != 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_stream_bindings();
}
#endif
