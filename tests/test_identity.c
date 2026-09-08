/*
 * Copyright (c) 2026 Romain Pelletant
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Tests for unique, key and keyref identity constraints. */
#include "identity.mxsd.h"
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
static int expect(const char *xml, mxsd_error_t wanted)
{
	mxsd_result_t result = identity_catalog_validate(xml);
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d, got %d (%s), constraint=%s\n", (int)wanted, (int)result.code,
		result.message, result.path ? result.path : "-");
	return 1;
}

/** Exercise tuple uniqueness, mandatory keys and key references. */
int main(void)
{
	int failures = 0;
	static const char valid[] =
		"<catalog><product code='p1'><serial>S1</serial></product></catalog>";
	allocator_state_t state = {0u, 0u};
	const mxsd_allocator_t allocator = {test_allocate, test_deallocate, &state};
#ifdef MXSD_NO_HEAP
	failures += expect(valid, MXSD_ERR_RESOURCE_LIMIT);
#else
	failures += expect(
		"<catalog><product code='p1' alias='one'><serial>S1</serial></product><product "
		"code='p2'><serial>S2</serial></product><order product='p2'/></catalog>",
		MXSD_OK);
	failures += expect(
		"<catalog><product code='p1' alias='same'><serial>S1</serial></product><product "
		"code='p2' alias='same'><serial>S2</serial></product></catalog>",
		MXSD_ERR_IDENTITY_CONSTRAINT);
	failures += expect("<catalog><product><serial>S1</serial></product></catalog>",
			   MXSD_ERR_IDENTITY_CONSTRAINT);
	failures += expect("<catalog><product code='p1'><serial>S1</serial></product><order "
			   "product='missing'/></catalog>",
			   MXSD_ERR_IDENTITY_CONSTRAINT);
	failures += expect(
		"<catalog><product code='p1'><serial>S1</serial></product><order/></catalog>",
		MXSD_OK);
#endif
	if (mxsd_validate_with_allocator(identity_catalog_schema(), valid, NULL).code !=
	    MXSD_ERR_RESOURCE_LIMIT) {
		failures++;
	}
	if (mxsd_validate_with_allocator(identity_catalog_schema(), valid, &allocator).code !=
		    MXSD_OK ||
	    state.allocations != 1u || state.releases != 1u) {
		failures++;
	}
	return failures ? 1 : 0;
}
