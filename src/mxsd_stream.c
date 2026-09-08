/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Bounded event-driven XML Schema validation.
 */

#include "mxsd_internal.h"

#include <ctype.h>
#include <string.h>

/** Normalize the empty namespace URI to the descriptor's NULL convention. */
static const char *stream_ns(const char *namespace_uri)
{
	return namespace_uri != NULL && namespace_uri[0] != '\0' ? namespace_uri : NULL;
}

/** Compare two expanded XML names. */
static int stream_name_equal(const char *left_name, const char *left_ns, const char *right_name,
			     const char *right_ns)
{
	left_ns = stream_ns(left_ns);
	right_ns = stream_ns(right_ns);
	return left_name != NULL && right_name != NULL && strcmp(left_name, right_name) == 0 &&
	       ((left_ns == NULL && right_ns == NULL) ||
		(left_ns != NULL && right_ns != NULL && strcmp(left_ns, right_ns) == 0));
}

/** Store and return the first streaming failure. */
static mxsd_result_t stream_fail(mxsd_stream_t *stream, mxsd_error_t code, const char *path)
{
	if (stream->result.code == MXSD_OK) {
		stream->result = make_result(code, stream->event_offset, path);
	}
	return stream->result;
}

/** Copy a name into fixed streaming storage. */
static int stream_copy_name(char *destination, const char *source)
{
	size_t length = source == NULL ? 0u : strlen(source);
	if (length >= MXSD_STREAM_NAME_CAPACITY) {
		return 0;
	}
	if (length != 0u) {
		memcpy(destination, source, length);
	}
	destination[length] = '\0';
	return 1;
}

/** Find a global declaration by expanded name. */
static const mxsd_global_element_t *stream_global_element(const mxsd_schema_t *schema,
							  const char *namespace_uri,
							  const char *local_name)
{
	size_t index;
	for (index = 0; index < schema->global_element_count; index++) {
		const mxsd_global_element_t *item = &schema->global_elements[index];
		if (stream_name_equal(local_name, namespace_uri, item->name, item->namespace_uri)) {
			return item;
		}
	}
	return NULL;
}

/** Test whether a global declaration may replace a particle head. */
static int stream_is_substitute(const mxsd_global_element_t *candidate, const mxsd_element_t *head)
{
	return candidate != NULL && head != NULL &&
	       !(head->block_methods & MXSD_DERIVATION_SUBSTITUTION) &&
	       stream_name_equal(candidate->substitution_head_name,
				 candidate->substitution_head_namespace, head->name,
				 head->namespace_uri);
}

/** Test a namespace URI against a compiled wildcard constraint. */
static int stream_wildcard_namespace(const mxsd_wildcard_t *wildcard, const char *namespace_uri)
{
	const char *cursor;
	namespace_uri = stream_ns(namespace_uri);
	if (wildcard == NULL || wildcard->namespace_constraint == NULL) {
		return 0;
	}
	if (!strcmp(wildcard->namespace_constraint, "##any")) {
		return 1;
	}
	if (!strcmp(wildcard->namespace_constraint, "##local")) {
		return namespace_uri == NULL;
	}
	if (!strcmp(wildcard->namespace_constraint, "##targetNamespace")) {
		return stream_name_equal("x", namespace_uri, "x", wildcard->target_namespace);
	}
	if (!strcmp(wildcard->namespace_constraint, "##other")) {
		return namespace_uri != NULL &&
		       !stream_name_equal("x", namespace_uri, "x", wildcard->target_namespace);
	}
	cursor = wildcard->namespace_constraint;
	while (*cursor != '\0') {
		const char *end;
		size_t length;
		while (*cursor == ' ') {
			cursor++;
		}
		end = strchr(cursor, ' ');
		length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
		if (namespace_uri != NULL && strlen(namespace_uri) == length &&
		    !memcmp(cursor, namespace_uri, length)) {
			return 1;
		}
		cursor = end == NULL ? cursor + length : end + 1;
	}
	return 0;
}

/** Find a matching wildcard in a recursive content model. */
static const mxsd_wildcard_t *stream_find_wildcard(const mxsd_particle_t *particle,
						   const char *namespace_uri)
{
	size_t index;
	const mxsd_wildcard_t *found;
	if (particle == NULL) {
		return NULL;
	}
	if (particle->kind == MXSD_PARTICLE_ANY &&
	    stream_wildcard_namespace(particle->wildcard, namespace_uri)) {
		return particle->wildcard;
	}
	for (index = 0; index < particle->child_count; index++) {
		found = stream_find_wildcard(&particle->children[index], namespace_uri);
		if (found != NULL) {
			return found;
		}
	}
	return NULL;
}

/** Consume one child against the bounded flat content-model cursor. */
static const mxsd_element_t *stream_match_child(mxsd_stream_t *stream, mxsd_stream_frame_t *frame,
						const char *namespace_uri, const char *local_name,
						const mxsd_global_element_t **substitute)
{
	const mxsd_complex_t *complex = frame->complex_type;
	size_t index, begin = 0u, end;
	*substitute = NULL;
	if (complex == NULL || complex->element_count > MXSD_MAX_PARTICLES) {
		return NULL;
	}
	end = complex->element_count;
	if (complex->model == MXSD_SEQUENCE) {
		begin = frame->sequence_index;
	}
	for (index = begin; index < end; index++) {
		const mxsd_element_t *element = &complex->elements[index];
		const mxsd_global_element_t *global =
			stream_global_element(stream->schema, namespace_uri, local_name);
		int matches = stream_name_equal(local_name, namespace_uri, element->name,
						element->namespace_uri);
		if (!matches && stream_is_substitute(global, element)) {
			matches = 1;
			*substitute = global;
		}
		if (matches && frame->child_counts[index] < element->max_occurs) {
			if (complex->model == MXSD_SEQUENCE) {
				size_t skipped;
				for (skipped = frame->sequence_index; skipped < index; skipped++) {
					if (frame->child_counts[skipped] <
					    complex->elements[skipped].min_occurs) {
						return NULL;
					}
				}
				frame->sequence_index = index;
			}
			frame->child_counts[index]++;
			return element;
		}
		if (complex->model == MXSD_SEQUENCE && index == frame->sequence_index &&
		    frame->child_counts[index] < element->min_occurs) {
			return NULL;
		}
	}
	return NULL;
}

/** Populate a new frame from a local or substituted declaration. */
static void stream_set_element(mxsd_stream_frame_t *frame, const mxsd_element_t *element,
			       const mxsd_global_element_t *global)
{
	if (global != NULL) {
		frame->expected_name = global->name;
		frame->expected_namespace = global->namespace_uri;
		frame->type = global->type;
		frame->complex_type = global->complex_type;
		frame->simple_type = global->simple_type;
		frame->default_value = global->default_value;
		frame->fixed_value = global->fixed_value;
		frame->nillable = global->nillable;
	} else {
		frame->expected_name = element->name;
		frame->expected_namespace = element->namespace_uri;
		frame->type = element->type;
		frame->complex_type = element->complex_type;
		frame->simple_type = element->simple_type;
		frame->default_value = element->default_value;
		frame->fixed_value = element->fixed_value;
		frame->nillable = element->nillable;
	}
}

mxsd_result_t mxsd_stream_init(mxsd_stream_t *stream, const mxsd_schema_t *schema)
{
	if (stream == NULL || schema == NULL || schema->root_name == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	memset(stream, 0, sizeof(*stream));
	stream->schema = schema;
	stream->result = make_result(MXSD_OK, 0, NULL);
	return stream->result;
}

mxsd_result_t mxsd_start_content(mxsd_stream_t *stream)
{
	mxsd_stream_frame_t *frame;
	size_t index;
	if (stream == NULL || stream->schema == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (stream->result.code != MXSD_OK) {
		return stream->result;
	}
	if (stream->depth == 0u) {
		return stream_fail(stream, MXSD_ERR_ARGUMENT, NULL);
	}
	frame = &stream->frames[stream->depth - 1u];
	if (frame->content_started) {
		return stream->result;
	}
	if (!frame->skip_validation && frame->type == MXSD_T_COMPLEX) {
		if (frame->complex_type == NULL || frame->complex_type->abstract_type) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, frame->expected_name);
		}
		for (index = 0; index < frame->complex_type->attribute_count; index++) {
			if (frame->complex_type->attributes[index].required &&
			    !frame->seen_attributes[index]) {
				return stream_fail(stream, MXSD_ERR_MISSING_ATTRIBUTE,
						   frame->expected_name);
			}
		}
	}
	frame->content_started = 1;
	return stream->result;
}

mxsd_result_t mxsd_start_element(mxsd_stream_t *stream, const char *namespace_uri,
				 const char *local_name)
{
	mxsd_stream_frame_t *frame;
	if (stream == NULL || stream->schema == NULL || local_name == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (stream->result.code != MXSD_OK) {
		return stream->result;
	}
	stream->event_offset++;
	if (stream->finished || stream->depth >= MXSD_MAX_DEPTH) {
		return stream_fail(stream,
				   stream->finished ? MXSD_ERR_MALFORMED_XML
						    : MXSD_ERR_EXCESSIVE_NESTING,
				   local_name);
	}
	if (stream->depth != 0u) {
		mxsd_stream_frame_t *parent = &stream->frames[stream->depth - 1u];
		const mxsd_element_t *element;
		const mxsd_global_element_t *substitute;
		mxsd_result_t result = mxsd_start_content(stream);
		if (result.code != MXSD_OK) {
			return result;
		}
		if (parent->nilled) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, parent->expected_name);
		}
		parent->child_count++;
		frame = &stream->frames[stream->depth];
		memset(frame, 0, sizeof(*frame));
		if (parent->skip_validation) {
			frame->skip_validation = 1;
		} else if (parent->type != MXSD_T_COMPLEX || parent->complex_type == NULL ||
			   parent->complex_type->has_simple_content) {
			return stream_fail(stream, MXSD_ERR_UNEXPECTED_ELEMENT, local_name);
		} else {
			element = stream_match_child(stream, parent, namespace_uri, local_name,
						     &substitute);
			if (element == NULL) {
				const mxsd_wildcard_t *wildcard = stream_find_wildcard(
					parent->complex_type->particle, namespace_uri);
				substitute = stream_global_element(stream->schema, namespace_uri,
								   local_name);
				if (wildcard == NULL ||
				    (wildcard->process_contents == MXSD_PROCESS_STRICT &&
				     substitute == NULL)) {
					return stream_fail(stream, MXSD_ERR_UNEXPECTED_ELEMENT,
							   local_name);
				}
				if (substitute == NULL ||
				    wildcard->process_contents == MXSD_PROCESS_SKIP) {
					frame->skip_validation = 1;
				} else {
					frame->expected_name = substitute->name;
					frame->expected_namespace = substitute->namespace_uri;
					frame->type = substitute->type;
					frame->complex_type = substitute->complex_type;
					frame->simple_type = substitute->simple_type;
					frame->default_value = substitute->default_value;
					frame->fixed_value = substitute->fixed_value;
					frame->nillable = substitute->nillable;
				}
			} else {
				stream_set_element(frame, element, substitute);
				if ((substitute == NULL && element->abstract_element) ||
				    (substitute != NULL && substitute->abstract_element)) {
					return stream_fail(stream, MXSD_ERR_INVALID_VALUE,
							   local_name);
				}
			}
		}
	} else {
		frame = &stream->frames[0];
		memset(frame, 0, sizeof(*frame));
		if (!stream_name_equal(local_name, namespace_uri, stream->schema->root_name,
				       stream->schema->root_namespace_uri)) {
			return stream_fail(stream, MXSD_ERR_WRONG_ROOT, stream->schema->root_name);
		}
		frame->expected_name = stream->schema->root_name;
		frame->expected_namespace = stream->schema->root_namespace_uri;
		frame->type = stream->schema->root_type;
		frame->complex_type = stream->schema->root_complex;
		frame->simple_type = stream->schema->root_simple;
		frame->default_value = stream->schema->default_value;
		frame->fixed_value = stream->schema->fixed_value;
		frame->nillable = stream->schema->nillable;
		if (stream->schema->abstract_element) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, frame->expected_name);
		}
	}
	if (!stream_copy_name(frame->local_name, local_name) ||
	    !stream_copy_name(frame->namespace_uri, stream_ns(namespace_uri))) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, local_name);
	}
	stream->depth++;
	return stream->result;
}

mxsd_result_t mxsd_attribute(mxsd_stream_t *stream, const char *namespace_uri,
			     const char *local_name, const char *value)
{
	static const char xsi[] = "http://www.w3.org/2001/XMLSchema-instance";
	mxsd_stream_frame_t *frame;
	xml_parser_t parser;
	text_span_t span;
	size_t index;
	if (stream == NULL || local_name == NULL || value == NULL || stream->depth == 0u) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (stream->result.code != MXSD_OK) {
		return stream->result;
	}
	stream->event_offset++;
	frame = &stream->frames[stream->depth - 1u];
	if (frame->content_started) {
		return stream_fail(stream, MXSD_ERR_MALFORMED_XML, local_name);
	}
	if (frame->skip_validation) {
		return stream->result;
	}
	if (stream_name_equal(local_name, namespace_uri, "nil", xsi)) {
		if (!strcmp(value, "true") || !strcmp(value, "1")) {
			frame->nilled = 1;
		} else if (strcmp(value, "false") && strcmp(value, "0")) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, local_name);
		}
		return stream->result;
	}
	if (stream_name_equal(local_name, namespace_uri, "type", xsi)) {
		return stream_fail(stream, MXSD_ERR_ARGUMENT, "use mxsd_xsi_type");
	}
	if (frame->type != MXSD_T_COMPLEX || frame->complex_type == NULL) {
		return stream_fail(stream, MXSD_ERR_UNEXPECTED_ATTRIBUTE, local_name);
	}
	memset(&parser, 0, sizeof(parser));
	span.data = value;
	span.length = strlen(value);
	for (index = 0; index < frame->complex_type->attribute_count; index++) {
		const mxsd_attribute_t *attribute = &frame->complex_type->attributes[index];
		if (!stream_name_equal(local_name, namespace_uri, attribute->name,
				       attribute->namespace_uri)) {
			continue;
		}
		if (frame->seen_attributes[index]) {
			return stream_fail(stream, MXSD_ERR_MALFORMED_XML, local_name);
		}
		frame->seen_attributes[index] = 1;
		if (!validate_simple_value(&parser, attribute->type, attribute->simple_type,
					   span) ||
		    (attribute->fixed_value != NULL &&
		     !simple_values_equal(&parser, attribute->type, attribute->simple_type, span,
					  attribute->fixed_value))) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, local_name);
		}
		return stream->result;
	}
	if (frame->complex_type->any_attribute != NULL &&
	    stream_wildcard_namespace(frame->complex_type->any_attribute, namespace_uri)) {
		const mxsd_global_attribute_t *global = NULL;
		for (index = 0; index < stream->schema->global_attribute_count; index++) {
			const mxsd_global_attribute_t *candidate =
				&stream->schema->global_attributes[index];
			if (stream_name_equal(local_name, namespace_uri, candidate->name,
					      candidate->namespace_uri)) {
				global = candidate;
				break;
			}
		}
		if (frame->complex_type->any_attribute->process_contents == MXSD_PROCESS_SKIP ||
		    (global == NULL &&
		     frame->complex_type->any_attribute->process_contents == MXSD_PROCESS_LAX)) {
			return stream->result;
		}
		if (global != NULL &&
		    validate_simple_value(&parser, global->type, global->simple_type, span)) {
			return stream->result;
		}
	}
	return stream_fail(stream, MXSD_ERR_UNEXPECTED_ATTRIBUTE, local_name);
}

mxsd_result_t mxsd_xsi_type(mxsd_stream_t *stream, const char *type_namespace_uri,
			    const char *type_local_name)
{
	mxsd_stream_frame_t *frame;
	const mxsd_named_type_t *dynamic = NULL;
	size_t index;
	if (stream == NULL || type_local_name == NULL || stream->depth == 0u) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (stream->result.code != MXSD_OK) {
		return stream->result;
	}
	frame = &stream->frames[stream->depth - 1u];
	if (frame->content_started) {
		return stream_fail(stream, MXSD_ERR_MALFORMED_XML, type_local_name);
	}
	for (index = 0; index < stream->schema->named_type_count; index++) {
		const mxsd_named_type_t *candidate = &stream->schema->named_types[index];
		if (stream_name_equal(type_local_name, type_namespace_uri, candidate->name,
				      candidate->namespace_uri)) {
			dynamic = candidate;
			break;
		}
	}
	if (dynamic == NULL) {
		return stream_fail(stream, MXSD_ERR_INVALID_VALUE, type_local_name);
	}
	if (dynamic->complex_type != NULL) {
		if (frame->type != MXSD_T_COMPLEX ||
		    !complex_derives_from(dynamic->complex_type, frame->complex_type, 0u) ||
		    dynamic->complex_type->abstract_type) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, type_local_name);
		}
		frame->complex_type = dynamic->complex_type;
	} else {
		if (frame->type == MXSD_T_COMPLEX ||
		    !simple_derives_from(dynamic->simple_type, frame->simple_type, 0u)) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, type_local_name);
		}
		frame->type = dynamic->type;
		frame->simple_type = dynamic->simple_type;
	}
	return stream->result;
}

mxsd_result_t mxsd_text(mxsd_stream_t *stream, const char *text, size_t length)
{
	mxsd_stream_frame_t *frame;
	size_t index;
	mxsd_result_t result;
	if (stream == NULL || text == NULL || stream->depth == 0u) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (stream->result.code != MXSD_OK) {
		return stream->result;
	}
	stream->event_offset++;
	result = mxsd_start_content(stream);
	if (result.code != MXSD_OK) {
		return result;
	}
	frame = &stream->frames[stream->depth - 1u];
	if (frame->skip_validation) {
		return stream->result;
	}
	if (frame->nilled) {
		for (index = 0; index < length; index++) {
			if (!isspace((unsigned char)text[index])) {
				return stream_fail(stream, MXSD_ERR_INVALID_VALUE,
						   frame->expected_name);
			}
		}
		return stream->result;
	}
	if (frame->type == MXSD_T_COMPLEX && frame->complex_type != NULL &&
	    !frame->complex_type->has_simple_content) {
		for (index = 0; index < length; index++) {
			if (!isspace((unsigned char)text[index])) {
				return stream_fail(stream, MXSD_ERR_TEXT_NOT_ALLOWED,
						   frame->expected_name);
			}
		}
		return stream->result;
	}
	if (length > MXSD_STREAM_TEXT_CAPACITY - frame->text_length) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, frame->expected_name);
	}
	memcpy(frame->text + frame->text_length, text, length);
	frame->text_length += length;
	frame->text[frame->text_length] = '\0';
	return stream->result;
}

mxsd_result_t mxsd_end_element(mxsd_stream_t *stream, const char *namespace_uri,
			       const char *local_name)
{
	mxsd_stream_frame_t *frame;
	xml_parser_t parser;
	text_span_t value;
	size_t index;
	mxsd_result_t result;
	if (stream == NULL || local_name == NULL || stream->depth == 0u) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (stream->result.code != MXSD_OK) {
		return stream->result;
	}
	stream->event_offset++;
	result = mxsd_start_content(stream);
	if (result.code != MXSD_OK) {
		return result;
	}
	frame = &stream->frames[stream->depth - 1u];
	if (!stream_name_equal(local_name, namespace_uri, frame->local_name,
			       frame->namespace_uri)) {
		return stream_fail(stream, MXSD_ERR_MALFORMED_XML, local_name);
	}
	if (!frame->skip_validation && frame->nilled && !frame->nillable) {
		return stream_fail(stream, MXSD_ERR_INVALID_VALUE, frame->expected_name);
	}
	if (!frame->skip_validation && !frame->nilled && frame->type == MXSD_T_COMPLEX &&
	    frame->complex_type != NULL && !frame->complex_type->has_simple_content) {
		if (frame->complex_type->model == MXSD_CHOICE) {
			if (frame->child_count < frame->complex_type->min_occurs ||
			    frame->child_count > frame->complex_type->max_occurs) {
				return stream_fail(stream, MXSD_ERR_MISSING_ELEMENT,
						   frame->expected_name);
			}
		} else {
			for (index = 0; index < frame->complex_type->element_count; index++) {
				if (frame->child_counts[index] <
				    frame->complex_type->elements[index].min_occurs) {
					return stream_fail(stream, MXSD_ERR_MISSING_ELEMENT,
							   frame->expected_name);
				}
			}
		}
	} else if (!frame->skip_validation && !frame->nilled) {
		const mxsd_simple_t *simple = frame->simple_type;
		mxsd_type_t type = frame->type;
		const char *effective = frame->text;
		if (frame->type == MXSD_T_COMPLEX && frame->complex_type != NULL) {
			simple = frame->complex_type->simple_content;
			type = frame->complex_type->simple_content_type;
		}
		if (frame->text_length == 0u && frame->default_value != NULL) {
			effective = frame->default_value;
		}
		memset(&parser, 0, sizeof(parser));
		value.data = effective;
		value.length = strlen(effective);
		if (!validate_simple_value(&parser, type, simple, value) ||
		    (frame->fixed_value != NULL &&
		     !simple_values_equal(&parser, type, simple, value, frame->fixed_value))) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, frame->expected_name);
		}
	}
	stream->depth--;
	if (stream->depth == 0u) {
		stream->finished = 1;
	}
	return stream->result;
}

mxsd_result_t mxsd_stream_finish(mxsd_stream_t *stream)
{
	if (stream == NULL || stream->schema == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (stream->result.code != MXSD_OK) {
		return stream->result;
	}
	if (stream->depth != 0u || !stream->finished) {
		return stream_fail(stream, MXSD_ERR_MALFORMED_XML, NULL);
	}
	return stream->result;
}
