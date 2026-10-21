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
#include "mxsd_regex.h"
#include "mxsd_xml.h"

#include <ctype.h>
#include <limits.h>
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

void mxsd_skip_ignorable_markup(xml_parser_t *parser)
{
	for (;;) {
		const char *end;

		skip_whitespace(parser);
		if (parser->position + 4u <= parser->length &&
		    memcmp(parser->input + parser->position, "<!--", 4u) == 0) {
			end = strstr(parser->input + parser->position + 4u, "-->");
			if (end == NULL) {
				parser->position = parser->length;
				return;
			}
			parser->position = (size_t)(end - parser->input) + 3u;
			continue;
		}
		if (parser->position + 2u <= parser->length &&
		    memcmp(parser->input + parser->position, "<?", 2u) == 0) {
			end = strstr(parser->input + parser->position + 2u, "?>");
			if (end == NULL) {
				parser->position = parser->length;
				return;
			}
			parser->position = (size_t)(end - parser->input) + 2u;
			continue;
		}
		return;
	}
}

int mxsd_parse_xml_name(xml_parser_t *parser, text_span_t *name)
{
	size_t start = parser->position;
	uint32_t codepoint;
	size_t consumed;

	consumed = mxsd_utf8_decode(parser->input + start, parser->length - start, &codepoint);
	if (consumed == 0u || !mxsd_xml_name_start(codepoint)) {
		return 0;
	}
	parser->position += consumed;
	while (parser->position < parser->length) {
		consumed = mxsd_utf8_decode(parser->input + parser->position,
					    parser->length - parser->position, &codepoint);
		if (consumed == 0u || !mxsd_xml_name_char(codepoint)) {
			break;
		}
		parser->position += consumed;
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

static int expanded_name_has_namespace(const xml_parser_t *parser, text_span_t qualified_name,
				       const char *expected_namespace, int is_attribute)
{
	text_span_t prefix, local_name;
	(void)local_name;
	split_qualified_name(qualified_name, &prefix, &local_name);
	return span_equals_string(resolve_namespace(parser, prefix, is_attribute),
				  expected_namespace);
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
		    (length == 7u && memcmp(cursor, "##other", 7u) == 0 && uri.length > 0 &&
		     !span_equals_nullable(uri, wildcard->target_namespace)) ||
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

static int wildcard_accepts_namespace(const mxsd_wildcard_t *wildcard, text_span_t uri)
{
	const char *cursor = wildcard->namespace_constraint;

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
		    (length == 7u && memcmp(cursor, "##other", 7u) == 0 && uri.length > 0 &&
		     !span_equals_nullable(uri, wildcard->target_namespace)) ||
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

static mxsd_result_t register_id(xml_parser_t *parser, text_span_t value, size_t offset,
				 const char *path)
{
	char decoded_storage[MXSD_MAX_TEXT_LENGTH + 1u];
	text_span_t decoded;
	size_t index;

	if (parser->values_are_decoded) {
		decoded = value;
	} else if (!mxsd_decode_xml_references(value, &decoded, decoded_storage)) {
		return make_result(MXSD_ERR_INVALID_VALUE, offset, path);
	}
	value = decoded;
	while (value.length > 0 && isspace((unsigned char)value.data[0])) {
		value.data++;
		value.length--;
	}
	while (value.length > 0 && isspace((unsigned char)value.data[value.length - 1u])) {
		value.length--;
	}
	for (index = 0; index < parser->id_count; index++) {
		if (value.length == parser->ids[index].length &&
		    memcmp(value.data, parser->ids[index].data, value.length) == 0) {
			return make_result(MXSD_ERR_IDENTITY_CONSTRAINT, offset, path);
		}
	}
	if (parser->id_count >= MXSD_MAX_IDENTITY_VALUES) {
		return make_result(MXSD_ERR_RESOURCE_LIMIT, offset, path);
	}
	if (value.length > MXSD_MAX_IDENTITY_TEXT - parser->identity_text_length) {
		return make_result(MXSD_ERR_RESOURCE_LIMIT, offset, path);
	}
	memcpy(parser->identity_text + parser->identity_text_length, value.data, value.length);
	parser->ids[parser->id_count].data = parser->identity_text + parser->identity_text_length;
	parser->ids[parser->id_count].length = value.length;
	parser->identity_text_length += value.length;
	parser->id_count++;
	return make_result(MXSD_OK, offset, NULL);
}

static int type_is_idref(mxsd_type_t type, const mxsd_simple_t *simple)
{
	if (simple != NULL && simple->variety == MXSD_SIMPLE_LIST) {
		return simple->item_type.type == MXSD_T_IDREF ||
		       (simple->item_type.simple != NULL &&
			simple->item_type.simple->base == MXSD_T_IDREF);
	}
	return (simple != NULL ? simple->base : type) == MXSD_T_IDREF;
}

static mxsd_result_t register_idrefs(xml_parser_t *parser, text_span_t value, size_t offset,
				     const char *path)
{
	char decoded_storage[MXSD_MAX_TEXT_LENGTH + 1u];
	text_span_t decoded;
	size_t position = 0u;

	if (parser->values_are_decoded) {
		decoded = value;
	} else if (!mxsd_decode_xml_references(value, &decoded, decoded_storage)) {
		return make_result(MXSD_ERR_INVALID_VALUE, offset, path);
	}
	value = decoded;
	while (position < value.length) {
		text_span_t reference;
		while (position < value.length && isspace((unsigned char)value.data[position])) {
			position++;
		}
		reference.data = value.data + position;
		while (position < value.length && !isspace((unsigned char)value.data[position])) {
			position++;
		}
		reference.length = (size_t)(value.data + position - reference.data);
		if (reference.length == 0u) {
			continue;
		}
		if (parser->idref_count >= MXSD_MAX_IDENTITY_VALUES) {
			return make_result(MXSD_ERR_RESOURCE_LIMIT, offset, path);
		}
		if (reference.length > MXSD_MAX_IDENTITY_TEXT - parser->identity_text_length) {
			return make_result(MXSD_ERR_RESOURCE_LIMIT, offset, path);
		}
		memcpy(parser->identity_text + parser->identity_text_length, reference.data,
		       reference.length);
		parser->idrefs[parser->idref_count].data =
			parser->identity_text + parser->identity_text_length;
		parser->idrefs[parser->idref_count].length = reference.length;
		parser->identity_text_length += reference.length;
		parser->idref_count++;
	}
	return make_result(MXSD_OK, offset, NULL);
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
	size_t id_attribute_count = 0;

	parser->position = tag->attributes_offset;
	while (validated_count < tag->attribute_count) {
		text_span_t name;
		text_span_t value;
		char normalized_attribute[MXSD_MAX_TEXT_LENGTH + 1u];
		const mxsd_attribute_t *attribute;
		size_t attribute_index;

		skip_whitespace(parser);
		if (at_end_of_start_tag(parser) || !parse_attribute(parser, &name, &value)) {
			return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
		}
		if (is_namespace_declaration(name)) {
			continue;
		}
		if (value.length <= MXSD_MAX_TEXT_LENGTH) {
			size_t source;
			size_t target = 0u;
			for (source = 0u; source < value.length; source++) {
				char character = value.data[source];
				if (character == '\r' && source + 1u < value.length &&
				    value.data[source + 1u] == '\n') {
					source++;
				}
				normalized_attribute[target++] =
					character == '\t' || character == '\r' || character == '\n'
						? ' '
						: character;
			}
			normalized_attribute[target] = '\0';
			value.data = normalized_attribute;
			value.length = target;
		}
		validated_count++;

		if (expanded_name_equals(parser, name, "nil",
					 "http://www.w3.org/2001/XMLSchema-instance", 1) ||
		    expanded_name_equals(parser, name, "type",
					 "http://www.w3.org/2001/XMLSchema-instance", 1) ||
		    expanded_name_equals(parser, name, "schemaLocation",
					 "http://www.w3.org/2001/XMLSchema-instance", 1) ||
		    expanded_name_equals(parser, name, "noNamespaceSchemaLocation",
					 "http://www.w3.org/2001/XMLSchema-instance", 1)) {
			continue;
		}
		if (expanded_name_has_namespace(parser, name,
						"http://www.w3.org/2001/XMLSchema-instance", 1)) {
			return make_result(MXSD_ERR_UNEXPECTED_ATTRIBUTE, parser->position, path);
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
					if ((global->simple_type != NULL
						     ? global->simple_type->base
						     : global->type) == MXSD_T_ID) {
						mxsd_result_t result = register_id(
							parser, value, parser->position, path);
						if (result.code != MXSD_OK) {
							return result;
						}
						id_attribute_count++;
						if (id_attribute_count > 1u) {
							return make_result(MXSD_ERR_INVALID_VALUE,
									   parser->position, path);
						}
					} else if (type_is_idref(global->type,
								 global->simple_type)) {
						mxsd_result_t result = register_idrefs(
							parser, value, parser->position, path);
						if (result.code != MXSD_OK) {
							return result;
						}
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
		if ((attribute->simple_type != NULL ? attribute->simple_type->base
						    : attribute->type) == MXSD_T_ID) {
			mxsd_result_t result = register_id(parser, value, parser->position, path);
			if (result.code != MXSD_OK) {
				return result;
			}
			id_attribute_count++;
			if (id_attribute_count > 1u) {
				return make_result(MXSD_ERR_INVALID_VALUE, parser->position, path);
			}
		} else if (type_is_idref(attribute->type, attribute->simple_type)) {
			mxsd_result_t result =
				register_idrefs(parser, value, parser->position, path);
			if (result.code != MXSD_OK) {
				return result;
			}
		}
	}

	parser->position = tag->content_offset;
	return make_result(MXSD_OK, parser->position, NULL);
}

static mxsd_result_t check_required_attributes(xml_parser_t *parser, const mxsd_complex_t *complex,
					       const unsigned char seen[MXSD_MAX_ATTRIBUTES],
					       size_t offset, const char *path)
{
	size_t index;

	for (index = 0; index < complex->attribute_count; index++) {
		const mxsd_attribute_t *attribute = &complex->attributes[index];
		if (complex->attributes[index].required &&
		    (index >= MXSD_MAX_ATTRIBUTES || !seen[index])) {
			return make_result(MXSD_ERR_MISSING_ATTRIBUTE, offset, path);
		}
		if ((index >= MXSD_MAX_ATTRIBUTES || !seen[index]) &&
		    type_is_idref(attribute->type, attribute->simple_type) &&
		    (attribute->fixed_value != NULL || attribute->default_value != NULL)) {
			const char *effective = attribute->fixed_value != NULL
							? attribute->fixed_value
							: attribute->default_value;
			text_span_t value = {effective, strlen(effective)};
			mxsd_result_t result = register_idrefs(parser, value, offset, path);
			if (result.code != MXSD_OK) {
				return result;
			}
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
				   const char *expected_namespace, text_span_t lexical_name);

static int resolved_name_equals(text_span_t local_name, text_span_t namespace_uri,
				const char *expected_name, const char *expected_namespace)
{
	return span_equals_string(local_name, expected_name) &&
	       span_equals_nullable(namespace_uri, expected_namespace);
}

static mxsd_result_t peek_child_name(xml_parser_t *parser, const char *path,
				     text_span_t *local_name, text_span_t *namespace_uri)
{
	size_t saved_position = parser->position;
	size_t saved_namespaces = parser->namespace_count;
	start_tag_t tag;
	text_span_t prefix;
	mxsd_result_t result;

	result = parse_start_tag(parser, path, &tag);
	if (result.code == MXSD_OK) {
		split_qualified_name(tag.name, &prefix, local_name);
		*namespace_uri = resolve_namespace(parser, prefix, 0);
		if (prefix.length > 0 && namespace_uri->length == 0) {
			result = make_result(MXSD_ERR_MALFORMED_XML, saved_position, path);
		}
	}
	parser->position = saved_position;
	parser->namespace_count = saved_namespaces;
	return result;
}

static const mxsd_wildcard_t typed_wildcard = {"##any", NULL, MXSD_PROCESS_LAX};
static const mxsd_particle_t typed_wildcard_particle = {
	MXSD_PARTICLE_ANY, 0UL, ULONG_MAX, NULL, NULL, 0, &typed_wildcard};
static const mxsd_complex_t typed_wildcard_any_type = {
	NULL,
	0,
	NULL,
	0,
	MXSD_SEQUENCE,
	0UL,
	ULONG_MAX,
	NULL,
	MXSD_T_STRING,
	0,
	0,
	&typed_wildcard_particle,
	&typed_wildcard,
	NULL,
	0,
	0,
	NULL,
	"http://www.w3.org/2001/XMLSchema",
	1,
	1,
	0,
};

static mxsd_result_t validate_typed_wildcard_element(xml_parser_t *parser, const char *path)
{
	static const char xsi[] = "http://www.w3.org/2001/XMLSchema-instance";
	size_t saved_position = parser->position;
	size_t saved_namespaces = parser->namespace_count;
	start_tag_t tag;
	/* Initialized for compilers that cannot see uri is set whenever it is used. */
	text_span_t prefix = {NULL, 0}, local = {NULL, 0}, uri = {NULL, 0};
	char local_storage[MXSD_MAX_TEXT_LENGTH + 1u];
	char uri_storage[MXSD_MAX_TEXT_LENGTH + 1u];
	size_t parsed = 0;
	int has_type = 0;
	mxsd_result_t result = parse_start_tag(parser, path, &tag);

	if (result.code != MXSD_OK) {
		goto restore;
	}
	split_qualified_name(tag.name, &prefix, &local);
	uri = resolve_namespace(parser, prefix, 0);
	if (local.length > MXSD_MAX_TEXT_LENGTH || uri.length > MXSD_MAX_TEXT_LENGTH) {
		result = make_result(MXSD_ERR_RESOURCE_LIMIT, saved_position, path);
		goto restore;
	}
	parser->position = tag.attributes_offset;
	while (parsed < tag.attribute_count) {
		text_span_t name, value;
		skip_whitespace(parser);
		if (!parse_attribute(parser, &name, &value)) {
			result = make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
			goto restore;
		}
		if (is_namespace_declaration(name)) {
			continue;
		}
		parsed++;
		if (expanded_name_equals(parser, name, "type", xsi, 1)) {
			has_type = 1;
		}
	}
	if (!has_type) {
		result = make_result(MXSD_ERR_UNEXPECTED_ELEMENT, saved_position, path);
		goto restore;
	}
	memcpy(local_storage, local.data, local.length);
	local_storage[local.length] = '\0';
	memcpy(uri_storage, uri.data, uri.length);
	uri_storage[uri.length] = '\0';
restore:
	parser->position = saved_position;
	parser->namespace_count = saved_namespaces;
	if (result.code != MXSD_OK || !has_type) {
		return result;
	}
	return validate_element(parser, local_storage, uri.length ? uri_storage : NULL,
				MXSD_T_COMPLEX, &typed_wildcard_any_type, NULL, NULL, NULL, 0, 0,
				0);
}

int mxsd_at_end_element(const xml_parser_t *parser)
{
	return parser->position + 1u < parser->length && parser->input[parser->position] == '<' &&
	       parser->input[parser->position + 1u] == '/';
}

static const mxsd_global_element_t *
find_global_element(const xml_parser_t *parser, text_span_t local_name, text_span_t namespace_uri)
{
	size_t index;
	for (index = 0; index < parser->global_element_count; index++) {
		const mxsd_global_element_t *element = &parser->global_elements[index];
		if (resolved_name_equals(local_name, namespace_uri, element->name,
					 element->namespace_uri)) {
			return element;
		}
	}
	return NULL;
}

static int builtin_derives_from(mxsd_type_t candidate, mxsd_type_t base, unsigned blocked);

static int union_accepts_builtin(const mxsd_simple_t *base, mxsd_type_t candidate, unsigned blocked)
{
	size_t index;

	if (base == NULL || base->variety != MXSD_SIMPLE_UNION ||
	    (blocked & MXSD_DERIVATION_RESTRICTION) != 0u) {
		return 0;
	}
	for (index = 0u; index < base->member_type_count; index++) {
		const mxsd_simple_ref_t *member = &base->member_types[index];
		if ((member->simple == NULL &&
		     builtin_derives_from(candidate, member->type, blocked)) ||
		    union_accepts_builtin(member->simple, candidate, blocked)) {
			return 1;
		}
	}
	return 0;
}

/** Return whether a generated descriptor denotes the simple-type ur-type. */
static int simple_is_any_simple_type(const mxsd_simple_t *simple)
{
	static const char xsd[] = "http://www.w3.org/2001/XMLSchema";

	return simple != NULL && simple->type_name != NULL && simple->type_namespace != NULL &&
	       strcmp(simple->type_name, "anySimpleType") == 0 &&
	       strcmp(simple->type_namespace, xsd) == 0;
}

static int substitution_type_allowed(const mxsd_global_element_t *candidate,
				     const mxsd_element_t *head)
{
	unsigned blocked = head->block_methods |
			   (head->complex_type != NULL ? head->complex_type->block_methods : 0u);
	if (simple_is_any_simple_type(head->simple_type)) {
		return candidate->complex_type == NULL ||
		       candidate->complex_type->has_simple_content;
	}
	if (candidate->complex_type != NULL) {
		if (head->complex_type != NULL) {
			return complex_derives_from(candidate->complex_type, head->complex_type,
						    blocked);
		}
		if (candidate->complex_type->has_simple_content &&
		    (candidate->complex_type->derivation_method & blocked) == 0u) {
			return simple_derives_from(candidate->complex_type->simple_content,
						   head->simple_type, blocked) &&
			       (head->simple_type != NULL ||
				candidate->complex_type->simple_content_type == head->type);
		}
		return 0;
	}
	if (candidate->simple_type != NULL) {
		if (head->complex_type != NULL && head->complex_type->any_type) {
			return (blocked & MXSD_DERIVATION_RESTRICTION) == 0u;
		}
		return simple_derives_from(candidate->simple_type, head->simple_type, blocked) &&
		       (head->simple_type != NULL || candidate->type == head->type);
	}
	if (head->complex_type != NULL && head->complex_type->any_type) {
		return (blocked & MXSD_DERIVATION_RESTRICTION) == 0u;
	}
	return candidate->type == head->type ||
	       union_accepts_builtin(head->simple_type, candidate->type, blocked);
}

int mxsd_global_substitutes_for(const xml_parser_t *parser, const mxsd_global_element_t *candidate,
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
			return substitution_type_allowed(candidate, head);
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

mxsd_result_t mxsd_skip_element(xml_parser_t *parser, const char *path)
{
	start_tag_t tag;
	mxsd_result_t result = parse_start_tag(parser, path, &tag);
	if (result.code != MXSD_OK) {
		return result;
	}
	if (!tag.is_empty) {
		for (;;) {
			skip_ignorable_markup(parser);
			if (at_end_element(parser)) {
				break;
			}
			if (parser->position >= parser->length) {
				return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
			}
			if (parser->position + 9u <= parser->length &&
			    memcmp(parser->input + parser->position, "<![CDATA[", 9u) == 0) {
				/* A CDATA section may contain markup characters; skip it whole. */
				const char *end =
					strstr(parser->input + parser->position + 9u, "]]>");

				if (end == NULL) {
					return make_result(MXSD_ERR_MALFORMED_XML, parser->position,
							   path);
				}
				parser->position = (size_t)(end - parser->input) + 3u;
				continue;
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

/** Skip character data between particles when the containing type is mixed. */
static void skip_particle_text(xml_parser_t *parser)
{
	for (;;) {
		size_t saved;

		if (parser->mixed_content) {
			while (parser->position < parser->length &&
			       parser->input[parser->position] != '<') {
				parser->position++;
			}
		} else {
			skip_whitespace(parser);
		}
		saved = parser->position;
		skip_ignorable_markup(parser);
		if (parser->position == saved) {
			return;
		}
	}
}

static int particle_accepts_empty(const mxsd_particle_t *particle);

static int particle_can_start(xml_parser_t *parser, const mxsd_particle_t *particle)
{
	text_span_t name, namespace_uri;
	size_t index;
	skip_particle_text(parser);
	if (at_end_element(parser) || parser->position >= parser->length ||
	    parser->input[parser->position] != '<' ||
	    peek_child_name(parser, NULL, &name, &namespace_uri).code != MXSD_OK) {
		return 0;
	}
	if (particle->kind == MXSD_PARTICLE_ELEMENT) {
		if (resolved_name_equals(name, namespace_uri, particle->element->name,
					 particle->element->namespace_uri)) {
			return 1;
		}
		return mxsd_global_substitutes_for(parser,
						   find_global_element(parser, name, namespace_uri),
						   particle->element);
	}
	if (particle->kind == MXSD_PARTICLE_ANY) {
		return wildcard_accepts_namespace(particle->wildcard, namespace_uri);
	}
	for (index = 0; index < particle->child_count; index++) {
		if (particle_can_start(parser, &particle->children[index])) {
			return 1;
		}
		if (particle->kind == MXSD_PARTICLE_SEQUENCE &&
		    particle->children[index].min_occurs > 0 &&
		    !particle_accepts_empty(&particle->children[index])) {
			break;
		}
	}
	return 0;
}

static mxsd_result_t consume_particle(xml_parser_t *parser, const mxsd_particle_t *particle,
				      const char *path, unsigned long *consumed, int force_minimum);
static const mxsd_element_t *particle_homogeneous_element(const mxsd_particle_t *particle)
{
	const mxsd_element_t *element = NULL;
	size_t index;

	if (particle->kind == MXSD_PARTICLE_ELEMENT) {
		return particle->element;
	}
	if (particle->kind == MXSD_PARTICLE_ANY) {
		return NULL;
	}
	for (index = 0u; index < particle->child_count; index++) {
		const mxsd_element_t *child =
			particle_homogeneous_element(&particle->children[index]);
		if (child == NULL) {
			return NULL;
		}
		if (element != NULL &&
		    (strcmp(element->name, child->name) != 0 ||
		     ((element->namespace_uri == NULL) != (child->namespace_uri == NULL)) ||
		     (element->namespace_uri != NULL &&
		      strcmp(element->namespace_uri, child->namespace_uri) != 0))) {
			return NULL;
		}
		element = child;
	}
	return element;
}

static unsigned long particle_one_maximum(const mxsd_particle_t *particle)
{
	unsigned long result = 0u;
	size_t index;

	if (particle->kind == MXSD_PARTICLE_ELEMENT || particle->kind == MXSD_PARTICLE_ANY) {
		return 1u;
	}
	for (index = 0u; index < particle->child_count; index++) {
		const mxsd_particle_t *child = &particle->children[index];
		unsigned long child_maximum = particle_one_maximum(child);
		if (child->max_occurs == ULONG_MAX || child_maximum == ULONG_MAX ||
		    child_maximum > ULONG_MAX / child->max_occurs) {
			child_maximum = ULONG_MAX;
		} else {
			child_maximum *= child->max_occurs;
		}
		if (particle->kind == MXSD_PARTICLE_CHOICE) {
			if (child_maximum > result) {
				result = child_maximum;
			}
		} else if (result == ULONG_MAX || child_maximum == ULONG_MAX ||
			   child_maximum > ULONG_MAX - result) {
			result = ULONG_MAX;
		} else {
			result += child_maximum;
		}
	}
	return result;
}

static unsigned long consecutive_particle_starts(xml_parser_t *parser,
						 const mxsd_particle_t *particle, const char *path)
{
	xml_parser_t lookahead = *parser;
	unsigned long count = 0u;

	while (count < ULONG_MAX && particle_can_start(&lookahead, particle)) {
		mxsd_result_t result = skip_any_element(&lookahead, path);
		if (result.code != MXSD_OK) {
			break;
		}
		count++;
	}
	return count;
}

static int particle_contains_start(xml_parser_t *parser, const mxsd_particle_t *particle)
{
	size_t index;
	if (particle->kind == MXSD_PARTICLE_ELEMENT || particle->kind == MXSD_PARTICLE_ANY) {
		return particle_can_start(parser, particle);
	}
	for (index = 0; index < particle->child_count; index++) {
		if (particle_contains_start(parser, &particle->children[index])) {
			return 1;
		}
	}
	return 0;
}

static mxsd_result_t match_particle_once(xml_parser_t *parser, const mxsd_particle_t *particle,
					 const char *path, int prefer_minimum, int *matched)
{
	size_t index;
	*matched = 0;
	if (particle->kind == MXSD_PARTICLE_ELEMENT) {
		mxsd_result_t result;
		text_span_t actual_name, actual_namespace;
		const mxsd_global_element_t *substitute = NULL;
		if (!particle_can_start(parser, particle)) {
			return make_result(MXSD_OK, parser->position, NULL);
		}
		result = peek_child_name(parser, path, &actual_name, &actual_namespace);
		if (result.code != MXSD_OK) {
			return result;
		}
		if (!resolved_name_equals(actual_name, actual_namespace, particle->element->name,
					  particle->element->namespace_uri)) {
			substitute = find_global_element(parser, actual_name, actual_namespace);
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
		text_span_t name, namespace_uri;
		const mxsd_global_element_t *declaration;
		mxsd_result_t result;
		if (!particle_can_start(parser, particle)) {
			return make_result(MXSD_OK, parser->position, NULL);
		}
		result = peek_child_name(parser, path, &name, &namespace_uri);
		if (result.code != MXSD_OK) {
			return result;
		}
		declaration = find_global_element(parser, name, namespace_uri);
		if (particle->wildcard->process_contents == MXSD_PROCESS_STRICT &&
		    declaration == NULL) {
			result = validate_typed_wildcard_element(parser, path);
			if (result.code == MXSD_OK) {
				*matched = 1;
			}
			return result;
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
			const mxsd_particle_t *child = &particle->children[index];
			mxsd_particle_t limited;
			mxsd_result_t result;
			if (prefer_minimum && index == 0u && child->min_occurs > 0u &&
			    child->max_occurs > child->min_occurs) {
				limited = *child;
				limited.max_occurs = limited.min_occurs;
				child = &limited;
			}
			result = consume_particle(parser, child, path, &count, prefer_minimum);
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
				const mxsd_particle_t *child = &particle->children[index];
				mxsd_particle_t limited;
				mxsd_result_t result;
				if (prefer_minimum && child->min_occurs > 0u &&
				    child->max_occurs > child->min_occurs) {
					limited = *child;
					limited.max_occurs = limited.min_occurs;
					child = &limited;
				}
				result = consume_particle(parser, child, path, &count,
							  prefer_minimum);
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
						match_particle_once(parser, child, path, 0, &one);
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
				      const char *path, unsigned long *consumed, int force_minimum)
{
	*consumed = 0;
	while (*consumed < particle->max_occurs && particle_can_start(parser, particle)) {
		int matched = 0;
		int prefer_minimum = force_minimum || *consumed + 1u < particle->min_occurs;
		if (!prefer_minimum && particle->max_occurs > 1u &&
		    particle->kind != MXSD_PARTICLE_ELEMENT &&
		    particle->kind != MXSD_PARTICLE_ANY) {
			if (particle_homogeneous_element(particle) != NULL) {
				unsigned long maximum = particle_one_maximum(particle);
				prefer_minimum = maximum != ULONG_MAX &&
						 consecutive_particle_starts(parser, particle,
									     path) > maximum;
			} else if (particle->kind == MXSD_PARTICLE_CHOICE) {
				size_t child_index;
				for (child_index = 0u; child_index < particle->child_count;
				     child_index++) {
					const mxsd_particle_t *child =
						&particle->children[child_index];
					unsigned long maximum;
					if (!particle_can_start(parser, child) ||
					    particle_homogeneous_element(child) == NULL) {
						continue;
					}
					maximum = particle_one_maximum(child);
					if (child->max_occurs != ULONG_MAX &&
					    maximum <= ULONG_MAX / child->max_occurs) {
						maximum *= child->max_occurs;
					} else {
						maximum = ULONG_MAX;
					}
					prefer_minimum = maximum != ULONG_MAX &&
							 consecutive_particle_starts(
								 parser, child, path) > maximum;
					break;
				}
			}
		}
		mxsd_result_t result =
			match_particle_once(parser, particle, path, prefer_minimum, &matched);
		if (result.code != MXSD_OK) {
			return result;
		}
		if (!matched) {
			break;
		}
		(*consumed)++;
	}
	if (*consumed < particle->min_occurs && !particle_accepts_empty(particle)) {
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
	skip_particle_text(parser);
	result = consume_particle(parser, particle, path, &count, 0);
	if (result.code != MXSD_OK) {
		return result;
	}
	skip_particle_text(parser);
	if (!at_end_element(parser)) {
		/* A remaining declared token is normally a saturated occurrence. */
		return make_result(particle_contains_start(parser, particle)
					   ? MXSD_ERR_OCCURRENCE
					   : MXSD_ERR_UNEXPECTED_ELEMENT,
				   parser->position, path);
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

static size_t find_unordered_child(const mxsd_complex_t *complex, text_span_t child_name,
				   text_span_t child_namespace)
{
	size_t index;

	for (index = 0; index < complex->element_count; index++) {
		const mxsd_element_t *element = &complex->elements[index];
		if (resolved_name_equals(child_name, child_namespace, element->name,
					 element->namespace_uri)) {
			return index;
		}
	}
	return complex->element_count;
}

static mxsd_result_t find_sequence_child(const mxsd_complex_t *complex, text_span_t child_name,
					 text_span_t child_namespace, size_t offset, size_t *index,
					 unsigned long *occurrences)
{
	if (*index >= complex->element_count) {
		return make_result(MXSD_ERR_UNEXPECTED_ELEMENT, offset, NULL);
	}

	while (*index < complex->element_count) {
		const mxsd_element_t *element = &complex->elements[*index];
		if (resolved_name_equals(child_name, child_namespace, element->name,
					 element->namespace_uri)) {
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
		text_span_t child_name, child_namespace;
		mxsd_result_t result;
		const mxsd_element_t *element;
		unsigned long occurrences;
		size_t child_offset;

		skip_particle_text(parser);
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
		result = peek_child_name(parser, path, &child_name, &child_namespace);
		if (result.code != MXSD_OK) {
			return result;
		}

		if (complex->model == MXSD_SEQUENCE) {
			result = find_sequence_child(complex, child_name, child_namespace,
						     child_offset, &current_index,
						     &sequence_occurrences);
			if (result.code != MXSD_OK) {
				if (result.code == MXSD_ERR_UNEXPECTED_ELEMENT &&
				    result.path == NULL) {
					result.path = path;
				}
				return result;
			}
			occurrences = sequence_occurrences;
		} else {
			current_index = find_unordered_child(complex, child_name, child_namespace);
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
	char value_storage[MXSD_MAX_TEXT_LENGTH + 1u];
	xml_parser_t decoded_parser;

	if (!is_empty) {
		size_t value_offset = parser->position;
		size_t value_length = 0u;

		while (parser->position < parser->length) {
			const char *end;
			size_t start;

			if (parser->input[parser->position] != '<') {
				char decoded_storage[MXSD_MAX_TEXT_LENGTH + 1u];
				text_span_t raw, decoded;

				start = parser->position;
				while (parser->position < parser->length &&
				       parser->input[parser->position] != '<') {
					parser->position++;
				}
				raw.data = parser->input + start;
				raw.length = parser->position - start;
				if (!mxsd_decode_xml_references(raw, &decoded, decoded_storage)) {
					return make_result(MXSD_ERR_INVALID_VALUE, start, path);
				}
				if (decoded.length > MXSD_MAX_TEXT_LENGTH - value_length) {
					return make_result(MXSD_ERR_RESOURCE_LIMIT, start, path);
				}
				memcpy(value_storage + value_length, decoded.data, decoded.length);
				value_length += decoded.length;
				continue;
			}
			if (parser->position + 2u <= parser->length &&
			    parser->input[parser->position + 1u] == '/') {
				break;
			}
			if (parser->position + 4u <= parser->length &&
			    memcmp(parser->input + parser->position, "<!--", 4u) == 0) {
				end = strstr(parser->input + parser->position + 4u, "-->");
				if (end == NULL) {
					return make_result(MXSD_ERR_MALFORMED_XML, parser->position,
							   path);
				}
				parser->position = (size_t)(end - parser->input) + 3u;
				continue;
			}
			if (parser->position + 2u <= parser->length &&
			    memcmp(parser->input + parser->position, "<?", 2u) == 0) {
				end = strstr(parser->input + parser->position + 2u, "?>");
				if (end == NULL) {
					return make_result(MXSD_ERR_MALFORMED_XML, parser->position,
							   path);
				}
				parser->position = (size_t)(end - parser->input) + 2u;
				continue;
			}
			if (parser->position + 9u <= parser->length &&
			    memcmp(parser->input + parser->position, "<![CDATA[", 9u) == 0) {
				start = parser->position + 9u;
				end = strstr(parser->input + start, "]]>");
				if (end == NULL) {
					return make_result(MXSD_ERR_MALFORMED_XML, parser->position,
							   path);
				}
				if ((size_t)(end - parser->input) - start >
				    MXSD_MAX_TEXT_LENGTH - value_length) {
					return make_result(MXSD_ERR_RESOURCE_LIMIT, start, path);
				}
				memcpy(value_storage + value_length, parser->input + start,
				       (size_t)(end - parser->input) - start);
				value_length += (size_t)(end - parser->input) - start;
				parser->position = (size_t)(end - parser->input) + 3u;
				continue;
			}
			return make_result(MXSD_ERR_UNEXPECTED_ELEMENT, parser->position, path);
		}
		value_storage[value_length] = '\0';
		value.data = value_storage;
		value.length = value_length;
		if (value.length == 0 && (fixed_value != NULL || default_value != NULL)) {
			const char *effective = fixed_value != NULL ? fixed_value : default_value;
			value.data = effective;
			value.length = strlen(effective);
		}
		decoded_parser = *parser;
		decoded_parser.values_are_decoded = 1;
		if (!validate_simple_value(&decoded_parser, type, simple, value) ||
		    (fixed_value != NULL &&
		     !simple_values_equal(&decoded_parser, type, simple, value, fixed_value))) {
			return make_result(MXSD_ERR_INVALID_VALUE, value_offset, path);
		}
	} else {
		const char *effective = fixed_value != NULL ? fixed_value : default_value;
		decoded_parser = *parser;
		decoded_parser.values_are_decoded = 1;
		if (effective != NULL) {
			value.data = effective;
			value.length = strlen(effective);
		}
		if (!validate_simple_value(&decoded_parser, type, simple, value)) {
			return make_result(MXSD_ERR_INVALID_VALUE, element_offset, path);
		}
	}
	if ((simple != NULL ? simple->base : type) == MXSD_T_ID) {
		int was_decoded = parser->values_are_decoded;
		mxsd_result_t result;

		parser->values_are_decoded = 1;
		result = register_id(parser, value, element_offset, path);
		parser->values_are_decoded = was_decoded;
		return result;
	}
	if (type_is_idref(type, simple)) {
		int was_decoded = parser->values_are_decoded;
		mxsd_result_t result;

		parser->values_are_decoded = 1;
		result = register_idrefs(parser, value, element_offset, path);
		parser->values_are_decoded = was_decoded;
		return result;
	}

	return make_result(MXSD_OK, parser->position, NULL);
}

static mxsd_result_t parse_end_tag(xml_parser_t *parser, const char *expected_name,
				   const char *expected_namespace, text_span_t lexical_name)
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
	    (expected_name != NULL
		     ? !expanded_name_equals(parser, name, expected_name, expected_namespace, 0)
		     : name.length != lexical_name.length ||
			       memcmp(name.data, lexical_name.data, name.length) != 0)) {
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

static text_span_t collapse_qname_whitespace(text_span_t value)
{
	while (value.length != 0u && isspace((unsigned char)value.data[0])) {
		value.data++;
		value.length--;
	}
	while (value.length != 0u && isspace((unsigned char)value.data[value.length - 1u])) {
		value.length--;
	}
	return value;
}

static int find_builtin_type(const xml_parser_t *parser, text_span_t lexical_qname,
			     mxsd_type_t *type, int *is_any_type)
{
	static const struct {
		const char *name;
		mxsd_type_t type;
	} builtins[] = {
		{"anySimpleType", MXSD_T_STRING},
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
	static const char xsd_namespace[] = "http://www.w3.org/2001/XMLSchema";
	text_span_t prefix, local, uri;
	size_t index;

	split_qualified_name(lexical_qname, &prefix, &local);
	uri = resolve_namespace(parser, prefix, 0);
	if (!span_equals_string(uri, xsd_namespace)) {
		return 0;
	}
	*is_any_type = span_equals_string(local, "anyType");
	if (*is_any_type) {
		return 1;
	}
	for (index = 0; index < sizeof(builtins) / sizeof(builtins[0]); index++) {
		if (span_equals_string(local, builtins[index].name)) {
			*type = builtins[index].type;
			return 1;
		}
	}
	return 0;
}

int mxsd_complex_derives_from(const mxsd_complex_t *candidate, const mxsd_complex_t *base,
			      unsigned blocked)
{
	if (base != NULL && base->any_type) {
		while (candidate != NULL) {
			if ((candidate->derivation_method & blocked) != 0u) {
				return 0;
			}
			candidate = candidate->base_definition;
		}
		return 1;
	}
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
	if (simple_is_any_simple_type(base)) {
		return candidate != NULL && (candidate->derivation_method & blocked) == 0u;
	}
	if (base != NULL && base->variety == MXSD_SIMPLE_UNION) {
		size_t index;
		if ((blocked & MXSD_DERIVATION_RESTRICTION) != 0u) {
			return 0;
		}
		for (index = 0u; index < base->member_type_count; index++) {
			const mxsd_simple_ref_t *member = &base->member_types[index];
			if ((member->simple != NULL &&
			     simple_derives_from(candidate, member->simple, blocked)) ||
			    (member->simple == NULL && candidate != NULL &&
			     builtin_derives_from(candidate->base, member->type, blocked))) {
				return 1;
			}
		}
		return 0;
	}
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

static int builtin_parent(mxsd_type_t type, mxsd_type_t *parent)
{
	switch (type) {
	case MXSD_T_INTEGER:
		*parent = MXSD_T_DECIMAL;
		return 1;
	case MXSD_T_LONG:
		*parent = MXSD_T_INTEGER;
		return 1;
	case MXSD_T_INT:
		*parent = MXSD_T_LONG;
		return 1;
	case MXSD_T_SHORT:
		*parent = MXSD_T_INT;
		return 1;
	case MXSD_T_BYTE:
		*parent = MXSD_T_SHORT;
		return 1;
	case MXSD_T_NON_POSITIVE_INTEGER:
		*parent = MXSD_T_INTEGER;
		return 1;
	case MXSD_T_NEGATIVE_INTEGER:
		*parent = MXSD_T_NON_POSITIVE_INTEGER;
		return 1;
	case MXSD_T_NON_NEGATIVE_INTEGER:
		*parent = MXSD_T_INTEGER;
		return 1;
	case MXSD_T_POSITIVE_INTEGER:
		*parent = MXSD_T_NON_NEGATIVE_INTEGER;
		return 1;
	case MXSD_T_UNSIGNED_LONG:
		*parent = MXSD_T_NON_NEGATIVE_INTEGER;
		return 1;
	case MXSD_T_UNSIGNED_INT:
		*parent = MXSD_T_UNSIGNED_LONG;
		return 1;
	case MXSD_T_UNSIGNED_SHORT:
		*parent = MXSD_T_UNSIGNED_INT;
		return 1;
	case MXSD_T_UNSIGNED_BYTE:
		*parent = MXSD_T_UNSIGNED_SHORT;
		return 1;
	case MXSD_T_NORMALIZED_STRING:
		*parent = MXSD_T_STRING;
		return 1;
	case MXSD_T_TOKEN:
		*parent = MXSD_T_NORMALIZED_STRING;
		return 1;
	case MXSD_T_LANGUAGE:
	case MXSD_T_NAME:
	case MXSD_T_NMTOKEN:
		*parent = MXSD_T_TOKEN;
		return 1;
	case MXSD_T_NCNAME:
		*parent = MXSD_T_NAME;
		return 1;
	case MXSD_T_ID:
	case MXSD_T_IDREF:
	case MXSD_T_ENTITY:
		*parent = MXSD_T_NCNAME;
		return 1;
	default:
		return 0;
	}
}

static int builtin_derives_from(mxsd_type_t candidate, mxsd_type_t base, unsigned blocked)
{
	if (candidate == base) {
		return 1;
	}
	if ((blocked & MXSD_DERIVATION_RESTRICTION) != 0u) {
		return 0;
	}
	while (builtin_parent(candidate, &candidate)) {
		if (candidate == base) {
			return 1;
		}
	}
	return 0;
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
	int saved_mixed_content;
	int unbound = expected_name == NULL;

	parser->depth++;
	if (parser->depth > MXSD_MAX_DEPTH) {
		return make_result(MXSD_ERR_EXCESSIVE_NESTING, parser->position, expected_name);
	}

	result = parse_start_tag(parser, expected_name, &tag);
	if (result.code != MXSD_OK) {
		return result;
	}

	if (!unbound &&
	    !expanded_name_equals(parser, tag.name, expected_name, expected_namespace, 0)) {
		mxsd_error_t code =
			parser->depth == 1 ? MXSD_ERR_WRONG_ROOT : MXSD_ERR_UNEXPECTED_ELEMENT;
		return make_result(code, tag.start_offset, expected_name);
	}

	if (!unbound && abstract_element) {
		return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset, expected_name);
	}

	result = read_instance_options(parser, &tag, expected_name, &instance);
	if (result.code != MXSD_OK) {
		return result;
	}
	if (unbound && !instance.type_present) {
		return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset, NULL);
	}
	if (instance.type_present) {
		instance.type_name = collapse_qname_whitespace(instance.type_name);
		const mxsd_named_type_t *dynamic = find_named_type(parser, instance.type_name);
		mxsd_type_t builtin_type = MXSD_T_STRING;
		int builtin_any_type = 0;
		unsigned effective_block =
			block_methods | (complex != NULL ? complex->block_methods : 0u);
		int builtin =
			dynamic == NULL && find_builtin_type(parser, instance.type_name,
							     &builtin_type, &builtin_any_type);
		if (builtin && unbound) {
			if (builtin_any_type) {
				return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset, NULL);
			}
			type = builtin_type;
			complex = NULL;
			simple = NULL;
		} else if (builtin) {
			if (builtin_any_type) {
				if (type != MXSD_T_COMPLEX || complex == NULL ||
				    !complex->any_type) {
					return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset,
							   expected_name);
				}
			} else {
				if (type == MXSD_T_COMPLEX) {
					if (complex == NULL || !complex->any_type ||
					    (effective_block & MXSD_DERIVATION_RESTRICTION) != 0u) {
						return make_result(MXSD_ERR_INVALID_VALUE,
								   tag.start_offset, expected_name);
					}
				} else if ((simple != NULL && !simple_is_any_simple_type(simple)) ||
					   (simple == NULL &&
					    !builtin_derives_from(builtin_type, type,
								  effective_block))) {
					return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset,
							   expected_name);
				}
				type = builtin_type;
				complex = NULL;
				simple = NULL;
			}
		} else if (dynamic == NULL) {
			return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset, expected_name);
		}
		if (!builtin && dynamic->complex_type != NULL && unbound) {
			type = MXSD_T_COMPLEX;
			complex = dynamic->complex_type;
			simple = NULL;
		} else if (!builtin && dynamic->complex_type != NULL) {
			int derives = 0;
			if (type == MXSD_T_COMPLEX) {
				derives = complex_derives_from(dynamic->complex_type, complex,
							       effective_block);
			} else if (dynamic->complex_type->has_simple_content &&
				   (dynamic->complex_type->derivation_method & effective_block) ==
					   0u) {
				derives =
					simple_is_any_simple_type(simple) ||
					(simple_derives_from(dynamic->complex_type->simple_content,
							     simple, effective_block) &&
					 (simple != NULL ||
					  dynamic->complex_type->simple_content_type == type));
			}
			if (!derives || dynamic->complex_type->abstract_type) {
				return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset,
						   expected_name);
			}
			type = MXSD_T_COMPLEX;
			complex = dynamic->complex_type;
			simple = NULL;
		} else if (!builtin && unbound) {
			type = dynamic->type;
			simple = dynamic->simple_type;
			complex = NULL;
		} else if (!builtin) {
			if (type == MXSD_T_COMPLEX) {
				if (complex == NULL || !complex->any_type ||
				    !simple_derives_from(dynamic->simple_type, NULL,
							 effective_block)) {
					return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset,
							   expected_name);
				}
			} else if (!simple_derives_from(dynamic->simple_type, simple,
							effective_block) ||
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
		result = check_required_attributes(parser, complex, seen_attributes,
						   tag.start_offset, expected_name);
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
	} else if (instance.nil_present && !nillable) {
		return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset, expected_name);
	} else if (type == MXSD_T_COMPLEX) {
		saved_mixed_content = parser->mixed_content;
		parser->mixed_content = complex->mixed_content;
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
		parser->mixed_content = saved_mixed_content;
	} else {
		result = validate_simple_content(parser, expected_name, type, simple, tag.is_empty,
						 tag.start_offset, default_value, fixed_value);
	}

	if (result.code != MXSD_OK) {
		return result;
	}
	if (!tag.is_empty && type == MXSD_T_COMPLEX && complex->mixed_content &&
	    fixed_value != NULL) {
		text_span_t mixed_value = {parser->input + tag.content_offset,
					   parser->position - tag.content_offset};

		if (mixed_value.length != 0u &&
		    (memchr(mixed_value.data, '<', mixed_value.length) != NULL ||
		     !simple_values_equal(parser, MXSD_T_STRING, NULL, mixed_value, fixed_value))) {
			return make_result(MXSD_ERR_INVALID_VALUE, tag.start_offset, expected_name);
		}
	}

	if (!tag.is_empty) {
		result = parse_end_tag(parser, expected_name, expected_namespace, tag.name);
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

	if (schema == NULL || xml == NULL) {
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
	parser.notations = schema->notations;
	parser.notation_count = schema->notation_count;
	if (parser.length >= 3u && (unsigned char)parser.input[0] == 0xefu &&
	    (unsigned char)parser.input[1] == 0xbbu && (unsigned char)parser.input[2] == 0xbfu) {
		parser.position = 3u;
	}
	skip_whitespace(&parser);

	result = skip_xml_declaration(&parser);
	if (result.code != MXSD_OK) {
		return result;
	}
	skip_ignorable_markup(&parser);

	result = validate_element(&parser, schema->root_name, schema->root_namespace_uri,
				  schema->root_type, schema->root_complex, schema->root_simple,
				  schema->default_value, schema->fixed_value, schema->nillable,
				  schema->abstract_element, schema->block_methods);
	if (result.code != MXSD_OK) {
		return result;
	}

	skip_ignorable_markup(&parser);
	if (parser.position != parser.length) {
		return make_result(MXSD_ERR_MALFORMED_XML, parser.position, NULL);
	}
	{
		size_t reference;
		for (reference = 0u; reference < parser.idref_count; reference++) {
			size_t identifier;
			int found = 0;
			for (identifier = 0u; identifier < parser.id_count; identifier++) {
				if (parser.idrefs[reference].length ==
					    parser.ids[identifier].length &&
				    memcmp(parser.idrefs[reference].data,
					   parser.ids[identifier].data,
					   parser.idrefs[reference].length) == 0) {
					found = 1;
					break;
				}
			}
			if (!found) {
				return make_result(MXSD_ERR_IDENTITY_CONSTRAINT, parser.position,
						   NULL);
			}
		}
	}
	return make_result(MXSD_OK, parser.position, NULL);
}
