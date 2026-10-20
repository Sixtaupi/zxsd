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

#include <string.h>

/** One value captured for an identity tuple. */
typedef struct {
	text_span_t text;
	text_span_t namespace_uri;
	mxsd_type_t type;
	const mxsd_simple_t *simple_type;
	int present;
	int nilled;
	int typed;
	int qname;
} identity_value_t;

/** One completed key, unique or keyref tuple. */
typedef struct {
	identity_value_t fields[MXSD_MAX_IDENTITY_FIELDS];
	size_t scope_offset;
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
	size_t start_offset;
	size_t text_offset;
	mxsd_type_t type;
	const mxsd_complex_t *complex_type;
	const mxsd_simple_t *simple_type;
	int has_child;
	int nilled;
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

static int identity_step_matches(const mxsd_xpath_step_t *step, const identity_frame_t *frame)
{
	if (step->name != NULL && !span_equals_string(frame->local, step->name)) {
		return 0;
	}
	if (step->name == NULL && step->namespace_uri == NULL) {
		return 1;
	}
	return span_equals_nullable(frame->namespace_uri, step->namespace_uri);
}

static int identity_single_path_matches(const mxsd_xpath_t *xpath, const identity_frame_t *stack,
					size_t depth, size_t base_depth)
{
	size_t element_steps = xpath->step_count - (xpath->attribute ? 1u : 0u);
	size_t relative_count = depth - base_depth;
	size_t start;
	size_t index;
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

static int identity_path_matches(const mxsd_xpath_t *xpath, const identity_frame_t *stack,
				 size_t depth, size_t base_depth)
{
	size_t index;

	if (xpath->alternative_count == 0u) {
		return identity_single_path_matches(xpath, stack, depth, base_depth);
	}
	for (index = 0; index < xpath->alternative_count; index++) {
		if (identity_path_matches(&xpath->alternatives[index], stack, depth, base_depth)) {
			return 1;
		}
	}
	return 0;
}

static int identity_element_path_matches(const mxsd_xpath_t *xpath, const identity_frame_t *stack,
					 size_t depth, size_t base_depth)
{
	size_t index;

	if (xpath->alternative_count == 0u) {
		return !xpath->attribute &&
		       identity_single_path_matches(xpath, stack, depth, base_depth);
	}
	for (index = 0; index < xpath->alternative_count; index++) {
		if (identity_element_path_matches(&xpath->alternatives[index], stack, depth,
						  base_depth)) {
			return 1;
		}
	}
	return 0;
}

static int identity_scope_depth(const mxsd_identity_t *constraint, const identity_frame_t *stack,
				size_t depth, size_t *scope_depth)
{
	size_t candidate;
	if (constraint->scope.step_count == 0u && constraint->scope.alternative_count == 0u) {
		*scope_depth = 0u;
		return 1;
	}
	for (candidate = depth + 1u; candidate > 0u; candidate--) {
		size_t current = candidate - 1u;
		if (identity_element_path_matches(&constraint->scope, stack, current, 0u)) {
			*scope_depth = current;
			return 1;
		}
	}
	return 0;
}

static int identity_attribute_path_matches(xml_parser_t *parser, text_span_t name,
					   const mxsd_xpath_t *xpath, const identity_frame_t *stack,
					   size_t depth, size_t base_depth)
{
	size_t index;

	if (xpath->alternative_count == 0u) {
		const mxsd_xpath_step_t *attribute_step;
		text_span_t prefix, local, namespace_uri;

		if (!xpath->attribute ||
		    !identity_single_path_matches(xpath, stack, depth, base_depth)) {
			return 0;
		}
		attribute_step = &xpath->steps[xpath->step_count - 1u];
		if (attribute_step->name == NULL) {
			split_qualified_name(name, &prefix, &local);
			namespace_uri = resolve_namespace(parser, prefix, 1);
			return attribute_step->namespace_uri == NULL ||
			       span_equals_string(namespace_uri, attribute_step->namespace_uri);
		}
		return expanded_name_equals(parser, name, attribute_step->name,
					    attribute_step->namespace_uri, 1);
	}
	for (index = 0; index < xpath->alternative_count; index++) {
		if (identity_attribute_path_matches(parser, name, &xpath->alternatives[index],
						    stack, depth, base_depth)) {
			return 1;
		}
	}
	return 0;
}

static int identity_type_family(mxsd_type_t type)
{
	switch (type) {
	case MXSD_T_DECIMAL:
	case MXSD_T_INTEGER:
	case MXSD_T_LONG:
	case MXSD_T_INT:
	case MXSD_T_SHORT:
	case MXSD_T_BYTE:
	case MXSD_T_NON_POSITIVE_INTEGER:
	case MXSD_T_NEGATIVE_INTEGER:
	case MXSD_T_NON_NEGATIVE_INTEGER:
	case MXSD_T_POSITIVE_INTEGER:
	case MXSD_T_UNSIGNED_LONG:
	case MXSD_T_UNSIGNED_INT:
	case MXSD_T_UNSIGNED_SHORT:
	case MXSD_T_UNSIGNED_BYTE:
		return 1;
	case MXSD_T_STRING:
	case MXSD_T_NORMALIZED_STRING:
	case MXSD_T_TOKEN:
	case MXSD_T_LANGUAGE:
	case MXSD_T_NAME:
	case MXSD_T_NCNAME:
	case MXSD_T_ID:
	case MXSD_T_IDREF:
	case MXSD_T_ENTITY:
		return 2;
	default:
		return 100 + (int)type;
	}
}

static void identity_set_value(xml_parser_t *parser, identity_value_t *field, text_span_t text,
			       mxsd_type_t type, const mxsd_simple_t *simple_type, int typed)
{
	mxsd_type_t effective = simple_type != NULL ? simple_type->base : type;

	field->text = text;
	field->type = type;
	field->simple_type = simple_type;
	field->typed = typed;
	if (typed && (effective == MXSD_T_QNAME || effective == MXSD_T_NOTATION)) {
		text_span_t prefix;
		text_span_t local;

		while (field->text.length != 0u &&
		       (field->text.data[0] == ' ' || field->text.data[0] == '\t' ||
			field->text.data[0] == '\r' || field->text.data[0] == '\n')) {
			field->text.data++;
			field->text.length--;
		}
		while (field->text.length != 0u &&
		       (field->text.data[field->text.length - 1u] == ' ' ||
			field->text.data[field->text.length - 1u] == '\t' ||
			field->text.data[field->text.length - 1u] == '\r' ||
			field->text.data[field->text.length - 1u] == '\n')) {
			field->text.length--;
		}
		split_qualified_name(field->text, &prefix, &local);
		field->text = local;
		field->namespace_uri = resolve_namespace(parser, prefix, 0);
		field->qname = 1;
	}
}

static int identity_values_equal(const xml_parser_t *parser, identity_value_t left,
				 identity_value_t right)
{
	char right_text[MXSD_MAX_TEXT_LENGTH + 1u];
	text_span_t a = left.text;
	text_span_t b = right.text;
	mxsd_type_t left_type = left.simple_type != NULL ? left.simple_type->base : left.type;
	mxsd_type_t right_type = right.simple_type != NULL ? right.simple_type->base : right.type;

	if (!left.present || !right.present || !left.typed || !right.typed) {
		return 0;
	}
	if (left.nilled || right.nilled) {
		return left.nilled && right.nilled;
	}
	if (left.qname || right.qname) {
		return left.qname && right.qname && left.text.length == right.text.length &&
		       memcmp(left.text.data, right.text.data, left.text.length) == 0 &&
		       left.namespace_uri.length == right.namespace_uri.length &&
		       (left.namespace_uri.length == 0u ||
			memcmp(left.namespace_uri.data, right.namespace_uri.data,
			       left.namespace_uri.length) == 0);
	}
	if (identity_type_family(left_type) != identity_type_family(right_type) ||
	    b.length > MXSD_MAX_TEXT_LENGTH) {
		return 0;
	}
	memcpy(right_text, b.data, b.length);
	right_text[b.length] = '\0';
	return simple_values_equal(parser, left.type, left.simple_type, a, right_text);
}

static int identity_rows_equal(const xml_parser_t *parser, const identity_row_t *left,
			       const identity_row_t *right, size_t field_count)
{
	size_t index;
	for (index = 0; index < field_count; index++) {
		if (!identity_values_equal(parser, left->fields[index], right->fields[index])) {
			return 0;
		}
	}
	return 1;
}

static mxsd_result_t finalize_identity(xml_parser_t *parser, identity_state_t *state,
				       size_t active_index, size_t offset)
{
	active_identity_t *active = &state->active[active_index];
	const mxsd_identity_t *constraint = &state->schema->identities[active->constraint];
	identity_table_t *table = &state->tables[active->constraint];
	size_t field;
	size_t row;
	int complete = 1;
	for (field = 0; field < constraint->field_count; field++) {
		if (active->row.fields[field].present > 1) {
			return make_result(MXSD_ERR_IDENTITY_CONSTRAINT, offset, constraint->name);
		}
		if (active->row.fields[field].present == 0) {
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
			if (table->rows[row].scope_offset == active->row.scope_offset &&
			    identity_rows_equal(parser, &table->rows[row], &active->row,
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

static const mxsd_global_element_t *identity_global_element(const xml_parser_t *parser,
							    const identity_frame_t *frame)
{
	size_t index;
	for (index = 0; index < parser->global_element_count; index++) {
		const mxsd_global_element_t *element = &parser->global_elements[index];
		if (span_equals_string(frame->local, element->name) &&
		    span_equals_nullable(frame->namespace_uri, element->namespace_uri)) {
			return element;
		}
	}
	return NULL;
}

static const mxsd_attribute_t *identity_attribute(const xml_parser_t *parser,
						  const identity_frame_t *frame, text_span_t name)
{
	size_t index;
	if (frame->complex_type == NULL) {
		return NULL;
	}
	for (index = 0; index < frame->complex_type->attribute_count; index++) {
		const mxsd_attribute_t *attribute = &frame->complex_type->attributes[index];
		if (expanded_name_equals(parser, name, attribute->name, attribute->namespace_uri,
					 1)) {
			return attribute;
		}
	}
	return NULL;
}

static int identity_builtin_type(const xml_parser_t *parser, text_span_t lexical, mxsd_type_t *type)
{
	static const struct {
		const char *name;
		mxsd_type_t type;
	} builtins[] = {
		{"string", MXSD_T_STRING},
		{"boolean", MXSD_T_BOOLEAN},
		{"decimal", MXSD_T_DECIMAL},
		{"float", MXSD_T_FLOAT},
		{"double", MXSD_T_DOUBLE},
		{"integer", MXSD_T_INTEGER},
		{"long", MXSD_T_LONG},
		{"int", MXSD_T_INT},
		{"short", MXSD_T_SHORT},
		{"byte", MXSD_T_BYTE},
		{"nonPositiveInteger", MXSD_T_NON_POSITIVE_INTEGER},
		{"negativeInteger", MXSD_T_NEGATIVE_INTEGER},
		{"nonNegativeInteger", MXSD_T_NON_NEGATIVE_INTEGER},
		{"positiveInteger", MXSD_T_POSITIVE_INTEGER},
		{"unsignedLong", MXSD_T_UNSIGNED_LONG},
		{"unsignedInt", MXSD_T_UNSIGNED_INT},
		{"unsignedShort", MXSD_T_UNSIGNED_SHORT},
		{"unsignedByte", MXSD_T_UNSIGNED_BYTE},
		{"duration", MXSD_T_DURATION},
		{"dateTime", MXSD_T_DATE_TIME},
		{"time", MXSD_T_TIME},
		{"date", MXSD_T_DATE},
		{"gYearMonth", MXSD_T_GYEAR_MONTH},
		{"gYear", MXSD_T_GYEAR},
		{"gMonthDay", MXSD_T_GMONTH_DAY},
		{"gDay", MXSD_T_GDAY},
		{"gMonth", MXSD_T_GMONTH},
		{"hexBinary", MXSD_T_HEX_BINARY},
		{"base64Binary", MXSD_T_BASE64_BINARY},
		{"anyURI", MXSD_T_ANY_URI},
		{"QName", MXSD_T_QNAME},
		{"NOTATION", MXSD_T_NOTATION},
		{"normalizedString", MXSD_T_NORMALIZED_STRING},
		{"token", MXSD_T_TOKEN},
		{"language", MXSD_T_LANGUAGE},
		{"Name", MXSD_T_NAME},
		{"NCName", MXSD_T_NCNAME},
		{"NMTOKEN", MXSD_T_NMTOKEN},
		{"ID", MXSD_T_ID},
		{"IDREF", MXSD_T_IDREF},
		{"ENTITY", MXSD_T_ENTITY},
	};
	text_span_t prefix, local, uri;
	size_t index;
	split_qualified_name(lexical, &prefix, &local);
	uri = resolve_namespace(parser, prefix, 0);
	if (!span_equals_string(uri, "http://www.w3.org/2001/XMLSchema")) {
		return 0;
	}
	for (index = 0; index < sizeof(builtins) / sizeof(builtins[0]); index++) {
		if (span_equals_string(local, builtins[index].name)) {
			*type = builtins[index].type;
			return 1;
		}
	}
	return 0;
}

static void identity_prepare_frame(xml_parser_t *parser, const start_tag_t *tag,
				   const identity_frame_t *parent, identity_frame_t *frame)
{
	const mxsd_global_element_t *element = identity_global_element(parser, frame);
	const mxsd_element_t *local_element = NULL;
	size_t parsed = 0;
	size_t saved = parser->position;
	size_t index;

	if (element == NULL && parent != NULL && parent->complex_type != NULL) {
		for (index = 0; index < parent->complex_type->element_count; index++) {
			const mxsd_element_t *candidate = &parent->complex_type->elements[index];
			if (span_equals_string(frame->local, candidate->name) &&
			    span_equals_nullable(frame->namespace_uri, candidate->namespace_uri)) {
				local_element = candidate;
				break;
			}
		}
	}

	frame->type = element != NULL
			      ? element->type
			      : (local_element != NULL ? local_element->type : MXSD_T_STRING);
	frame->complex_type =
		element != NULL ? element->complex_type
				: (local_element != NULL ? local_element->complex_type : NULL);
	frame->simple_type = element != NULL
				     ? element->simple_type
				     : (local_element != NULL ? local_element->simple_type : NULL);
	if (frame->complex_type != NULL && frame->complex_type->has_simple_content) {
		frame->type = frame->complex_type->simple_content_type;
		frame->simple_type = frame->complex_type->simple_content;
	}
	parser->position = tag->attributes_offset;
	while (parsed < tag->attribute_count) {
		text_span_t name, value;
		skip_whitespace(parser);
		if (!parse_attribute(parser, &name, &value)) {
			break;
		}
		if (is_namespace_declaration(name)) {
			continue;
		}
		parsed++;
		if (expanded_name_equals(parser, name, "nil",
					 "http://www.w3.org/2001/XMLSchema-instance", 1)) {
			frame->nilled =
				span_equals_string(value, "true") || span_equals_string(value, "1");
		} else if (expanded_name_equals(parser, name, "type",
						"http://www.w3.org/2001/XMLSchema-instance", 1)) {
			mxsd_type_t builtin;
			if (identity_builtin_type(parser, value, &builtin)) {
				frame->type = builtin;
				frame->complex_type = NULL;
				frame->simple_type = NULL;
				continue;
			}
			for (index = 0; index < parser->named_type_count; index++) {
				const mxsd_named_type_t *candidate = &parser->named_types[index];
				text_span_t prefix, local, uri;
				split_qualified_name(value, &prefix, &local);
				uri = resolve_namespace(parser, prefix, 0);
				if (span_equals_string(local, candidate->name) &&
				    span_equals_nullable(uri, candidate->namespace_uri)) {
					frame->type = candidate->type;
					frame->complex_type = candidate->complex_type;
					frame->simple_type = candidate->simple_type;
					break;
				}
			}
		}
	}
	parser->position = saved;
}

static void capture_identity_attributes(xml_parser_t *parser, const start_tag_t *tag,
					identity_state_t *state, size_t depth)
{
	size_t parsed = 0;
	size_t saved = parser->position;
	size_t active_index;
	parser->position = tag->attributes_offset;
	while (parsed < tag->attribute_count) {
		text_span_t name, value;
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
				const mxsd_attribute_t *attribute;
				if (!identity_attribute_path_matches(parser, name, xpath,
								     state->stack, depth,
								     active->base_depth)) {
					continue;
				}
				attribute = identity_attribute(parser, &state->stack[depth], name);
				if (active->row.fields[field].present == 0) {
					identity_set_value(
						parser, &active->row.fields[field], value,
						attribute != NULL ? attribute->type : MXSD_T_STRING,
						attribute != NULL ? attribute->simple_type : NULL,
						attribute != NULL);
				}
				active->row.fields[field].present += attribute != NULL ? 1 : 2;
			}
		}
	}
	parser->position = saved;
	for (active_index = 0; active_index < state->active_count; active_index++) {
		active_identity_t *active = &state->active[active_index];
		const mxsd_identity_t *constraint = &state->schema->identities[active->constraint];
		size_t field;
		for (field = 0; field < constraint->field_count; field++) {
			const mxsd_xpath_t *xpath = &constraint->fields[field];
			size_t attribute_index;
			if (!xpath->attribute || xpath->alternative_count != 0u ||
			    active->row.fields[field].present != 0 ||
			    !identity_single_path_matches(xpath, state->stack, depth,
							  active->base_depth) ||
			    state->stack[depth].complex_type == NULL) {
				continue;
			}
			for (attribute_index = 0;
			     attribute_index < state->stack[depth].complex_type->attribute_count;
			     attribute_index++) {
				const mxsd_attribute_t *attribute =
					&state->stack[depth]
						 .complex_type->attributes[attribute_index];
				const mxsd_xpath_step_t *step =
					&xpath->steps[xpath->step_count - 1u];
				const char *effective = attribute->fixed_value != NULL
								? attribute->fixed_value
								: attribute->default_value;
				if (effective == NULL ||
				    (step->name != NULL &&
				     strcmp(step->name, attribute->name) != 0) ||
				    ((step->namespace_uri == NULL) !=
				     (attribute->namespace_uri == NULL)) ||
				    (step->namespace_uri != NULL &&
				     strcmp(step->namespace_uri, attribute->namespace_uri) != 0)) {
					continue;
				}
				{
					text_span_t value = {effective, strlen(effective)};
					identity_set_value(parser, &active->row.fields[field],
							   value, attribute->type,
							   attribute->simple_type, 1);
				}
				active->row.fields[field].present = 1;
				break;
			}
		}
	}
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
	state->stack[depth].start_offset = tag.start_offset;
	state->stack[depth].has_child = 0;
	state->stack[depth].nilled = 0;
	identity_prepare_frame(parser, &tag, depth == 0 ? NULL : &state->stack[depth - 1u],
			       &state->stack[depth]);
	active_base = state->active_count;
	for (constraint_index = 0; constraint_index < state->schema->identity_count;
	     constraint_index++) {
		const mxsd_identity_t *constraint = &state->schema->identities[constraint_index];
		size_t scope_depth;
		if (identity_scope_depth(constraint, state->stack, depth, &scope_depth) &&
		    identity_path_matches(&constraint->selector, state->stack, depth,
					  scope_depth)) {
			active_identity_t *active;
			if (state->active_count >= MXSD_MAX_IDENTITY_CONSTRAINTS * MXSD_MAX_DEPTH) {
				return make_result(MXSD_ERR_IDENTITY_CONSTRAINT, tag.start_offset,
						   constraint->name);
			}
			active = &state->active[state->active_count++];
			memset(active, 0, sizeof(*active));
			active->constraint = constraint_index;
			active->base_depth = depth;
			active->row.scope_offset = state->stack[scope_depth].start_offset;
		}
	}
	capture_identity_attributes(parser, &tag, state, depth);
	if (!tag.is_empty) {
		while (parser->position < parser->length) {
			if (at_end_element(parser)) {
				break;
			}
			if ((parser->position + 4u <= parser->length &&
			     memcmp(parser->input + parser->position, "<!--", 4u) == 0) ||
			    (parser->position + 2u <= parser->length &&
			     memcmp(parser->input + parser->position, "<?", 2u) == 0)) {
				skip_ignorable_markup(parser);
				continue;
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
					if (identity_element_path_matches(xpath, state->stack,
									  depth,
									  active->base_depth)) {
						if (active->row.fields[field].present == 0) {
							identity_set_value(
								parser, &active->row.fields[field],
								text, state->stack[depth].type,
								state->stack[depth].simple_type, 1);
						}
						active->row.fields[field].present +=
							state->stack[depth].type == MXSD_T_COMPLEX
								? 2
								: 1;
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
	if (state->stack[depth].has_child) {
		size_t active_index;
		for (active_index = 0; active_index < state->active_count; active_index++) {
			active_identity_t *active = &state->active[active_index];
			const mxsd_identity_t *constraint =
				&state->schema->identities[active->constraint];
			size_t field;
			for (field = 0; field < constraint->field_count; field++) {
				if (identity_element_path_matches(&constraint->fields[field],
								  state->stack, depth,
								  active->base_depth)) {
					active->row.fields[field].present += 2;
				}
			}
		}
	}
	if (tag.is_empty || state->stack[depth].nilled) {
		size_t active_index;
		for (active_index = 0; active_index < state->active_count; active_index++) {
			active_identity_t *active = &state->active[active_index];
			const mxsd_identity_t *constraint =
				&state->schema->identities[active->constraint];
			size_t field;
			for (field = 0; field < constraint->field_count; field++) {
				if (identity_element_path_matches(&constraint->fields[field],
								  state->stack, depth,
								  active->base_depth)) {
					{
						text_span_t empty = {"", 0u};
						identity_set_value(
							parser, &active->row.fields[field], empty,
							state->stack[depth].type,
							state->stack[depth].simple_type, 1);
					}
					active->row.fields[field].nilled =
						state->stack[depth].nilled;
					active->row.fields[field].present +=
						state->stack[depth].type == MXSD_T_COMPLEX ? 2 : 1;
				}
			}
		}
	}
	while (state->active_count > active_base) {
		result = finalize_identity(parser, state, state->active_count - 1u,
					   tag.start_offset);
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
	parser.global_elements = schema->global_elements;
	parser.global_element_count = schema->global_element_count;
	parser.global_attributes = schema->global_attributes;
	parser.global_attribute_count = schema->global_attribute_count;
	parser.named_types = schema->named_types;
	parser.named_type_count = schema->named_type_count;
	parser.notations = schema->notations;
	parser.notation_count = schema->notation_count;
	state->schema = schema;
	state->tables = tables;
	skip_whitespace(&parser);
	result = skip_xml_declaration(&parser);
	if (result.code == MXSD_OK) {
		skip_ignorable_markup(&parser);
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
					if (tables[constraint].rows[row].scope_offset ==
						    tables[identity->refer_index]
							    .rows[target]
							    .scope_offset &&
					    identity_rows_equal(
						    &parser, &tables[constraint].rows[row],
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
