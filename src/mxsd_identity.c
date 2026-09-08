/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Bounded XSD identity-constraint validation.
 */

#include "mxsd_internal.h"

#include <ctype.h>
#include <string.h>

/** One value captured for an identity tuple. */
typedef struct {
	text_span_t text;
	int present;
} identity_value_t;

/** One completed key, unique or keyref tuple. */
typedef struct {
	identity_value_t fields[MXSD_MAX_IDENTITY_FIELDS];
} identity_row_t;

/** Bounded tuple storage for one generated constraint. */
typedef struct {
	identity_row_t rows[MXSD_MAX_IDENTITY_VALUES];
	size_t count;
} identity_table_t;

/** Expanded element name retained while walking an instance document. */
typedef struct {
	text_span_t local;
	text_span_t namespace_uri;
	size_t text_offset;
	int has_child;
} identity_frame_t;

/** Tuple under construction for a currently open selected element. */
typedef struct {
	size_t constraint;
	size_t base_depth;
	identity_row_t row;
} active_identity_t;

/** State used by the restricted, streaming identity XPath evaluator. */
typedef struct {
	const mxsd_schema_t *schema;
	identity_table_t *tables;
	identity_frame_t stack[MXSD_MAX_DEPTH];
	active_identity_t active[MXSD_MAX_IDENTITY_CONSTRAINTS * MXSD_MAX_DEPTH];
	size_t active_count;
} identity_state_t;

static text_span_t identity_trim(text_span_t value)
{
	while (value.length && isspace((unsigned char)value.data[0])) {
		value.data++;
		value.length--;
	}
	while (value.length && isspace((unsigned char)value.data[value.length - 1u])) {
		value.length--;
	}
	return value;
}

static int identity_step_matches(const mxsd_xpath_step_t *step, const identity_frame_t *frame)
{
	if (step->name != NULL && !span_equals_string(frame->local, step->name)) {
		return 0;
	}
	return span_equals_nullable(frame->namespace_uri, step->namespace_uri);
}

static int identity_path_matches(const mxsd_xpath_t *xpath, const identity_frame_t *stack,
				 size_t depth, size_t base_depth, int field_path)
{
	size_t element_steps = xpath->step_count - (xpath->attribute ? 1u : 0u);
	size_t relative_count = depth - base_depth;
	size_t start;
	size_t index;
	(void)field_path;
	if (xpath->descendant) {
		if (relative_count < element_steps) {
			return 0;
		}
		start = depth + 1u - element_steps;
	} else {
		if (relative_count != element_steps) {
			return 0;
		}
		start = base_depth + 1u;
	}
	for (index = 0; index < element_steps; index++) {
		if (!identity_step_matches(&xpath->steps[index], &stack[start + index])) {
			return 0;
		}
	}
	return 1;
}

static int identity_values_equal(identity_value_t left, identity_value_t right)
{
	text_span_t a = identity_trim(left.text);
	text_span_t b = identity_trim(right.text);
	return left.present && right.present && a.length == b.length &&
	       memcmp(a.data, b.data, a.length) == 0;
}

static int identity_rows_equal(const identity_row_t *left, const identity_row_t *right,
			       size_t field_count)
{
	size_t index;
	for (index = 0; index < field_count; index++) {
		if (!identity_values_equal(left->fields[index], right->fields[index])) {
			return 0;
		}
	}
	return 1;
}

static mxsd_result_t finalize_identity(identity_state_t *state, size_t active_index, size_t offset)
{
	active_identity_t *active = &state->active[active_index];
	const mxsd_identity_t *constraint = &state->schema->identities[active->constraint];
	identity_table_t *table = &state->tables[active->constraint];
	size_t field;
	size_t row;
	int complete = 1;
	for (field = 0; field < constraint->field_count; field++) {
		if (!active->row.fields[field].present) {
			complete = 0;
		}
	}
	if (!complete) {
		if (constraint->kind == MXSD_ID_KEY) {
			return make_result(MXSD_ERR_IDENTITY_CONSTRAINT, offset, constraint->name);
		}
		return make_result(MXSD_OK, offset, NULL);
	}
	if (constraint->kind != MXSD_ID_KEYREF) {
		for (row = 0; row < table->count; row++) {
			if (identity_rows_equal(&table->rows[row], &active->row,
						constraint->field_count)) {
				return make_result(MXSD_ERR_IDENTITY_CONSTRAINT, offset,
						   constraint->name);
			}
		}
	}
	if (table->count >= MXSD_MAX_IDENTITY_VALUES) {
		return make_result(MXSD_ERR_IDENTITY_CONSTRAINT, offset, constraint->name);
	}
	table->rows[table->count++] = active->row;
	return make_result(MXSD_OK, offset, NULL);
}

static void capture_identity_attributes(xml_parser_t *parser, const start_tag_t *tag,
					identity_state_t *state, size_t depth)
{
	size_t parsed = 0;
	size_t saved = parser->position;
	parser->position = tag->attributes_offset;
	while (parsed < tag->attribute_count) {
		text_span_t name, value;
		size_t active_index;
		skip_whitespace(parser);
		if (!parse_attribute(parser, &name, &value)) {
			break;
		}
		if (is_namespace_declaration(name)) {
			continue;
		}
		parsed++;
		for (active_index = 0; active_index < state->active_count; active_index++) {
			active_identity_t *active = &state->active[active_index];
			const mxsd_identity_t *constraint =
				&state->schema->identities[active->constraint];
			size_t field;
			for (field = 0; field < constraint->field_count; field++) {
				const mxsd_xpath_t *xpath = &constraint->fields[field];
				const mxsd_xpath_step_t *attribute_step;
				if (!xpath->attribute || active->row.fields[field].present ||
				    !identity_path_matches(xpath, state->stack, depth,
							   active->base_depth, 1)) {
					continue;
				}
				attribute_step = &xpath->steps[xpath->step_count - 1u];
				if (expanded_name_equals(parser, name, attribute_step->name,
							 attribute_step->namespace_uri, 1)) {
					active->row.fields[field].text = value;
					active->row.fields[field].present = 1;
				}
			}
		}
	}
	parser->position = saved;
}

static mxsd_result_t scan_identity_element(xml_parser_t *parser, identity_state_t *state,
					   size_t depth)
{
	start_tag_t tag;
	text_span_t prefix, local;
	mxsd_result_t result;
	size_t constraint_index;
	size_t active_base;
	if (depth >= MXSD_MAX_DEPTH) {
		return make_result(MXSD_ERR_EXCESSIVE_NESTING, parser->position, NULL);
	}
	result = parse_start_tag(parser, NULL, &tag);
	if (result.code != MXSD_OK) {
		return result;
	}
	split_qualified_name(tag.name, &prefix, &local);
	state->stack[depth].local = local;
	state->stack[depth].namespace_uri = resolve_namespace(parser, prefix, 0);
	state->stack[depth].text_offset = parser->position;
	state->stack[depth].has_child = 0;
	active_base = state->active_count;
	for (constraint_index = 0; constraint_index < state->schema->identity_count;
	     constraint_index++) {
		const mxsd_identity_t *constraint = &state->schema->identities[constraint_index];
		if (identity_path_matches(&constraint->selector, state->stack, depth, 0, 0)) {
			active_identity_t *active;
			if (state->active_count >= MXSD_MAX_IDENTITY_CONSTRAINTS * MXSD_MAX_DEPTH) {
				return make_result(MXSD_ERR_IDENTITY_CONSTRAINT, tag.start_offset,
						   constraint->name);
			}
			active = &state->active[state->active_count++];
			memset(active, 0, sizeof(*active));
			active->constraint = constraint_index;
			active->base_depth = depth;
		}
	}
	capture_identity_attributes(parser, &tag, state, depth);
	if (!tag.is_empty) {
		while (parser->position < parser->length) {
			if (at_end_element(parser)) {
				break;
			}
			if (parser->input[parser->position] == '<') {
				state->stack[depth].has_child = 1;
				result = scan_identity_element(parser, state, depth + 1u);
				if (result.code != MXSD_OK) {
					return result;
				}
			} else {
				parser->position++;
			}
		}
		if (!state->stack[depth].has_child) {
			text_span_t text = {parser->input + state->stack[depth].text_offset,
					    parser->position - state->stack[depth].text_offset};
			size_t active_index;
			for (active_index = 0; active_index < state->active_count; active_index++) {
				active_identity_t *active = &state->active[active_index];
				const mxsd_identity_t *constraint =
					&state->schema->identities[active->constraint];
				size_t field;
				for (field = 0; field < constraint->field_count; field++) {
					const mxsd_xpath_t *xpath = &constraint->fields[field];
					if (!xpath->attribute &&
					    !active->row.fields[field].present &&
					    identity_path_matches(xpath, state->stack, depth,
								  active->base_depth, 1)) {
						active->row.fields[field].text = text;
						active->row.fields[field].present = 1;
					}
				}
			}
		}
		parser->position += 2u;
		{
			text_span_t closing;
			if (!parse_xml_name(parser, &closing)) {
				return make_result(MXSD_ERR_MALFORMED_XML, parser->position, NULL);
			}
		}
		skip_whitespace(parser);
		if (parser->position >= parser->length ||
		    parser->input[parser->position++] != '>') {
			return make_result(MXSD_ERR_MALFORMED_XML, parser->position, NULL);
		}
	}
	while (state->active_count > active_base) {
		result = finalize_identity(state, state->active_count - 1u, tag.start_offset);
		state->active_count--;
		if (result.code != MXSD_OK) {
			return result;
		}
	}
	parser->namespace_count = tag.namespace_base;
	return make_result(MXSD_OK, parser->position, NULL);
}

mxsd_result_t mxsd_validate_identities(const mxsd_schema_t *schema, const char *xml,
				       const mxsd_allocator_t *allocator)
{
	xml_parser_t parser;
	identity_state_t *state;
	identity_table_t *tables;
	void *scratch;
	mxsd_result_t result;
	size_t constraint;
	size_t row;
	if (schema->identity_count == 0) {
		return make_result(MXSD_OK, 0, NULL);
	}
	if (schema->identity_count > MXSD_MAX_IDENTITY_CONSTRAINTS) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (allocator == NULL || allocator->allocate == NULL || allocator->deallocate == NULL) {
		return make_result(MXSD_ERR_RESOURCE_LIMIT, 0, NULL);
	}
	scratch = allocator->allocate(allocator->context,
				      sizeof(*state) + schema->identity_count * sizeof(*tables));
	if (scratch == NULL) {
		return make_result(MXSD_ERR_RESOURCE_LIMIT, 0, NULL);
	}
	memset(scratch, 0, sizeof(*state) + schema->identity_count * sizeof(*tables));
	state = (identity_state_t *)scratch;
	tables = (identity_table_t *)(state + 1);
	memset(&parser, 0, sizeof(parser));
	parser.input = xml;
	parser.length = strlen(xml);
	state->schema = schema;
	state->tables = tables;
	skip_whitespace(&parser);
	result = skip_xml_declaration(&parser);
	if (result.code == MXSD_OK) {
		result = scan_identity_element(&parser, state, 0);
	}
	if (result.code == MXSD_OK) {
		for (constraint = 0; constraint < schema->identity_count; constraint++) {
			const mxsd_identity_t *identity = &schema->identities[constraint];
			if (identity->kind != MXSD_ID_KEYREF) {
				continue;
			}
			for (row = 0; row < tables[constraint].count; row++) {
				size_t target;
				int found = 0;
				for (target = 0; target < tables[identity->refer_index].count;
				     target++) {
					if (identity_rows_equal(
						    &tables[constraint].rows[row],
						    &tables[identity->refer_index].rows[target],
						    identity->field_count)) {
						found = 1;
						break;
					}
				}
				if (!found) {
					result = make_result(MXSD_ERR_IDENTITY_CONSTRAINT, 0,
							     identity->name);
					break;
				}
			}
			if (result.code != MXSD_OK) {
				break;
			}
		}
	}
	allocator->deallocate(allocator->context, scratch);
	return result;
}
