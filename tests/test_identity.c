/*
 * Copyright (c) 2026 Sixtaupi
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Tests for unique, key and keyref identity constraints. */
#include "identity.zxsd.h"
#include <stdio.h>
#include <stdlib.h>

/** Counted allocator state used to verify allocator symmetry. */
typedef struct {
	size_t allocations;
	size_t releases;
} allocator_state_t;

/** Allocate one runtime scratch block. */
static void *test_allocate(void *context, size_t size)
{
	allocator_state_t *state = (allocator_state_t *)context;
	state->allocations++;
	return malloc(size);
}

/** Release one runtime scratch block. */
static void test_deallocate(void *context, void *pointer)
{
	allocator_state_t *state = (allocator_state_t *)context;
	state->releases++;
	free(pointer);
}

/** Validate one catalog and compare its result. */
static int expect(const char *xml, zxsd_error_t wanted)
{
	zxsd_result_t result = identity_catalog_validate(xml);
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d, got %d (%s), constraint=%s\n", (int)wanted, (int)result.code,
		result.message, result.path ? result.path : "-");
	return 1;
}

/** Emit one catalog through the event API and check its identity result. */
static int stream_expect(const char *second_alias, const char *order_product, zxsd_error_t wanted)
{
	zxsd_stream_t stream;
	allocator_state_t state = {0u, 0u};
	const zxsd_allocator_t allocator = {test_allocate, test_deallocate, &state};
	zxsd_result_t result =
		zxsd_stream_init_with_allocator(&stream, identity_catalog_schema(), &allocator);
#define EMIT(call)                                                                                 \
	do {                                                                                       \
		if (result.code == ZXSD_OK) {                                                      \
			result = (call);                                                           \
		}                                                                                  \
	} while (0)
	EMIT(zxsd_start_element(&stream, NULL, "catalog"));
	EMIT(zxsd_start_content(&stream));
	EMIT(zxsd_start_element(&stream, NULL, "product"));
	EMIT(zxsd_namespace(&stream, "p", "urn:products"));
	EMIT(zxsd_attribute(&stream, NULL, "code", "p1"));
	EMIT(zxsd_attribute(&stream, NULL, "alias", "same"));
	EMIT(zxsd_attribute(&stream, NULL, "qualified", "p:first"));
	EMIT(zxsd_start_content(&stream));
	EMIT(zxsd_start_element(&stream, NULL, "serial"));
	EMIT(zxsd_start_content(&stream));
	EMIT(zxsd_text(&stream, "S1", 2u));
	EMIT(zxsd_end_element(&stream, NULL, "serial"));
	EMIT(zxsd_end_element(&stream, NULL, "product"));
	EMIT(zxsd_start_element(&stream, NULL, "product"));
	EMIT(zxsd_namespace(&stream, "q", "urn:products"));
	EMIT(zxsd_attribute(&stream, NULL, "code", "p2"));
	EMIT(zxsd_attribute(&stream, NULL, "qualified", "q:second"));
	if (second_alias != NULL) {
		EMIT(zxsd_attribute(&stream, NULL, "alias", second_alias));
	}
	EMIT(zxsd_start_content(&stream));
	EMIT(zxsd_start_element(&stream, NULL, "serial"));
	EMIT(zxsd_start_content(&stream));
	EMIT(zxsd_text(&stream, "S2", 2u));
	EMIT(zxsd_end_element(&stream, NULL, "serial"));
	EMIT(zxsd_end_element(&stream, NULL, "product"));
	if (order_product != NULL) {
		EMIT(zxsd_start_element(&stream, NULL, "order"));
		EMIT(zxsd_namespace(&stream, "r", "urn:products"));
		EMIT(zxsd_attribute(&stream, NULL, "product", order_product));
		EMIT(zxsd_attribute(&stream, NULL, "qualifiedProduct", "r:second"));
		EMIT(zxsd_start_content(&stream));
		EMIT(zxsd_end_element(&stream, NULL, "order"));
	}
	EMIT(zxsd_end_element(&stream, NULL, "catalog"));
	EMIT(zxsd_stream_finish(&stream));
#undef EMIT
	if (result.code != wanted || state.allocations != state.releases) {
		fprintf(stderr, "stream wanted %d, got %d; allocations=%lu releases=%lu\n",
			(int)wanted, (int)result.code, (unsigned long)state.allocations,
			(unsigned long)state.releases);
		return 1;
	}
	return 0;
}

/** Exercise tuple uniqueness, mandatory keys and key references. */
int zxsd_test_identity(void)
{
	int failures = 0;
	static const char valid[] =
		"<catalog><product code='p1'><serial>S1</serial></product></catalog>";
	allocator_state_t state = {0u, 0u};
	const zxsd_allocator_t allocator = {test_allocate, test_deallocate, &state};
#ifdef ZXSD_NO_HEAP
	failures += expect(valid, ZXSD_ERR_RESOURCE_LIMIT);
#else
	failures += expect(
		"<catalog><product code='p1' alias='one'><serial>S1</serial></product><product "
		"code='p2'><serial>S2</serial></product><order product='p2'/></catalog>",
		ZXSD_OK);
	failures += expect("<?xml version='1.0'?><!--prolog--><catalog><product code='p1'>"
			   "<serial>S1</serial></product></catalog>",
			   ZXSD_OK);
	failures += expect(
		"<catalog><product code='p1' alias='same'><serial>S1</serial></product><product "
		"code='p2' alias='same'><serial>S2</serial></product></catalog>",
		ZXSD_ERR_IDENTITY_CONSTRAINT);
	failures += expect("<catalog><product code='S1'><serial>S1</serial></product><order "
			   "product='S1'/></catalog>",
			   ZXSD_ERR_IDENTITY_CONSTRAINT);
	failures += expect("<catalog><product><serial>S1</serial></product></catalog>",
			   ZXSD_ERR_IDENTITY_CONSTRAINT);
	failures += expect("<catalog><product code='p1'><serial>S1</serial></product><order "
			   "product='missing'/></catalog>",
			   ZXSD_ERR_IDENTITY_CONSTRAINT);
	failures += expect(
		"<catalog><product code='p1'><serial>S1</serial></product><order/></catalog>",
		ZXSD_OK);
	failures += expect(
		"<catalog><product code='p1' xmlId='same'><serial>S1</serial></product><product "
		"code='p2' xmlId='same'><serial>S2</serial></product></catalog>",
		ZXSD_ERR_IDENTITY_CONSTRAINT);
#endif
	if (zxsd_validate_with_allocator(identity_catalog_schema(), valid, NULL).code !=
	    ZXSD_ERR_RESOURCE_LIMIT) {
		failures++;
	}
	if (zxsd_validate_with_allocator(identity_catalog_schema(), valid, &allocator).code !=
		    ZXSD_OK ||
	    state.allocations != 1u || state.releases != 1u) {
		failures++;
	}
	failures += stream_expect(NULL, "p2", ZXSD_OK);
	failures += stream_expect("same", "p2", ZXSD_ERR_IDENTITY_CONSTRAINT);
	failures += stream_expect(NULL, "missing", ZXSD_ERR_IDENTITY_CONSTRAINT);
	return failures ? 1 : 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_identity();
}
#endif
