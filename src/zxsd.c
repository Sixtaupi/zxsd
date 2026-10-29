/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public validation facade and runtime resource checks.
 */

#include "zxsd_internal.h"

#include <stdlib.h>

/** Count recursive particles while enforcing a traversal-depth bound. */
static size_t schema_particle_count(const zxsd_particle_t *particle, size_t depth)
{
	size_t count = 1u;
	size_t index;

	if (particle == NULL) {
		return 0u;
	}
	if (depth >= ZXSD_MAX_DEPTH) {
		return ZXSD_MAX_SCHEMA_COMPLEXITY + 1u;
	}
	for (index = 0; index < particle->child_count; index++) {
		size_t children = schema_particle_count(&particle->children[index], depth + 1u);

		if (children > ZXSD_MAX_SCHEMA_COMPLEXITY - count) {
			return ZXSD_MAX_SCHEMA_COMPLEXITY + 1u;
		}
		count += children;
	}
	return count;
}

static int schema_xpath_within_limits(const zxsd_xpath_t *xpath)
{
	size_t index;

	if (xpath->step_count > ZXSD_MAX_XPATH_DEPTH ||
	    xpath->alternative_count > ZXSD_MAX_XPATH_ALTERNATIVES) {
		return 0;
	}
	for (index = 0u; index < xpath->alternative_count; ++index) {
		if (!schema_xpath_within_limits(&xpath->alternatives[index])) {
			return 0;
		}
	}
	return 1;
}

const char *zxsd_error_string(zxsd_error_t error)
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
		"incompatible runtime ABI or configuration",
	};

	if ((unsigned)error >= sizeof(messages) / sizeof(messages[0])) {
		return "unknown error";
	}
	return messages[error];
}

/** Check runtime-visible descriptor counts against configured limits. */
static int schema_within_limits(const zxsd_schema_t *schema)
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
	if (complexity > ZXSD_MAX_SCHEMA_COMPLEXITY ||
	    schema->identity_count > ZXSD_MAX_IDENTITY_CONSTRAINTS) {
		return 0;
	}
	for (identity = 0u; identity < schema->identity_count; ++identity) {
		const zxsd_identity_t *constraint = &schema->identities[identity];
		size_t field;

		if (constraint->field_count > ZXSD_MAX_IDENTITY_FIELDS ||
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

zxsd_result_t zxsd_validate_with_allocator(const zxsd_schema_t *schema, const char *xml,
					   const zxsd_allocator_t *allocator)
{
	zxsd_result_t result;

	if (schema == NULL || xml == NULL) {
		return make_result(ZXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (!schema_within_limits(schema)) {
		return make_result(ZXSD_ERR_RESOURCE_LIMIT, 0, schema->root_name);
	}

	result = zxsd_dom_validate(schema, xml);
	if (result.code != ZXSD_OK) {
		return result;
	}
	return validate_identities(schema, xml, allocator);
}

#ifndef ZXSD_NO_HEAP
/** Standard-library allocation adapter used by zxsd_validate(). */
static void *default_allocate(void *context, size_t size)
{
	(void)context;
	return malloc(size);
}

/** Standard-library deallocation adapter used by zxsd_validate(). */
static void default_deallocate(void *context, void *pointer)
{
	(void)context;
	free(pointer);
}
#endif

zxsd_result_t zxsd_validate(const zxsd_schema_t *schema, const char *xml)
{
#ifdef ZXSD_NO_HEAP
	return zxsd_validate_with_allocator(schema, xml, NULL);
#else
	const zxsd_allocator_t allocator = {default_allocate, default_deallocate, NULL};

	return zxsd_validate_with_allocator(schema, xml, &allocator);
#endif
}
