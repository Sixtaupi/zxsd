/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public validation facade and runtime resource checks.
 */

#include "mxsd_internal.h"

#include <stdlib.h>

/** Count recursive particles while enforcing a traversal-depth bound. */
static size_t schema_particle_count(const mxsd_particle_t *particle, size_t depth)
{
	size_t count = 1u;
	size_t index;

	if (particle == NULL) {
		return 0u;
	}
	if (depth >= MXSD_MAX_DEPTH) {
		return MXSD_MAX_SCHEMA_COMPLEXITY + 1u;
	}
	for (index = 0; index < particle->child_count; index++) {
		size_t children = schema_particle_count(&particle->children[index], depth + 1u);

		if (children > MXSD_MAX_SCHEMA_COMPLEXITY - count) {
			return MXSD_MAX_SCHEMA_COMPLEXITY + 1u;
		}
		count += children;
	}
	return count;
}

static int schema_xpath_within_limits(const mxsd_xpath_t *xpath)
{
	size_t index;

	if (xpath->step_count > MXSD_MAX_XPATH_DEPTH ||
	    xpath->alternative_count > MXSD_MAX_XPATH_ALTERNATIVES) {
		return 0;
	}
	for (index = 0u; index < xpath->alternative_count; ++index) {
		if (!schema_xpath_within_limits(&xpath->alternatives[index])) {
			return 0;
		}
	}
	return 1;
}

const char *mxsd_error_string(mxsd_error_t error)
{
	static const char *const messages[] = {
		"ok",
		"invalid argument",
		"input too large",
		"malformed XML",
		"excessive nesting",
		"wrong root element",
		"missing required element",
		"unexpected element",
		"incorrect element order",
		"occurrence violation",
		"missing required attribute",
		"unexpected attribute",
		"invalid primitive value",
		"text is not allowed here",
		"identity constraint violation",
		"streaming capacity exceeded",
		"resource limit exceeded",
		"output failure",
		"unsupported by binding",
	};

	if ((unsigned)error >= sizeof(messages) / sizeof(messages[0])) {
		return "unknown error";
	}
	return messages[error];
}

/** Check runtime-visible descriptor counts against configured limits. */
static int schema_within_limits(const mxsd_schema_t *schema)
{
	size_t complexity = schema->global_element_count + schema->global_attribute_count +
			    schema->named_type_count + schema->identity_count +
			    schema->notation_count;

	size_t identity;

	if (schema->root_complex != NULL) {
		complexity +=
			schema->root_complex->element_count + schema->root_complex->attribute_count;
		complexity += schema_particle_count(schema->root_complex->particle, 0u);
	}
	if (complexity > MXSD_MAX_SCHEMA_COMPLEXITY ||
	    schema->identity_count > MXSD_MAX_IDENTITY_CONSTRAINTS) {
		return 0;
	}
	for (identity = 0u; identity < schema->identity_count; ++identity) {
		const mxsd_identity_t *constraint = &schema->identities[identity];
		size_t field;

		if (constraint->field_count > MXSD_MAX_IDENTITY_FIELDS ||
		    !schema_xpath_within_limits(&constraint->scope) ||
		    !schema_xpath_within_limits(&constraint->selector)) {
			return 0;
		}
		for (field = 0u; field < constraint->field_count; ++field) {
			if (!schema_xpath_within_limits(&constraint->fields[field])) {
				return 0;
			}
		}
	}
	return 1;
}

mxsd_result_t mxsd_validate_with_allocator(const mxsd_schema_t *schema, const char *xml,
					   const mxsd_allocator_t *allocator)
{
	mxsd_result_t result;

	if (schema == NULL || xml == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (!schema_within_limits(schema)) {
		return make_result(MXSD_ERR_RESOURCE_LIMIT, 0, schema->root_name);
	}

	result = mxsd_dom_validate(schema, xml);
	if (result.code != MXSD_OK) {
		return result;
	}
	return validate_identities(schema, xml, allocator);
}

#ifndef MXSD_NO_HEAP
/** Standard-library allocation adapter used by mxsd_validate(). */
static void *default_allocate(void *context, size_t size)
{
	(void)context;
	return malloc(size);
}

/** Standard-library deallocation adapter used by mxsd_validate(). */
static void default_deallocate(void *context, void *pointer)
{
	(void)context;
	free(pointer);
}
#endif

mxsd_result_t mxsd_validate(const mxsd_schema_t *schema, const char *xml)
{
#ifdef MXSD_NO_HEAP
	return mxsd_validate_with_allocator(schema, xml, NULL);
#else
	const mxsd_allocator_t allocator = {default_allocate, default_deallocate, NULL};

	return mxsd_validate_with_allocator(schema, xml, &allocator);
#endif
}
