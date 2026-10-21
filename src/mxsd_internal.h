/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Private interfaces shared by the mxsd runtime modules.
 */

#ifndef MXSD_INTERNAL_H
#define MXSD_INTERNAL_H

#include "mxsd.h"

#include <stddef.h>

/** Non-owning view over a contiguous character range. */
typedef struct {
	const char *data; /**< First character in the range. */
	size_t length;    /**< Number of bytes in the range. */
} text_span_t;

/** Namespace prefix binding active at the current parser depth. */
typedef struct {
	text_span_t prefix; /**< Prefix without the trailing colon. */
	text_span_t uri;    /**< Namespace URI bound to the prefix. */
} namespace_binding_t;

/** Mutable state of the allocation-free XML reader. */
typedef struct {
	const char *input;
	size_t length;
	size_t position;
	unsigned depth;
	namespace_binding_t namespaces[MXSD_MAX_NAMESPACES];
	size_t namespace_count;
	const mxsd_global_element_t *global_elements;
	size_t global_element_count;
	const mxsd_global_attribute_t *global_attributes;
	size_t global_attribute_count;
	const mxsd_named_type_t *named_types;
	size_t named_type_count;
	const mxsd_notation_t *notations;
	size_t notation_count;
	int mixed_content;
	int values_are_decoded;
	text_span_t ids[MXSD_MAX_IDENTITY_VALUES];
	size_t id_count;
	text_span_t idrefs[MXSD_MAX_IDENTITY_VALUES];
	size_t idref_count;
	char identity_text[MXSD_MAX_IDENTITY_TEXT];
	size_t identity_text_length;
} xml_parser_t;

/** Information collected while scanning an XML start tag. */
typedef struct {
	text_span_t name;
	size_t start_offset;
	size_t attributes_offset;
	size_t content_offset;
	size_t namespace_base;
	size_t attribute_count;
	int is_empty;
} start_tag_t;

mxsd_result_t mxsd_make_result(mxsd_error_t code, size_t offset, const char *path);
int mxsd_span_equals_string(text_span_t span, const char *string);
int mxsd_span_equals_nullable(text_span_t span, const char *text);
void mxsd_skip_whitespace(xml_parser_t *parser);
void mxsd_skip_ignorable_markup(xml_parser_t *parser);
int mxsd_parse_xml_name(xml_parser_t *parser, text_span_t *name);
int mxsd_parse_attribute(xml_parser_t *parser, text_span_t *name, text_span_t *value);
int mxsd_is_namespace_declaration(text_span_t name);
text_span_t mxsd_namespace_prefix(text_span_t declaration_name);
void mxsd_split_qualified_name(text_span_t qualified, text_span_t *prefix, text_span_t *local_name);
text_span_t mxsd_resolve_namespace(const xml_parser_t *parser, text_span_t prefix,
				   int is_attribute);
int mxsd_expanded_name_equals(const xml_parser_t *parser, text_span_t qualified_name,
			      const char *expected_local_name, const char *expected_namespace,
			      int is_attribute);
mxsd_result_t mxsd_parse_start_tag(xml_parser_t *parser, const char *path, start_tag_t *tag);
int mxsd_at_end_of_start_tag(const xml_parser_t *parser);
int mxsd_at_end_element(const xml_parser_t *parser);
mxsd_result_t mxsd_skip_xml_declaration(xml_parser_t *parser);
/**
 * @brief Skip one well-formed element, including its content and end tag.
 *
 * The parser must be positioned on the start tag. Namespace declarations of
 * the skipped element are released before returning.
 */
mxsd_result_t mxsd_skip_element(xml_parser_t *parser, const char *path);
mxsd_result_t mxsd_validate_identities(const mxsd_schema_t *schema, const char *xml,
				       const mxsd_allocator_t *allocator);
mxsd_result_t mxsd_dom_validate(const mxsd_schema_t *schema, const char *xml);
int mxsd_validate_boolean(text_span_t value);
int mxsd_decode_xml_references(text_span_t input, text_span_t *output, char *storage);
int mxsd_validate_simple_value(const xml_parser_t *parser, mxsd_type_t type,
			       const mxsd_simple_t *simple, text_span_t value);
int mxsd_simple_values_equal(const xml_parser_t *parser, mxsd_type_t type,
			     const mxsd_simple_t *simple, text_span_t left, const char *right_text);
int mxsd_complex_derives_from(const mxsd_complex_t *candidate, const mxsd_complex_t *base,
			      unsigned blocked);
int mxsd_simple_derives_from(const mxsd_simple_t *candidate, const mxsd_simple_t *base,
			     unsigned blocked);
int mxsd_global_substitutes_for(const xml_parser_t *parser, const mxsd_global_element_t *candidate,
				const mxsd_element_t *head);

/* Keep call sites compact while retaining namespaced link symbols. */
#define make_result              mxsd_make_result
#define span_equals_string       mxsd_span_equals_string
#define span_equals_nullable     mxsd_span_equals_nullable
#define skip_whitespace          mxsd_skip_whitespace
#define skip_ignorable_markup    mxsd_skip_ignorable_markup
#define parse_xml_name           mxsd_parse_xml_name
#define parse_attribute          mxsd_parse_attribute
#define is_namespace_declaration mxsd_is_namespace_declaration
#define namespace_prefix         mxsd_namespace_prefix
#define split_qualified_name     mxsd_split_qualified_name
#define resolve_namespace        mxsd_resolve_namespace
#define expanded_name_equals     mxsd_expanded_name_equals
#define parse_start_tag          mxsd_parse_start_tag
#define at_end_of_start_tag      mxsd_at_end_of_start_tag
#define at_end_element           mxsd_at_end_element
#define skip_xml_declaration     mxsd_skip_xml_declaration
#define skip_any_element         mxsd_skip_element
#define validate_identities      mxsd_validate_identities
#define validate_boolean         mxsd_validate_boolean
#define validate_simple_value    mxsd_validate_simple_value
#define simple_values_equal      mxsd_simple_values_equal
#define complex_derives_from     mxsd_complex_derives_from
#define simple_derives_from      mxsd_simple_derives_from

#endif /* MXSD_INTERNAL_H */
