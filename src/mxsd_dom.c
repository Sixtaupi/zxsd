/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Descriptor-driven DOM-style XML Schema validation.
 */

#include "mxsd.h"
#include "mxsd_internal.h"
#include "mxsd_xml.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** Build a structured validation result. */
mxsd_result_t mxsd_make_result(mxsd_error_t code, size_t offset, const char *path)
{
	mxsd_result_t result;

	result.code = code;
	result.offset = offset;
	result.path = path;
	result.message = mxsd_error_string(code);
	return result;
}

int mxsd_span_equals_string(text_span_t span, const char *string)
{
	size_t length = strlen(string);
	return span.length == length && memcmp(span.data, string, length) == 0;
}

void mxsd_skip_whitespace(xml_parser_t *parser)
{
	while (parser->position < parser->length &&
	       isspace((unsigned char)parser->input[parser->position])) {
		parser->position++;
	}
}

int mxsd_parse_xml_name(xml_parser_t *parser, text_span_t *name)
{
	size_t start = parser->position;
	unsigned char character;

	if (start >= parser->length) {
		return 0;
	}

	character = (unsigned char)parser->input[start];
	if (!isalpha(character) && character != '_') {
		return 0;
	}

	parser->position++;
	while (parser->position < parser->length) {
		character = (unsigned char)parser->input[parser->position];
		if (!isalnum(character) && strchr("_.:-", character) == NULL) {
			break;
		}
		parser->position++;
	}

	name->data = parser->input + start;
	name->length = parser->position - start;
	return 1;
}

static int parse_quoted_value(xml_parser_t *parser, text_span_t *value)
{
	char quote;
	size_t start;

	if (parser->position >= parser->length) {
		return 0;
	}

	quote = parser->input[parser->position];
	if (quote != '\'' && quote != '"') {
		return 0;
	}

	parser->position++;
	start = parser->position;
	while (parser->position < parser->length && parser->input[parser->position] != quote &&
	       parser->input[parser->position] != '<') {
		parser->position++;
	}

	if (parser->position >= parser->length || parser->input[parser->position] != quote) {
		return 0;
	}

	value->data = parser->input + start;
	value->length = parser->position - start;
	parser->position++;
	return 1;
}

int mxsd_parse_attribute(xml_parser_t *parser, text_span_t *name, text_span_t *value)
{
	skip_whitespace(parser);
	if (!parse_xml_name(parser, name)) {
		return 0;
	}

	skip_whitespace(parser);
	if (parser->position >= parser->length || parser->input[parser->position] != '=') {
		return 0;
	}
	parser->position++;

	skip_whitespace(parser);
	return parse_quoted_value(parser, value);
}

int mxsd_is_namespace_declaration(text_span_t name)
{
	return (name.length == 5 && memcmp(name.data, "xmlns", 5) == 0) ||
	       (name.length > 6 && memcmp(name.data, "xmlns:", 6) == 0);
}

text_span_t mxsd_namespace_prefix(text_span_t declaration_name)
{
	text_span_t prefix;

	if (declaration_name.length == 5) {
		prefix.data = declaration_name.data;
		prefix.length = 0;
	} else {
		prefix.data = declaration_name.data + 6;
		prefix.length = declaration_name.length - 6;
	}
	return prefix;
}

void mxsd_split_qualified_name(text_span_t qualified, text_span_t *prefix, text_span_t *local_name)
{
	size_t index;

	prefix->data = qualified.data;
	prefix->length = 0;
	*local_name = qualified;

	for (index = 0; index < qualified.length; index++) {
		if (qualified.data[index] == ':') {
			prefix->length = index;
			local_name->data = qualified.data + index + 1;
			local_name->length = qualified.length - index - 1;
			return;
		}
	}
}

text_span_t mxsd_resolve_namespace(const xml_parser_t *parser, text_span_t prefix, int is_attribute)
{
	static const char xml_namespace[] = "http://www.w3.org/XML/1998/namespace";
	text_span_t empty = {NULL, 0};
	size_t index;

	/* Default namespaces do not apply to unprefixed attributes. */
	if (is_attribute && prefix.length == 0) {
		return empty;
	}

	if (prefix.length == 3 && memcmp(prefix.data, "xml", 3) == 0) {
		text_span_t result = {xml_namespace, sizeof(xml_namespace) - 1};
		return result;
	}

	for (index = parser->namespace_count; index > 0; index--) {
		const namespace_binding_t *binding = &parser->namespaces[index - 1];
		if (binding->prefix.length == prefix.length &&
		    memcmp(binding->prefix.data, prefix.data, prefix.length) == 0) {
			return binding->uri;
		}
	}

	return empty;
}

int mxsd_expanded_name_equals(const xml_parser_t *parser, text_span_t qualified_name,
			      const char *expected_local_name, const char *expected_namespace,
			      int is_attribute)
{
	text_span_t prefix;
	text_span_t local_name;
	text_span_t actual_namespace;

	split_qualified_name(qualified_name, &prefix, &local_name);
	if (!span_equals_string(local_name, expected_local_name)) {
		return 0;
	}

	actual_namespace = resolve_namespace(parser, prefix, is_attribute);
	if (prefix.length > 0 && actual_namespace.length == 0) {
		return 0; /* Undeclared prefix. */
	}

	if (expected_namespace == NULL) {
		return actual_namespace.length == 0;
	}

	return span_equals_string(actual_namespace, expected_namespace);
}

int mxsd_span_equals_nullable(text_span_t span, const char *text)
{
	return text == NULL ? span.length == 0 : span_equals_string(span, text);
}

static int wildcard_accepts(const xml_parser_t *parser, const mxsd_wildcard_t *wildcard,
			    text_span_t qualified_name, int is_attribute)
{
	text_span_t prefix;
	text_span_t local;
	text_span_t uri;
	const char *cursor = wildcard->namespace_constraint;

	split_qualified_name(qualified_name, &prefix, &local);
	(void)local;
	uri = resolve_namespace(parser, prefix, is_attribute);
	if (prefix.length > 0 && uri.length == 0) {
		return 0;
	}
	if (strcmp(cursor, "##any") == 0) {
		return 1;
	}
	if (strcmp(cursor, "##other") == 0) {
		return uri.length > 0 && !span_equals_nullable(uri, wildcard->target_namespace);
	}
	while (*cursor) {
		const char *end = strchr(cursor, ' ');
		size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
		if ((length == 7u && memcmp(cursor, "##local", 7u) == 0 && uri.length == 0) ||
		    (length == 17u && memcmp(cursor, "##targetNamespace", 17u) == 0 &&
		     span_equals_nullable(uri, wildcard->target_namespace)) ||
		    (uri.length == length && memcmp(uri.data, cursor, length) == 0)) {
			return 1;
		}
		if (!end) {
			break;
		}
		cursor = end + 1;
	}
	return 0;
}

/** Find an attribute descriptor by expanded XML name. */
static const mxsd_attribute_t *find_attribute(const xml_parser_t *parser,
					      const mxsd_complex_t *complex, text_span_t name)
{
	size_t index;

	for (index = 0; index < complex->attribute_count; index++) {
		const mxsd_attribute_t *attribute = &complex->attributes[index];
		if (expanded_name_equals(parser, name, attribute->name, attribute->namespace_uri,
					 1)) {
			return attribute;
		}
	}
	return NULL;
}

static const mxsd_global_attribute_t *find_global_attribute(const xml_parser_t *parser,
							    text_span_t name)
{
	size_t index;
	for (index = 0; index < parser->global_attribute_count; index++) {
		const mxsd_global_attribute_t *attribute = &parser->global_attributes[index];
		if (expanded_name_equals(parser, name, attribute->name, attribute->namespace_uri,
					 1)) {
			return attribute;
		}
	}
	return NULL;
}

mxsd_result_t mxsd_parse_start_tag(xml_parser_t *parser, const char *path, start_tag_t *tag)
{
	text_span_t attribute_name;
	text_span_t attribute_value;

	memset(tag, 0, sizeof(*tag));
	tag->start_offset = parser->position;
	tag->namespace_base = parser->namespace_count;

	if (parser->position >= parser->length || parser->input[parser->position] != '<') {
		return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
	}
	parser->position++;

	if (!parse_xml_name(parser, &tag->name)) {
		return make_result(MXSD_ERR_MALFORMED_XML, tag->start_offset, path);
	}
	tag->attributes_offset = parser->position;

	for (;;) {
		skip_whitespace(parser);

		if (parser->position + 1 < parser->length &&
		    parser->input[parser->position] == '/' &&
		    parser->input[parser->position + 1] == '>') {
			parser->position += 2;
			tag->is_empty = 1;
			break;
		}
		if (parser->position < parser->length && parser->input[parser->position] == '>') {
			parser->position++;
			break;
		}

		if (!parse_attribute(parser, &attribute_name, &attribute_value)) {
			return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
		}

		if (is_namespace_declaration(attribute_name)) {
			namespace_binding_t *binding;
			if (parser->namespace_count >= MXSD_MAX_NAMESPACES) {
				return make_result(MXSD_ERR_EXCESSIVE_NESTING, parser->position,
						   path);
			}
			binding = &parser->namespaces[parser->namespace_count++];
			binding->prefix = namespace_prefix(attribute_name);
			binding->uri = attribute_value;
		} else {
			tag->attribute_count++;
			if (tag->attribute_count > MXSD_MAX_ATTRIBUTES) {
				return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
			}
		}
	}

	tag->content_offset = parser->position;
	return make_result(MXSD_OK, parser->position, NULL);
}

int mxsd_at_end_of_start_tag(const xml_parser_t *parser)
{
	if (parser->position >= parser->length) {
		return 1;
	}
	if (parser->input[parser->position] == '>') {
		return 1;
	}
	return parser->position + 1 < parser->length && parser->input[parser->position] == '/' &&
	       parser->input[parser->position + 1] == '>';
}

static mxsd_result_t validate_attributes(xml_parser_t *parser, const start_tag_t *tag,
					 const char *path, mxsd_type_t type,
					 const mxsd_complex_t *complex,
					 unsigned char seen[MXSD_MAX_ATTRIBUTES])
{
	size_t validated_count = 0;

	parser->position = tag->attributes_offset;
	while (validated_count < tag->attribute_count) {
		text_span_t name;
		text_span_t value;
		const mxsd_attribute_t *attribute;
		size_t attribute_index;

		skip_whitespace(parser);
		if (at_end_of_start_tag(parser) || !parse_attribute(parser, &name, &value)) {
			return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
		}
		if (is_namespace_declaration(name)) {
			continue;
		}
		validated_count++;

		if (expanded_name_equals(parser, name, "nil",
					 "http://www.w3.org/2001/XMLSchema-instance", 1) ||
		    expanded_name_equals(parser, name, "type",
					 "http://www.w3.org/2001/XMLSchema-instance", 1)) {
			continue;
		}

		if (type != MXSD_T_COMPLEX ||
		    (attribute = find_attribute(parser, complex, name)) == NULL) {
			if (type == MXSD_T_COMPLEX && complex->any_attribute != NULL &&
			    wildcard_accepts(parser, complex->any_attribute, name, 1)) {
				const mxsd_global_attribute_t *global =
					find_global_attribute(parser, name);
				if (complex->any_attribute->process_contents == MXSD_PROCESS_SKIP) {
					continue;
				}
				if (global != NULL) {
					if (!validate_simple_value(parser, global->type,
								   global->simple_type, value)) {
						return make_result(MXSD_ERR_INVALID_VALUE,
								   parser->position, path);
					}
					continue;
				}
				if (complex->any_attribute->process_contents == MXSD_PROCESS_LAX) {
					continue;
				}
			}
			return make_result(MXSD_ERR_UNEXPECTED_ATTRIBUTE, parser->position, path);
		}

		attribute_index = (size_t)(attribute - complex->attributes);
		if (attribute_index >= MXSD_MAX_ATTRIBUTES || seen[attribute_index]) {
			return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
		}
		seen[attribute_index] = 1;

		if (!validate_simple_value(parser, attribute->type, attribute->simple_type,
					   value)) {
			return make_result(MXSD_ERR_INVALID_VALUE, parser->position, path);
		}
		if (attribute->fixed_value != NULL &&
		    !simple_values_equal(parser, attribute->type, attribute->simple_type, value,
					 attribute->fixed_value)) {
			return make_result(MXSD_ERR_INVALID_VALUE, parser->position, path);
		}
	}

	parser->position = tag->content_offset;
	return make_result(MXSD_OK, parser->position, NULL);
}

static mxsd_result_t check_required_attributes(const mxsd_complex_t *complex,
					       const unsigned char seen[MXSD_MAX_ATTRIBUTES],
					       size_t offset, const char *path)
{
	size_t index;

	for (index = 0; index < complex->attribute_count; index++) {
		if (complex->attributes[index].required &&
		    (index >= MXSD_MAX_ATTRIBUTES || !seen[index])) {
			return make_result(MXSD_ERR_MISSING_ATTRIBUTE, offset, path);
		}
	}
	return make_result(MXSD_OK, offset, NULL);
}

static mxsd_result_t validate_element(xml_parser_t *parser, const char *expected_name,
				      const char *expected_namespace, mxsd_type_t type,
				      const mxsd_complex_t *complex, const mxsd_simple_t *simple,
				      const char *default_value, const char *fixed_value,
				      int nillable, int abstract_element, unsigned block_methods);

static mxsd_result_t parse_end_tag(xml_parser_t *parser, const char *expected_name,
				   const char *expected_namespace);

static mxsd_result_t peek_child_name(xml_parser_t *parser, const char *path, text_span_t *name)
{
	size_t saved_position = parser->position;

	if (parser->position >= parser->length || parser->input[parser->position] != '<') {
		return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
	}
	parser->position++;
	if (!parse_xml_name(parser, name)) {
		parser->position = saved_position;
		return make_result(MXSD_ERR_MALFORMED_XML, saved_position, path);
	}
	parser->position = saved_position;
	return make_result(MXSD_OK, saved_position, NULL);
}

int mxsd_at_end_element(const xml_parser_t *parser)
{
	return parser->position + 1u < parser->length && parser->input[parser->position] == '<' &&
	       parser->input[parser->position + 1u] == '/';
}

static const mxsd_global_element_t *find_global_element(const xml_parser_t *parser,
							text_span_t name)
{
	size_t index;
	for (index = 0; index < parser->global_element_count; index++) {
		const mxsd_global_element_t *element = &parser->global_elements[index];
		if (expanded_name_equals(parser, name, element->name, element->namespace_uri, 0)) {
			return element;
		}
	}
	return NULL;
}

static int global_substitutes_for(const xml_parser_t *parser,
				  const mxsd_global_element_t *candidate,
				  const mxsd_element_t *head)
{
	size_t depth = 0;
	if ((head->block_methods & MXSD_DERIVATION_SUBSTITUTION) != 0u) {
		return 0;
	}
	while (candidate != NULL && candidate->substitution_head_name != NULL &&
	       depth++ < parser->global_element_count) {
		if (strcmp(candidate->substitution_head_name, head->name) == 0 &&
		    ((candidate->substitution_head_namespace == NULL &&
		      head->namespace_uri == NULL) ||
		     (candidate->substitution_head_namespace != NULL &&
		      head->namespace_uri != NULL &&
		      strcmp(candidate->substitution_head_namespace, head->namespace_uri) == 0))) {
			return 1;
		}
		{
			size_t index;
			const mxsd_global_element_t *next = NULL;
			for (index = 0; index < parser->global_element_count; index++) {
				const mxsd_global_element_t *item = &parser->global_elements[index];
				if (strcmp(item->name, candidate->substitution_head_name) == 0 &&
				    ((item->namespace_uri == NULL &&
				      candidate->substitution_head_namespace == NULL) ||
				     (item->namespace_uri &&
				      candidate->substitution_head_namespace &&
				      strcmp(item->namespace_uri,
					     candidate->substitution_head_namespace) == 0))) {
					next = item;
					break;
				}
			}
			candidate = next;
		}
	}
	return 0;
}

static mxsd_result_t skip_any_element(xml_parser_t *parser, const char *path)
{
	start_tag_t tag;
	mxsd_result_t result = parse_start_tag(parser, path, &tag);
	if (result.code != MXSD_OK) {
		return result;
	}
	if (!tag.is_empty) {
		for (;;) {
			skip_whitespace(parser);
			if (at_end_element(parser)) {
				break;
			}
			if (parser->position >= parser->length) {
				return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
			}
			if (parser->input[parser->position] == '<') {
				result = skip_any_element(parser, path);
				if (result.code != MXSD_OK) {
					return result;
				}
			} else {
				while (parser->position < parser->length &&
				       parser->input[parser->position] != '<') {
					parser->position++;
				}
			}
		}
		{
			text_span_t closing;
			if (parser->position + 2u > parser->length ||
			    parser->input[parser->position] != '<' ||
			    parser->input[parser->position + 1u] != '/') {
				return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
			}
			parser->position += 2u;
			if (!parse_xml_name(parser, &closing) ||
			    closing.length != tag.name.length ||
			    memcmp(closing.data, tag.name.data, closing.length) != 0) {
				return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
			}
			skip_whitespace(parser);
			if (parser->position >= parser->length ||
			    parser->input[parser->position++] != '>') {
				return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
			}
		}
	}
	parser->namespace_count = tag.namespace_base;
	return make_result(MXSD_OK, parser->position, NULL);
}

static int particle_can_start(xml_parser_t *parser, const mxsd_particle_t *particle)
{
	text_span_t name;
	size_t index;
	if (at_end_element(parser) || parser->position >= parser->length ||
	    parser->input[parser->position] != '<' ||
	    peek_child_name(parser, NULL, &name).code != MXSD_OK) {
		return 0;
	}
	if (particle->kind == MXSD_PARTICLE_ELEMENT) {
		if (expanded_name_equals(parser, name, particle->element->name,
					 particle->element->namespace_uri, 0)) {
			return 1;
		}
		return global_substitutes_for(parser, find_global_element(parser, name),
					      particle->element);
	}
	if (particle->kind == MXSD_PARTICLE_ANY) {
		return wildcard_accepts(parser, particle->wildcard, name, 0);
	}
	for (index = 0; index < particle->child_count; index++) {
		if (particle_can_start(parser, &particle->children[index])) {
			return 1;
		}
		if (particle->kind == MXSD_PARTICLE_SEQUENCE &&
		    particle->children[index].min_occurs > 0) {
			break;
		}
	}
	return 0;
}

static mxsd_result_t consume_particle(xml_parser_t *parser, const mxsd_particle_t *particle,
				      const char *path, unsigned long *consumed);

static mxsd_result_t match_particle_once(xml_parser_t *parser, const mxsd_particle_t *particle,
					 const char *path, int *matched)
{
	size_t index;
	*matched = 0;
	if (particle->kind == MXSD_PARTICLE_ELEMENT) {
		mxsd_result_t result;
		text_span_t actual_name;
		const mxsd_global_element_t *substitute = NULL;
		if (!particle_can_start(parser, particle)) {
			return make_result(MXSD_OK, parser->position, NULL);
		}
		result = peek_child_name(parser, path, &actual_name);
		if (result.code != MXSD_OK) {
			return result;
		}
		if (!expanded_name_equals(parser, actual_name, particle->element->name,
					  particle->element->namespace_uri, 0)) {
			substitute = find_global_element(parser, actual_name);
		}
		if (substitute != NULL) {
			result = validate_element(
				parser, substitute->name, substitute->namespace_uri,
				substitute->type, substitute->complex_type, substitute->simple_type,
				substitute->default_value, substitute->fixed_value,
				substitute->nillable, substitute->abstract_element,
				substitute->block_methods);
		} else {
			result = validate_element(
				parser, particle->element->name, particle->element->namespace_uri,
				particle->element->type, particle->element->complex_type,
				particle->element->simple_type, particle->element->default_value,
				particle->element->fixed_value, particle->element->nillable,
				particle->element->abstract_element,
				particle->element->block_methods);
		}
		if (result.code == MXSD_OK) {
			*matched = 1;
		}
		return result;
	}
	if (particle->kind == MXSD_PARTICLE_ANY) {
		text_span_t name;
		const mxsd_global_element_t *declaration;
		mxsd_result_t result;
		if (!particle_can_start(parser, particle)) {
			return make_result(MXSD_OK, parser->position, NULL);
		}
		result = peek_child_name(parser, path, &name);
		if (result.code != MXSD_OK) {
			return result;
		}
		declaration = find_global_element(parser, name);
		if (particle->wildcard->process_contents == MXSD_PROCESS_STRICT &&
		    declaration == NULL) {
			return make_result(MXSD_ERR_UNEXPECTED_ELEMENT, parser->position, path);
		}
		if (declaration != NULL &&
		    particle->wildcard->process_contents != MXSD_PROCESS_SKIP) {
			result = validate_element(
				parser, declaration->name, declaration->namespace_uri,
				declaration->type, declaration->complex_type,
				declaration->simple_type, declaration->default_value,
				declaration->fixed_value, declaration->nillable,
				declaration->abstract_element, declaration->block_methods);
		} else {
			result = skip_any_element(parser, path);
		}
		if (result.code == MXSD_OK) {
			*matched = 1;
		}
		return result;
	}
	if (particle->kind == MXSD_PARTICLE_SEQUENCE) {
		int any = 0;
		for (index = 0; index < particle->child_count; index++) {
			unsigned long count = 0;
			mxsd_result_t result =
				consume_particle(parser, &particle->children[index], path, &count);
			if (result.code != MXSD_OK) {
				return result;
			}
			if (count) {
				any = 1;
			}
		}
		*matched = any;
		return make_result(MXSD_OK, parser->position, NULL);
	}
	if (particle->kind == MXSD_PARTICLE_CHOICE) {
		for (index = 0; index < particle->child_count; index++) {
			if (particle_can_start(parser, &particle->children[index])) {
				unsigned long count = 0;
				mxsd_result_t result = consume_particle(
					parser, &particle->children[index], path, &count);
				if (result.code == MXSD_OK && count) {
					*matched = 1;
				}
				return result;
			}
		}
		return make_result(MXSD_OK, parser->position, NULL);
	}
	{
		unsigned long counts[MXSD_MAX_PARTICLES] = {0};
		int progress;
		if (particle->child_count > MXSD_MAX_PARTICLES) {
			return make_result(MXSD_ERR_ARGUMENT, parser->position, path);
		}
		do {
			progress = 0;
			for (index = 0; index < particle->child_count; index++) {
				const mxsd_particle_t *child = &particle->children[index];
				if (counts[index] < child->max_occurs &&
				    particle_can_start(parser, child)) {
					int one = 0;
					mxsd_result_t result =
						match_particle_once(parser, child, path, &one);
					if (result.code != MXSD_OK) {
						return result;
					}
					if (one) {
						counts[index]++;
						progress = 1;
						*matched = 1;
						break;
					}
				}
			}
		} while (progress);
		for (index = 0; index < particle->child_count; index++) {
			if (counts[index] < particle->children[index].min_occurs) {
				return make_result(MXSD_ERR_MISSING_ELEMENT, parser->position,
						   path);
			}
		}
	}
	return make_result(MXSD_OK, parser->position, NULL);
}

static mxsd_result_t consume_particle(xml_parser_t *parser, const mxsd_particle_t *particle,
				      const char *path, unsigned long *consumed)
{
	*consumed = 0;
	while (*consumed < particle->max_occurs && particle_can_start(parser, particle)) {
		int matched = 0;
		mxsd_result_t result = match_particle_once(parser, particle, path, &matched);
		if (result.code != MXSD_OK) {
			return result;
		}
		if (!matched) {
			break;
		}
		(*consumed)++;
	}
	if (*consumed == particle->max_occurs && particle_can_start(parser, particle)) {
		return make_result(MXSD_ERR_OCCURRENCE, parser->position, path);
	}
	if (*consumed < particle->min_occurs) {
		mxsd_error_t code =
			at_end_element(parser) ? MXSD_ERR_MISSING_ELEMENT : MXSD_ERR_ELEMENT_ORDER;
		return make_result(code, parser->position, path);
	}
	return make_result(MXSD_OK, parser->position, NULL);
}

static mxsd_result_t validate_particle_content(xml_parser_t *parser,
					       const mxsd_particle_t *particle, const char *path)
{
	unsigned long count;
	mxsd_result_t result;
	skip_whitespace(parser);
	result = consume_particle(parser, particle, path, &count);
	if (result.code != MXSD_OK) {
		return result;
	}
	skip_whitespace(parser);
	if (!at_end_element(parser)) {
		return make_result(MXSD_ERR_UNEXPECTED_ELEMENT, parser->position, path);
	}
	return make_result(MXSD_OK, parser->position, NULL);
}

static int particle_accepts_empty(const mxsd_particle_t *particle)
{
	size_t index;
	if (particle->min_occurs == 0) {
		return 1;
	}
	if (particle->kind == MXSD_PARTICLE_ELEMENT || particle->kind == MXSD_PARTICLE_ANY) {
		return 0;
	}
	if (particle->kind == MXSD_PARTICLE_CHOICE) {
		for (index = 0; index < particle->child_count; index++) {
			if (particle_accepts_empty(&particle->children[index])) {
				return 1;
			}
		}
		return 0;
	}
	for (index = 0; index < particle->child_count; index++) {
		if (!particle_accepts_empty(&particle->children[index])) {
			return 0;
		}
	}
	return 1;
}

static size_t find_unordered_child(const xml_parser_t *parser, const mxsd_complex_t *complex,
				   text_span_t child_name)
{
	size_t index;

	for (index = 0; index < complex->element_count; index++) {
		const mxsd_element_t *element = &complex->elements[index];
		if (expanded_name_equals(parser, child_name, element->name, element->namespace_uri,
					 0)) {
			return index;
		}
	}
	return complex->element_count;
}

static mxsd_result_t find_sequence_child(const xml_parser_t *parser, const mxsd_complex_t *complex,
					 text_span_t child_name, size_t offset, size_t *index,
					 unsigned long *occurrences)
{
	if (*index >= complex->element_count) {
		return make_result(MXSD_ERR_UNEXPECTED_ELEMENT, offset, NULL);
	}

	while (*index < complex->element_count) {
		const mxsd_element_t *element = &complex->elements[*index];
		if (expanded_name_equals(parser, child_name, element->name, element->namespace_uri,
					 0)) {
			return make_result(MXSD_OK, offset, NULL);
		}
		if (*occurrences < element->min_occurs) {
			return make_result(MXSD_ERR_ELEMENT_ORDER, offset, element->name);
		}
		(*index)++;
		*occurrences = 0;
	}

	return make_result(MXSD_ERR_UNEXPECTED_ELEMENT, offset, NULL);
}

static mxsd_result_t check_completed_content(
	const mxsd_complex_t *complex, size_t index, unsigned long sequence_occurrences,
	const unsigned long child_occurrences[MXSD_MAX_PARTICLES], size_t offset, const char *path)
{
	size_t child;

	if (complex->model == MXSD_SEQUENCE) {
		while (index < complex->element_count) {
			const mxsd_element_t *element = &complex->elements[index];
			if (sequence_occurrences < element->min_occurs) {
				return make_result(MXSD_ERR_MISSING_ELEMENT, offset, element->name);
			}
			index++;
			sequence_occurrences = 0;
		}
		return make_result(MXSD_OK, offset, NULL);
	}

	if (complex->model == MXSD_ALL) {
		for (child = 0; child < complex->element_count; child++) {
			if (child_occurrences[child] < complex->elements[child].min_occurs) {
				return make_result(MXSD_ERR_MISSING_ELEMENT, offset,
						   complex->elements[child].name);
			}
		}
		return make_result(MXSD_OK, offset, NULL);
	}

	/* A choice requires at least min_occurs selected alternatives. */
	{
		unsigned long total = 0;
		for (child = 0; child < complex->element_count; child++) {
			total += child_occurrences[child];
		}
		if (total < complex->min_occurs) {
			return make_result(MXSD_ERR_MISSING_ELEMENT, offset, path);
		}
	}
	return make_result(MXSD_OK, offset, NULL);
}

static mxsd_result_t validate_complex_content(xml_parser_t *parser, const mxsd_complex_t *complex,
					      const char *path)
{
	unsigned long child_occurrences[MXSD_MAX_PARTICLES] = {0};
	unsigned long sequence_occurrences = 0;
	size_t current_index = 0;

	if (complex->element_count > MXSD_MAX_PARTICLES) {
		return make_result(MXSD_ERR_ARGUMENT, parser->position, path);
	}

	for (;;) {
		text_span_t child_name;
		mxsd_result_t result;
		const mxsd_element_t *element;
		unsigned long occurrences;
		size_t child_offset;

		skip_whitespace(parser);
		if (parser->position + 1 < parser->length &&
		    parser->input[parser->position] == '<' &&
		    parser->input[parser->position + 1] == '/') {
			break;
		}
		if (parser->position >= parser->length) {
			return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
		}
		if (parser->input[parser->position] != '<') {
			return make_result(MXSD_ERR_TEXT_NOT_ALLOWED, parser->position, path);
		}

		child_offset = parser->position;
		result = peek_child_name(parser, path, &child_name);
		if (result.code != MXSD_OK) {
			return result;
		}

		if (complex->model == MXSD_SEQUENCE) {
			result = find_sequence_child(parser, complex, child_name, child_offset,
						     &current_index, &sequence_occurrences);
			if (result.code != MXSD_OK) {
				if (result.code == MXSD_ERR_UNEXPECTED_ELEMENT &&
				    result.path == NULL) {
					result.path = path;
				}
				return result;
			}
			occurrences = sequence_occurrences;
		} else {
			current_index = find_unordered_child(parser, complex, child_name);
			if (current_index >= complex->element_count) {
				return make_result(MXSD_ERR_UNEXPECTED_ELEMENT, child_offset, path);
			}
			occurrences = child_occurrences[current_index];
		}

		element = &complex->elements[current_index];
		if (occurrences == element->max_occurs) {
			return make_result(MXSD_ERR_OCCURRENCE, child_offset, element->name);
		}

		result = validate_element(parser, element->name, element->namespace_uri,
					  element->type, element->complex_type,
					  element->simple_type, element->default_value,
					  element->fixed_value, element->nillable,
					  element->abstract_element, element->block_methods);
		if (result.code != MXSD_OK) {
			return result;
		}

		if (complex->model == MXSD_SEQUENCE) {
			sequence_occurrences++;
		} else {
			child_occurrences[current_index]++;
		}
	}

	return check_completed_content(complex, current_index, sequence_occurrences,
				       child_occurrences, parser->position, path);
}

static mxsd_result_t validate_empty_complex(const mxsd_complex_t *complex, size_t offset,
					    const char *path)
{
	size_t index;

	if (complex->model == MXSD_CHOICE && complex->min_occurs > 0) {
		return make_result(MXSD_ERR_MISSING_ELEMENT, offset, path);
	}
	if (complex->model == MXSD_CHOICE) {
		return make_result(MXSD_OK, offset, NULL);
	}

	for (index = 0; index < complex->element_count; index++) {
		if (complex->elements[index].min_occurs > 0) {
			return make_result(MXSD_ERR_MISSING_ELEMENT, offset,
					   complex->elements[index].name);
		}
	}
	return make_result(MXSD_OK, offset, NULL);
}

static mxsd_result_t validate_simple_content(xml_parser_t *parser, const char *path,
					     mxsd_type_t type, const mxsd_simple_t *simple,
					     int is_empty, size_t element_offset,
					     const char *default_value, const char *fixed_value)
{
	text_span_t value = {NULL, 0};

	if (!is_empty) {
		size_t value_offset = parser->position;
		while (parser->position < parser->length &&
		       parser->input[parser->position] != '<') {
			parser->position++;
		}
		value.data = parser->input + value_offset;
		value.length = parser->position - value_offset;
		if (value.length == 0 && (fixed_value != NULL || default_value != NULL)) {
			const char *effective = fixed_value != NULL ? fixed_value : default_value;
			value.data = effective;
			value.length = strlen(effective);
		}
		if (!validate_simple_value(parser, type, simple, value) ||
		    (fixed_value != NULL &&
		     !simple_values_equal(parser, type, simple, value, fixed_value))) {
			return make_result(MXSD_ERR_INVALID_VALUE, value_offset, path);
		}
	} else {
		const char *effective = fixed_value != NULL ? fixed_value : default_value;
		if (effective != NULL) {
			value.data = effective;
			value.length = strlen(effective);
		}
		if (!validate_simple_value(parser, type, simple, value)) {
			return make_result(MXSD_ERR_INVALID_VALUE, element_offset, path);
		}
	}

	return make_result(MXSD_OK, parser->position, NULL);
}

static mxsd_result_t parse_end_tag(xml_parser_t *parser, const char *expected_name,
				   const char *expected_namespace)
{
	text_span_t name;

	if (parser->position + 2 > parser->length || parser->input[parser->position] != '<' ||
	    parser->input[parser->position + 1] != '/') {
		return make_result(MXSD_ERR_MALFORMED_XML, parser->position, expected_name);
	}
	parser->position += 2;

	if (!parse_xml_name(parser, &name)) {
		return make_result(MXSD_ERR_MALFORMED_XML, parser->position, expected_name);
	}
	skip_whitespace(parser);

	if (parser->position >= parser->length || parser->input[parser->position] != '>' ||
	    !expanded_name_equals(parser, name, expected_name, expected_namespace, 0)) {
		return make_result(MXSD_ERR_MALFORMED_XML, parser->position, expected_name);
	}
	parser->position++;
	return make_result(MXSD_OK, parser->position, NULL);
}

/** xsi:nil and xsi:type values found on one start tag. */
typedef struct {
	int nil_present;
	int nil_value;
	int type_present;
	text_span_t type_name;
} instance_options_t;

static mxsd_result_t read_instance_options(xml_parser_t *parser, const start_tag_t *tag,
					   const char *path, instance_options_t *options)
{
	size_t saved = parser->position;
	size_t parsed = 0;
	memset(options, 0, sizeof(*options));
	parser->position = tag->attributes_offset;
	while (parsed < tag->attribute_count) {
		text_span_t name, value;
		skip_whitespace(parser);
		if (!parse_attribute(parser, &name, &value)) {
			parser->position = saved;
			return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
		}
		if (is_namespace_declaration(name)) {
			continue;
		}
		parsed++;
		if (expanded_name_equals(parser, name, "nil",
					 "http://www.w3.org/2001/XMLSchema-instance", 1)) {
			if (options->nil_present || !validate_boolean(value)) {
				parser->position = saved;
				return make_result(MXSD_ERR_INVALID_VALUE, tag->start_offset, path);
			}
			options->nil_present = 1;
			options->nil_value =
				span_equals_string(value, "true") || span_equals_string(value, "1");
		} else if (expanded_name_equals(parser, name, "type",
						"http://www.w3.org/2001/XMLSchema-instance", 1)) {
			if (options->type_present) {
				parser->position = saved;
				return make_result(MXSD_ERR_MALFORMED_XML, tag->start_offset, path);
			}
			options->type_present = 1;
			options->type_name = value;
		}
	}
	parser->position = saved;
	return make_result(MXSD_OK, saved, NULL);
}

static const mxsd_named_type_t *find_named_type(const xml_parser_t *parser,
						text_span_t lexical_qname)
{
	text_span_t prefix, local, uri;
	size_t index;
	split_qualified_name(lexical_qname, &prefix, &local);
	uri = resolve_namespace(parser, prefix, 0);
	if (prefix.length && uri.length == 0) {
		return NULL;
	}
	for (index = 0; index < parser->named_type_count; index++) {
		const mxsd_named_type_t *candidate = &parser->named_types[index];
		if (span_equals_string(local, candidate->name) &&
		    span_equals_nullable(uri, candidate->namespace_uri)) {
			return candidate;
		}
	}
	return NULL;
}

int mxsd_complex_derives_from(const mxsd_complex_t *candidate, const mxsd_complex_t *base,
			      unsigned blocked)
{
	while (candidate != NULL && candidate != base) {
		if ((candidate->derivation_method & blocked) != 0u) {
			return 0;
		}
		candidate = candidate->base_definition;
	}
	return candidate == base;
}

int mxsd_simple_derives_from(const mxsd_simple_t *candidate, const mxsd_simple_t *base,
			     unsigned blocked)
{
	if (base == NULL) {
		return candidate != NULL && (candidate->derivation_method & blocked) == 0u;
	}
	while (candidate != NULL && candidate != base) {
		if ((candidate->derivation_method & blocked) != 0u) {
			return 0;
		}
		candidate = candidate->base_definition;
	}
	return candidate == base;
}

static mxsd_result_t validate_element(xml_parser_t *parser, const char *expected_name,
				      const char *expected_namespace, mxsd_type_t type,
				      const mxsd_complex_t *complex, const mxsd_simple_t *simple,
				      const char *default_value, const char *fixed_value,
				      int nillable, int abstract_element, unsigned block_methods)
{
	start_tag_t tag;
	unsigned char seen_attributes[MXSD_MAX_ATTRIBUTES] = {0};
	mxsd_result_t result;
	instance_options_t instance;

	parser->depth++;
	if (parser->depth > MXSD_MAX_DEPTH) {
		return make_result(MXSD_ERR_EXCESSIVE_NESTING, parser->position, expected_name);
	}

	result = parse_start_tag(parser, expected_name, &tag);
	if (result.code != MXSD_OK) {
		return result;
	}

	if (!expanded_name_equals(parser, tag.name, expected_name, expected_namespace, 0)) {
		mxsd_error_t code =
			parser->depth == 1 ? MXSD_ERR_WRONG_ROOT : MXSD_ERR_UNEXPECTED_ELEMENT;
		return make_result(code, tag.start_offset, expected_name);
	}

	if (abstract_element) {
		return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset, expected_name);
	}

	result = read_instance_options(parser, &tag, expected_name, &instance);
	if (result.code != MXSD_OK) {
		return result;
	}
	if (instance.type_present) {
		const mxsd_named_type_t *dynamic = find_named_type(parser, instance.type_name);
		if (dynamic == NULL) {
			return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset, expected_name);
		}
		if (dynamic->complex_type != NULL) {
			if (type != MXSD_T_COMPLEX ||
			    !complex_derives_from(dynamic->complex_type, complex, block_methods) ||
			    dynamic->complex_type->abstract_type) {
				return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset,
						   expected_name);
			}
			type = MXSD_T_COMPLEX;
			complex = dynamic->complex_type;
			simple = NULL;
		} else {
			if (type == MXSD_T_COMPLEX ||
			    !simple_derives_from(dynamic->simple_type, simple, block_methods) ||
			    (simple == NULL && dynamic->type != type)) {
				return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset,
						   expected_name);
			}
			type = dynamic->type;
			simple = dynamic->simple_type;
			complex = NULL;
		}
	}

	result = validate_attributes(parser, &tag, expected_name, type, complex, seen_attributes);
	if (result.code != MXSD_OK) {
		return result;
	}
	if (type == MXSD_T_COMPLEX) {
		if (complex == NULL || complex->abstract_type) {
			return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset, expected_name);
		}
		result = check_required_attributes(complex, seen_attributes, tag.start_offset,
						   expected_name);
		if (result.code != MXSD_OK) {
			return result;
		}
	}

	if (instance.nil_present && instance.nil_value) {
		if (!nillable || fixed_value != NULL) {
			return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset, expected_name);
		}
		if (!tag.is_empty) {
			skip_whitespace(parser);
			if (!at_end_element(parser)) {
				return make_result(MXSD_ERR_INVALID_VALUE, parser->position,
						   expected_name);
			}
		}
		result = make_result(MXSD_OK, parser->position, NULL);
	} else if (type == MXSD_T_COMPLEX) {
		if (complex->has_simple_content) {
			result = validate_simple_content(
				parser, expected_name, complex->simple_content_type,
				complex->simple_content, tag.is_empty, tag.start_offset,
				default_value, fixed_value);
		} else if (complex->particle != NULL) {
			if (tag.is_empty) {
				result = particle_accepts_empty(complex->particle)
						 ? make_result(MXSD_OK, tag.start_offset, NULL)
						 : make_result(MXSD_ERR_MISSING_ELEMENT,
							       tag.start_offset, expected_name);
			} else {
				result = validate_particle_content(parser, complex->particle,
								   expected_name);
			}
		} else {
			result = tag.is_empty
					 ? validate_empty_complex(complex, tag.start_offset,
								  expected_name)
					 : validate_complex_content(parser, complex, expected_name);
		}
	} else {
		result = validate_simple_content(parser, expected_name, type, simple, tag.is_empty,
						 tag.start_offset, default_value, fixed_value);
	}

	if (result.code != MXSD_OK) {
		return result;
	}

	if (!tag.is_empty) {
		result = parse_end_tag(parser, expected_name, expected_namespace);
		if (result.code != MXSD_OK) {
			return result;
		}
	}

	parser->namespace_count = tag.namespace_base;
	parser->depth--;
	return make_result(MXSD_OK, parser->position, NULL);
}

mxsd_result_t mxsd_skip_xml_declaration(xml_parser_t *parser)
{
	const char *end;

	if (parser->position + 5 > parser->length ||
	    memcmp(parser->input + parser->position, "<?xml", 5) != 0) {
		return make_result(MXSD_OK, parser->position, NULL);
	}

	end = strstr(parser->input + parser->position, "?>");
	if (end == NULL) {
		return make_result(MXSD_ERR_MALFORMED_XML, parser->position, NULL);
	}

	parser->position = (size_t)(end - parser->input) + 2;
	skip_whitespace(parser);
	return make_result(MXSD_OK, parser->position, NULL);
}

mxsd_result_t mxsd_dom_validate(const mxsd_schema_t *schema, const char *xml)
{
	xml_parser_t parser;
	mxsd_result_t result;
	size_t input_length;

	if (schema == NULL || schema->root_name == NULL || xml == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	input_length = strlen(xml);
	if (input_length > MXSD_MAX_INPUT) {
		return make_result(MXSD_ERR_INPUT_TOO_LARGE, 0, NULL);
	}
	if (!mxsd_xml_well_formed(xml)) {
		return make_result(MXSD_ERR_MALFORMED_XML, 0, NULL);
	}

	memset(&parser, 0, sizeof(parser));
	parser.input = xml;
	parser.length = input_length;
	parser.global_elements = schema->global_elements;
	parser.global_element_count = schema->global_element_count;
	parser.global_attributes = schema->global_attributes;
	parser.global_attribute_count = schema->global_attribute_count;
	parser.named_types = schema->named_types;
	parser.named_type_count = schema->named_type_count;
	skip_whitespace(&parser);

	result = skip_xml_declaration(&parser);
	if (result.code != MXSD_OK) {
		return result;
	}

	result = validate_element(&parser, schema->root_name, schema->root_namespace_uri,
				  schema->root_type, schema->root_complex, schema->root_simple,
				  schema->default_value, schema->fixed_value, schema->nillable,
				  schema->abstract_element, schema->block_methods);
	if (result.code != MXSD_OK) {
		return result;
	}

	skip_whitespace(&parser);
	if (parser.position != parser.length) {
		return make_result(MXSD_ERR_MALFORMED_XML, parser.position, NULL);
	}
	return make_result(MXSD_OK, parser.position, NULL);
}
