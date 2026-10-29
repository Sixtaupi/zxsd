/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Private interfaces shared by the zxsd runtime modules.
 */

#ifndef ZXSD_INTERNAL_H
#define ZXSD_INTERNAL_H

#include "zxsd.h"

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
	namespace_binding_t namespaces[ZXSD_MAX_NAMESPACES];
	size_t namespace_count;
	const zxsd_global_element_t *global_elements;
	size_t global_element_count;
	const zxsd_global_attribute_t *global_attributes;
	size_t global_attribute_count;
	const zxsd_named_type_t *named_types;
	size_t named_type_count;
	const zxsd_notation_t *notations;
	size_t notation_count;
	int mixed_content;
	int values_are_decoded;
	text_span_t ids[ZXSD_MAX_IDENTITY_VALUES];
	size_t id_count;
	text_span_t idrefs[ZXSD_MAX_IDENTITY_VALUES];
	size_t idref_count;
	char identity_text[ZXSD_MAX_IDENTITY_TEXT];
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

zxsd_result_t zxsd_make_result(zxsd_error_t code, size_t offset, const char *path);
int zxsd_span_equals_string(text_span_t span, const char *string);
int zxsd_span_equals_nullable(text_span_t span, const char *text);
void zxsd_skip_whitespace(xml_parser_t *parser);
void zxsd_skip_ignorable_markup(xml_parser_t *parser);
int zxsd_parse_xml_name(xml_parser_t *parser, text_span_t *name);
int zxsd_parse_attribute(xml_parser_t *parser, text_span_t *name, text_span_t *value);
int zxsd_is_namespace_declaration(text_span_t name);
text_span_t zxsd_namespace_prefix(text_span_t declaration_name);
void zxsd_split_qualified_name(text_span_t qualified, text_span_t *prefix, text_span_t *local_name);
text_span_t zxsd_resolve_namespace(const xml_parser_t *parser, text_span_t prefix,
				   int is_attribute);
int zxsd_expanded_name_equals(const xml_parser_t *parser, text_span_t qualified_name,
			      const char *expected_local_name, const char *expected_namespace,
			      int is_attribute);
zxsd_result_t zxsd_parse_start_tag(xml_parser_t *parser, const char *path, start_tag_t *tag);
int zxsd_at_end_of_start_tag(const xml_parser_t *parser);
int zxsd_at_end_element(const xml_parser_t *parser);
zxsd_result_t zxsd_skip_xml_declaration(xml_parser_t *parser);
/**
 * @brief Skip one well-formed element, including its content and end tag.
 *
 * The parser must be positioned on the start tag. Namespace declarations of
 * the skipped element are released before returning.
 */
zxsd_result_t zxsd_skip_element(xml_parser_t *parser, const char *path);
zxsd_result_t zxsd_validate_identities(const zxsd_schema_t *schema, const char *xml,
				       const zxsd_allocator_t *allocator);
zxsd_result_t zxsd_dom_validate(const zxsd_schema_t *schema, const char *xml);
int zxsd_validate_boolean(text_span_t value);
int zxsd_decode_xml_references(text_span_t input, text_span_t *output, char *storage);
int zxsd_validate_simple_value(const xml_parser_t *parser, zxsd_type_t type,
			       const zxsd_simple_t *simple, text_span_t value);

/** Check whether the shared-pool interpreter supports a particle tree. */
int zxsd_model_supported(const zxsd_particle_t *particle);
/** Initialize a nested model after effective-type resolution. */
zxsd_error_t zxsd_model_init(zxsd_stream_t *stream, zxsd_stream_frame_t *frame);
/** Advance all viable repetition partitions by one leaf token. */
zxsd_error_t zxsd_model_consume(zxsd_stream_t *stream, zxsd_stream_frame_t *frame,
				const zxsd_particle_t *leaf);
/** Check whether a nested model accepts the current end of content. */
zxsd_error_t zxsd_model_finish(zxsd_stream_t *stream, zxsd_stream_frame_t *frame);
/** Release a frame's shared-pool frontier after its closing event. */
void zxsd_model_release(zxsd_stream_t *stream, zxsd_stream_frame_t *frame);
int zxsd_simple_values_equal(const xml_parser_t *parser, zxsd_type_t type,
			     const zxsd_simple_t *simple, text_span_t left, const char *right_text);
int zxsd_complex_derives_from(const zxsd_complex_t *candidate, const zxsd_complex_t *base,
			      unsigned blocked);
int zxsd_simple_derives_from(const zxsd_simple_t *candidate, const zxsd_simple_t *base,
			     unsigned blocked);
int zxsd_global_substitutes_for(const xml_parser_t *parser, const zxsd_global_element_t *candidate,
				const zxsd_element_t *head);

/* Keep call sites compact while retaining namespaced link symbols. */
#define make_result              zxsd_make_result
#define span_equals_string       zxsd_span_equals_string
#define span_equals_nullable     zxsd_span_equals_nullable
#define skip_whitespace          zxsd_skip_whitespace
#define skip_ignorable_markup    zxsd_skip_ignorable_markup
#define parse_xml_name           zxsd_parse_xml_name
#define parse_attribute          zxsd_parse_attribute
#define is_namespace_declaration zxsd_is_namespace_declaration
#define namespace_prefix         zxsd_namespace_prefix
#define split_qualified_name     zxsd_split_qualified_name
#define resolve_namespace        zxsd_resolve_namespace
#define expanded_name_equals     zxsd_expanded_name_equals
#define parse_start_tag          zxsd_parse_start_tag
#define at_end_of_start_tag      zxsd_at_end_of_start_tag
#define at_end_element           zxsd_at_end_element
#define skip_xml_declaration     zxsd_skip_xml_declaration
#define skip_any_element         zxsd_skip_element
#define validate_identities      zxsd_validate_identities
#define validate_boolean         zxsd_validate_boolean
#define validate_simple_value    zxsd_validate_simple_value
#define simple_values_equal      zxsd_simple_values_equal
#define complex_derives_from     zxsd_complex_derives_from
#define simple_derives_from      zxsd_simple_derives_from

#endif /* ZXSD_INTERNAL_H */
