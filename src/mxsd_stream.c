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
#include <limits.h>
#include <stdio.h>
#ifndef MXSD_NO_HEAP
#include <stdlib.h>
#endif
#include <string.h>

static const char *stream_ns(const char *namespace_uri);

/** Release the optional XML copy retained for streamed identity constraints. */
static void stream_release_identity(mxsd_stream_t *stream)
{
	if (stream->identity_xml != NULL && stream->identity_allocator.deallocate != NULL) {
		stream->identity_allocator.deallocate(stream->identity_allocator.context,
						      stream->identity_xml);
	}
	stream->identity_xml = NULL;
	stream->identity_xml_length = 0u;
	stream->identity_xml_capacity = 0u;
}

/** Append bytes to the bounded identity XML copy. */
static int stream_xml_append(mxsd_stream_t *stream, const char *text, size_t length)
{
	if (stream->identity_xml == NULL) {
		return 1;
	}
	if (length > stream->identity_xml_capacity - stream->identity_xml_length - 1u) {
		return 0;
	}
	memcpy(stream->identity_xml + stream->identity_xml_length, text, length);
	stream->identity_xml_length += length;
	stream->identity_xml[stream->identity_xml_length] = '\0';
	return 1;
}

/** Append one NUL-terminated string to the identity XML copy. */
static int stream_xml_string(mxsd_stream_t *stream, const char *text)
{
	return stream_xml_append(stream, text, strlen(text));
}

/** Append decoded XML character data or an attribute value with escaping. */
static int stream_xml_escaped(mxsd_stream_t *stream, const char *text, size_t length, int attribute)
{
	size_t start = 0u;
	size_t index;
	for (index = 0u; index < length; ++index) {
		const char *replacement = NULL;
		if (text[index] == '&') {
			replacement = "&amp;";
		} else if (text[index] == '<') {
			replacement = "&lt;";
		} else if (attribute && text[index] == '\'') {
			replacement = "&apos;";
		}
		if (replacement != NULL) {
			if (!stream_xml_append(stream, text + start, index - start) ||
			    !stream_xml_string(stream, replacement)) {
				return 0;
			}
			start = index + 1u;
		}
	}
	return stream_xml_append(stream, text + start, length - start);
}

/** Append an expanded element start using a private synthetic prefix. */
static int stream_xml_start_element(mxsd_stream_t *stream, const char *namespace_uri,
				    const char *local_name)
{
	char prefix[32];
	if (!stream_xml_string(stream, "<")) {
		return 0;
	}
	if (stream_ns(namespace_uri) != NULL) {
		(void)snprintf(prefix, sizeof(prefix), "_mxsde%lu",
			       (unsigned long)(stream->depth - 1u));
		return stream_xml_string(stream, prefix) && stream_xml_string(stream, ":") &&
		       stream_xml_string(stream, local_name) &&
		       stream_xml_string(stream, " xmlns:") && stream_xml_string(stream, prefix) &&
		       stream_xml_string(stream, "='") &&
		       stream_xml_escaped(stream, namespace_uri, strlen(namespace_uri), 1) &&
		       stream_xml_string(stream, "'");
	}
	return stream_xml_string(stream, local_name);
}

/** Append an expanded attribute using a unique synthetic prefix. */
static int stream_xml_attribute(mxsd_stream_t *stream, const char *namespace_uri,
				const char *local_name, const char *value)
{
	char prefix[32];
	(void)snprintf(prefix, sizeof(prefix), "_mxsda%lu", (unsigned long)stream->event_offset);
	if (!stream_xml_string(stream, " ")) {
		return 0;
	}
	if (stream_ns(namespace_uri) != NULL &&
	    (!stream_xml_string(stream, "xmlns:") || !stream_xml_string(stream, prefix) ||
	     !stream_xml_string(stream, "='") ||
	     !stream_xml_escaped(stream, namespace_uri, strlen(namespace_uri), 1) ||
	     !stream_xml_string(stream, "' ") || !stream_xml_string(stream, prefix) ||
	     !stream_xml_string(stream, ":"))) {
		return 0;
	}
	return stream_xml_string(stream, local_name) && stream_xml_string(stream, "='") &&
	       stream_xml_escaped(stream, value, strlen(value), 1) &&
	       stream_xml_string(stream, "'");
}

/** Append an already-expanded xsi:type with synthetic namespace bindings. */
static int stream_xml_xsi_type(mxsd_stream_t *stream, const char *namespace_uri,
			       const char *local_name)
{
	char xsi_prefix[32];
	char type_prefix[32];
	(void)snprintf(xsi_prefix, sizeof(xsi_prefix), "_mxsdx%lu",
		       (unsigned long)stream->event_offset);
	(void)snprintf(type_prefix, sizeof(type_prefix), "_mxsdt%lu",
		       (unsigned long)stream->event_offset);
	if (!stream_xml_string(stream, " xmlns:") || !stream_xml_string(stream, xsi_prefix) ||
	    !stream_xml_string(stream, "='http://www.w3.org/2001/XMLSchema-instance' ") ||
	    !stream_xml_string(stream, xsi_prefix) || !stream_xml_string(stream, ":type='")) {
		return 0;
	}
	if (stream_ns(namespace_uri) != NULL) {
		if (!stream_xml_string(stream, type_prefix) || !stream_xml_string(stream, ":") ||
		    !stream_xml_string(stream, local_name) ||
		    !stream_xml_string(stream, "' xmlns:") ||
		    !stream_xml_string(stream, type_prefix) || !stream_xml_string(stream, "='") ||
		    !stream_xml_escaped(stream, namespace_uri, strlen(namespace_uri), 1)) {
			return 0;
		}
	} else if (!stream_xml_string(stream, local_name)) {
		return 0;
	}
	return stream_xml_string(stream, "'");
}

/** Normalize the empty namespace URI to the descriptor's NULL convention. */
static const char *stream_ns(const char *namespace_uri)
{
	return namespace_uri != NULL && namespace_uri[0] != '\0' ? namespace_uri : NULL;
}

/** Populate the value validator with active streamed namespace bindings. */
static void stream_prepare_parser(const mxsd_stream_t *stream, xml_parser_t *parser)
{
	size_t index;
	memset(parser, 0, sizeof(*parser));
	parser->values_are_decoded = 1;
	parser->notations = stream->schema->notations;
	parser->notation_count = stream->schema->notation_count;
	for (index = 0u; index < stream->namespace_count; ++index) {
		parser->namespaces[index].prefix.data = stream->namespaces[index].prefix;
		parser->namespaces[index].prefix.length = strlen(stream->namespaces[index].prefix);
		parser->namespaces[index].uri.data = stream->namespaces[index].uri;
		parser->namespaces[index].uri.length = strlen(stream->namespaces[index].uri);
	}
	parser->namespace_count = stream->namespace_count;
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

/** Return whether a simple descriptor represents the XSD simple ur-type. */
static int stream_is_any_simple_type(const mxsd_simple_t *simple)
{
	static const char xsd[] = "http://www.w3.org/2001/XMLSchema";
	return simple != NULL && simple->type_name != NULL && simple->type_namespace != NULL &&
	       strcmp(simple->type_name, "anySimpleType") == 0 &&
	       strcmp(simple->type_namespace, xsd) == 0;
}

/** Store and return the first streaming failure. */
static mxsd_result_t stream_fail(mxsd_stream_t *stream, mxsd_error_t code, const char *path)
{
	if (stream->result.code == MXSD_OK) {
		stream->result = make_result(code, stream->event_offset, path);
		stream_release_identity(stream);
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
static int stream_is_substitute(const mxsd_stream_t *stream, const mxsd_global_element_t *candidate,
				const mxsd_element_t *head)
{
	xml_parser_t parser;

	if (candidate == NULL || head == NULL) {
		return 0;
	}
	memset(&parser, 0, sizeof(parser));
	parser.global_elements = stream->schema->global_elements;
	parser.global_element_count = stream->schema->global_element_count;
	return mxsd_global_substitutes_for(&parser, candidate, head);
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

/** Find the leaf declaration responsible for one expanded child name. */
static const mxsd_particle_t *stream_find_leaf(mxsd_stream_t *stream,
					       const mxsd_particle_t *particle,
					       const char *namespace_uri, const char *local_name,
					       const mxsd_global_element_t **substitute)
{
	size_t index;

	if (particle == NULL) {
		return NULL;
	}
	if (particle->kind == MXSD_PARTICLE_ELEMENT) {
		const mxsd_global_element_t *global =
			stream_global_element(stream->schema, namespace_uri, local_name);

		if (stream_name_equal(local_name, namespace_uri, particle->element->name,
				      particle->element->namespace_uri)) {
			*substitute = NULL;
			return particle;
		}
		if (stream_is_substitute(stream, global, particle->element)) {
			*substitute = global;
			return particle;
		}
		return NULL;
	}
	if (particle->kind == MXSD_PARTICLE_ANY) {
		if (stream_wildcard_namespace(particle->wildcard, namespace_uri)) {
			*substitute =
				stream_global_element(stream->schema, namespace_uri, local_name);
			return particle;
		}
		return NULL;
	}
	for (index = 0u; index < particle->child_count; ++index) {
		const mxsd_particle_t *found = stream_find_leaf(
			stream, &particle->children[index], namespace_uri, local_name, substitute);

		if (found != NULL) {
			return found;
		}
	}
	return NULL;
}

static int stream_particle_accepts_empty(const mxsd_particle_t *particle)
{
	size_t index;

	if (particle->min_occurs == 0u) {
		return 1;
	}
	if (particle->kind == MXSD_PARTICLE_ELEMENT || particle->kind == MXSD_PARTICLE_ANY) {
		return 0;
	}
	if (particle->kind == MXSD_PARTICLE_CHOICE) {
		for (index = 0u; index < particle->child_count; ++index) {
			if (stream_particle_accepts_empty(&particle->children[index])) {
				return 1;
			}
		}
		return 0;
	}
	for (index = 0u; index < particle->child_count; ++index) {
		if (!stream_particle_accepts_empty(&particle->children[index])) {
			return 0;
		}
	}
	return 1;
}

static int stream_particle_can_start(const mxsd_particle_t *particle, const mxsd_particle_t *token)
{
	size_t index;

	if (token == NULL) {
		return 0;
	}
	if (particle->kind == MXSD_PARTICLE_ELEMENT || particle->kind == MXSD_PARTICLE_ANY) {
		return particle == token;
	}
	for (index = 0u; index < particle->child_count; ++index) {
		const mxsd_particle_t *child = &particle->children[index];

		if (stream_particle_can_start(child, token)) {
			return 1;
		}
		if (particle->kind == MXSD_PARTICLE_SEQUENCE &&
		    !stream_particle_accepts_empty(child)) {
			break;
		}
	}
	return 0;
}

static const mxsd_particle_t *stream_homogeneous_leaf(const mxsd_particle_t *particle)
{
	const mxsd_particle_t *leaf = NULL;
	size_t index;

	if (particle->kind == MXSD_PARTICLE_ELEMENT || particle->kind == MXSD_PARTICLE_ANY) {
		return particle;
	}
	for (index = 0u; index < particle->child_count; ++index) {
		const mxsd_particle_t *child = stream_homogeneous_leaf(&particle->children[index]);

		if (child == NULL || (leaf != NULL && leaf != child)) {
			return NULL;
		}
		leaf = child;
	}
	return leaf;
}

static unsigned long stream_particle_one_maximum(const mxsd_particle_t *particle)
{
	unsigned long result = 0u;
	size_t index;

	if (particle->kind == MXSD_PARTICLE_ELEMENT || particle->kind == MXSD_PARTICLE_ANY) {
		return 1u;
	}
	for (index = 0u; index < particle->child_count; ++index) {
		const mxsd_particle_t *child = &particle->children[index];
		unsigned long maximum = stream_particle_one_maximum(child);

		if (child->max_occurs == ULONG_MAX || maximum == ULONG_MAX ||
		    maximum > ULONG_MAX / child->max_occurs) {
			maximum = ULONG_MAX;
		} else {
			maximum *= child->max_occurs;
		}
		if (particle->kind == MXSD_PARTICLE_CHOICE) {
			if (maximum > result) {
				result = maximum;
			}
		} else if (result == ULONG_MAX || maximum == ULONG_MAX ||
			   maximum > ULONG_MAX - result) {
			result = ULONG_MAX;
		} else {
			result += maximum;
		}
	}
	return result;
}

static unsigned long stream_consecutive_starts(const mxsd_stream_frame_t *frame, size_t position,
					       const mxsd_particle_t *particle)
{
	unsigned long count = 0u;

	while (position < frame->child_count &&
	       stream_particle_can_start(particle, frame->child_particles[position])) {
		position++;
		count++;
	}
	return count;
}

static int stream_consume_particle(const mxsd_stream_frame_t *frame,
				   const mxsd_particle_t *particle, size_t *position,
				   unsigned long *consumed, int force_minimum);

static int stream_match_particle_once(const mxsd_stream_frame_t *frame,
				      const mxsd_particle_t *particle, size_t *position,
				      int prefer_minimum, int *matched)
{
	size_t index;

	*matched = 0;
	if (particle->kind == MXSD_PARTICLE_ELEMENT || particle->kind == MXSD_PARTICLE_ANY) {
		if (*position < frame->child_count &&
		    frame->child_particles[*position] == particle) {
			(*position)++;
			*matched = 1;
		}
		return 1;
	}
	if (particle->kind == MXSD_PARTICLE_SEQUENCE) {
		int any = 0;

		for (index = 0u; index < particle->child_count; ++index) {
			const mxsd_particle_t *child = &particle->children[index];
			mxsd_particle_t limited;
			unsigned long count;

			if (prefer_minimum && index == 0u && child->min_occurs > 0u &&
			    child->max_occurs > child->min_occurs) {
				limited = *child;
				limited.max_occurs = limited.min_occurs;
				child = &limited;
			}
			if (!stream_consume_particle(frame, child, position, &count,
						     prefer_minimum)) {
				return 0;
			}
			any = any || count != 0u;
		}
		*matched = any;
		return 1;
	}
	if (particle->kind == MXSD_PARTICLE_CHOICE) {
		for (index = 0u; index < particle->child_count; ++index) {
			const mxsd_particle_t *child = &particle->children[index];

			if (*position < frame->child_count &&
			    stream_particle_can_start(child, frame->child_particles[*position])) {
				mxsd_particle_t limited;
				unsigned long count;

				if (prefer_minimum && child->min_occurs > 0u &&
				    child->max_occurs > child->min_occurs) {
					limited = *child;
					limited.max_occurs = limited.min_occurs;
					child = &limited;
				}
				if (!stream_consume_particle(frame, child, position, &count,
							     prefer_minimum)) {
					return 0;
				}
				*matched = count != 0u;
				return 1;
			}
		}
		return 1;
	}
	{
		unsigned long counts[MXSD_MAX_PARTICLES] = {0};
		int progress;

		if (particle->child_count > MXSD_MAX_PARTICLES) {
			return 0;
		}
		do {
			progress = 0;
			for (index = 0u; index < particle->child_count; ++index) {
				const mxsd_particle_t *child = &particle->children[index];

				if (*position < frame->child_count &&
				    counts[index] < child->max_occurs &&
				    stream_particle_can_start(child,
							      frame->child_particles[*position])) {
					int one;

					if (!stream_match_particle_once(frame, child, position, 0,
									&one)) {
						return 0;
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
		for (index = 0u; index < particle->child_count; ++index) {
			if (counts[index] < particle->children[index].min_occurs) {
				return 0;
			}
		}
	}
	return 1;
}

static int stream_consume_particle(const mxsd_stream_frame_t *frame,
				   const mxsd_particle_t *particle, size_t *position,
				   unsigned long *consumed, int force_minimum)
{
	*consumed = 0u;
	while (*consumed < particle->max_occurs && *position < frame->child_count &&
	       stream_particle_can_start(particle, frame->child_particles[*position])) {
		int matched;
		int prefer_minimum = force_minimum || *consumed + 1u < particle->min_occurs;

		if (!prefer_minimum && particle->max_occurs > 1u &&
		    stream_homogeneous_leaf(particle) != NULL) {
			unsigned long maximum = stream_particle_one_maximum(particle);

			prefer_minimum =
				maximum != ULONG_MAX &&
				stream_consecutive_starts(frame, *position, particle) > maximum;
		}
		if (!stream_match_particle_once(frame, particle, position, prefer_minimum,
						&matched)) {
			return 0;
		}
		if (!matched) {
			break;
		}
		(*consumed)++;
	}
	return *consumed >= particle->min_occurs || stream_particle_accepts_empty(particle);
}

static int stream_validate_particle(const mxsd_stream_frame_t *frame)
{
	size_t position = 0u;
	unsigned long count;

	return stream_consume_particle(frame, frame->complex_type->particle, &position, &count,
				       0) &&
	       position == frame->child_count;
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
		if (!matches && stream_is_substitute(stream, global, element)) {
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

mxsd_result_t mxsd_stream_init_with_allocator(mxsd_stream_t *stream, const mxsd_schema_t *schema,
					      const mxsd_allocator_t *allocator)
{
	if (stream == NULL || schema == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	memset(stream, 0, sizeof(*stream));
	stream->schema = schema;
	stream->result = make_result(MXSD_OK, 0, NULL);
	if (schema->identity_count != 0u) {
		if (allocator == NULL || allocator->allocate == NULL ||
		    allocator->deallocate == NULL) {
			stream->result = make_result(MXSD_ERR_RESOURCE_LIMIT, 0, schema->root_name);
			return stream->result;
		}
		stream->identity_allocator = *allocator;
		stream->identity_xml_capacity = MXSD_MAX_INPUT + 1u;
		stream->identity_xml =
			allocator->allocate(allocator->context, stream->identity_xml_capacity);
		if (stream->identity_xml == NULL) {
			stream->identity_xml_capacity = 0u;
			stream->result = make_result(MXSD_ERR_RESOURCE_LIMIT, 0, schema->root_name);
			return stream->result;
		}
		stream->identity_xml[0] = '\0';
	}
	return stream->result;
}

#ifndef MXSD_NO_HEAP
/** Standard-library allocation adapter for streamed identity constraints. */
static void *stream_default_allocate(void *context, size_t size)
{
	(void)context;
	return malloc(size);
}

/** Standard-library release adapter for streamed identity constraints. */
static void stream_default_deallocate(void *context, void *pointer)
{
	(void)context;
	free(pointer);
}
#endif

mxsd_result_t mxsd_stream_init(mxsd_stream_t *stream, const mxsd_schema_t *schema)
{
#ifdef MXSD_NO_HEAP
	return mxsd_stream_init_with_allocator(stream, schema, NULL);
#else
	const mxsd_allocator_t allocator = {stream_default_allocate, stream_default_deallocate,
					    NULL};
	return mxsd_stream_init_with_allocator(stream, schema, &allocator);
#endif
}

void mxsd_stream_dispose(mxsd_stream_t *stream)
{
	if (stream != NULL) {
		stream_release_identity(stream);
	}
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
	if (!stream_xml_string(stream, ">")) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, frame->expected_name);
	}
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
		const mxsd_element_t *element = NULL;
		const mxsd_global_element_t *substitute = NULL;
		mxsd_result_t result = mxsd_start_content(stream);
		if (result.code != MXSD_OK) {
			return result;
		}
		if (parent->nilled) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, parent->expected_name);
		}
		frame = &stream->frames[stream->depth];
		memset(frame, 0, sizeof(*frame));
		if (parent->skip_validation) {
			parent->child_count++;
			frame->skip_validation = 1;
		} else if (parent->type != MXSD_T_COMPLEX || parent->complex_type == NULL ||
			   parent->complex_type->has_simple_content) {
			return stream_fail(stream, MXSD_ERR_UNEXPECTED_ELEMENT, local_name);
		} else if (parent->complex_type->particle != NULL) {
			const mxsd_particle_t *leaf =
				stream_find_leaf(stream, parent->complex_type->particle,
						 namespace_uri, local_name, &substitute);

			if (leaf == NULL) {
				return stream_fail(stream, MXSD_ERR_UNEXPECTED_ELEMENT, local_name);
			}
			if (parent->child_count >= MXSD_STREAM_CHILD_CAPACITY) {
				return stream_fail(stream, MXSD_ERR_STREAM_LIMIT,
						   parent->expected_name);
			}
			parent->child_particles[parent->child_count++] = leaf;
			if (leaf->kind == MXSD_PARTICLE_ELEMENT) {
				element = leaf->element;
				stream_set_element(frame, element, substitute);
				if ((substitute == NULL && element->abstract_element) ||
				    (substitute != NULL && substitute->abstract_element)) {
					return stream_fail(stream, MXSD_ERR_INVALID_VALUE,
							   local_name);
				}
			} else {
				const mxsd_wildcard_t *wildcard = leaf->wildcard;

				if (wildcard->process_contents == MXSD_PROCESS_STRICT &&
				    substitute == NULL) {
					return stream_fail(stream, MXSD_ERR_UNEXPECTED_ELEMENT,
							   local_name);
				}
				if (substitute == NULL ||
				    wildcard->process_contents == MXSD_PROCESS_SKIP) {
					frame->skip_validation = 1;
				} else {
					stream_set_element(frame, NULL, substitute);
				}
			}
		} else {
			parent->child_count++;
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
		int unbound = stream->schema->root_name == NULL;

		frame = &stream->frames[0];
		memset(frame, 0, sizeof(*frame));
		if (!unbound &&
		    !stream_name_equal(local_name, namespace_uri, stream->schema->root_name,
				       stream->schema->root_namespace_uri)) {
			return stream_fail(stream, MXSD_ERR_WRONG_ROOT, stream->schema->root_name);
		}
		frame->expected_name = unbound ? local_name : stream->schema->root_name;
		frame->expected_namespace =
			unbound ? stream_ns(namespace_uri) : stream->schema->root_namespace_uri;
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
	frame->namespace_base = stream->namespace_count;
	if (!stream_copy_name(frame->local_name, local_name) ||
	    !stream_copy_name(frame->namespace_uri, stream_ns(namespace_uri))) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, local_name);
	}
	if (stream->depth == 0u && stream->schema->root_name == NULL) {
		frame->expected_name = frame->local_name;
		frame->expected_namespace = stream_ns(frame->namespace_uri);
	}
	stream->depth++;
	if (!stream_xml_start_element(stream, namespace_uri, local_name)) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, local_name);
	}
	return stream->result;
}

mxsd_result_t mxsd_namespace(mxsd_stream_t *stream, const char *prefix, const char *namespace_uri)
{
	mxsd_stream_frame_t *frame;
	size_t index;
	if (stream == NULL || prefix == NULL || namespace_uri == NULL || stream->depth == 0u) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (stream->result.code != MXSD_OK) {
		return stream->result;
	}
	frame = &stream->frames[stream->depth - 1u];
	if (frame->content_started || !strncmp(prefix, "_mxsd", 5u)) {
		return stream_fail(stream, MXSD_ERR_MALFORMED_XML, prefix);
	}
	if (stream->namespace_count >= MXSD_MAX_NAMESPACES ||
	    !stream_copy_name(stream->namespaces[stream->namespace_count].prefix, prefix) ||
	    !stream_copy_name(stream->namespaces[stream->namespace_count].uri, namespace_uri)) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, prefix);
	}
	for (index = frame->namespace_base; index < stream->namespace_count; ++index) {
		if (!strcmp(stream->namespaces[index].prefix, prefix)) {
			return stream_fail(stream, MXSD_ERR_MALFORMED_XML, prefix);
		}
	}
	stream->namespace_count++;
	stream->event_offset++;
	if (!stream_xml_string(stream, prefix[0] == '\0' ? " xmlns='" : " xmlns:") ||
	    (prefix[0] != '\0' &&
	     (!stream_xml_string(stream, prefix) || !stream_xml_string(stream, "='"))) ||
	    !stream_xml_escaped(stream, namespace_uri, strlen(namespace_uri), 1) ||
	    !stream_xml_string(stream, "'")) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, prefix);
	}
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
	if (!stream_xml_attribute(stream, namespace_uri, local_name, value)) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, local_name);
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
	if (stream_name_equal(local_name, namespace_uri, "schemaLocation", xsi) ||
	    stream_name_equal(local_name, namespace_uri, "noNamespaceSchemaLocation", xsi)) {
		return stream->result;
	}
	if (frame->type != MXSD_T_COMPLEX || frame->complex_type == NULL) {
		return stream_fail(stream, MXSD_ERR_UNEXPECTED_ATTRIBUTE, local_name);
	}
	stream_prepare_parser(stream, &parser);
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
	static const char xsd[] = "http://www.w3.org/2001/XMLSchema";
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
	mxsd_stream_frame_t *frame;
	const mxsd_named_type_t *dynamic = NULL;
	int unbound;
	size_t index;
	if (stream == NULL || type_local_name == NULL || stream->depth == 0u) {
		return make_result(MXSD_ERR_ARGUMENT, 0, NULL);
	}
	if (stream->result.code != MXSD_OK) {
		return stream->result;
	}
	frame = &stream->frames[stream->depth - 1u];
	unbound = stream->schema->root_name == NULL && stream->depth == 1u;
	if (frame->content_started) {
		return stream_fail(stream, MXSD_ERR_MALFORMED_XML, type_local_name);
	}
	if (!stream_xml_xsi_type(stream, type_namespace_uri, type_local_name)) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, type_local_name);
	}
	for (index = 0; index < stream->schema->named_type_count; index++) {
		const mxsd_named_type_t *candidate = &stream->schema->named_types[index];
		if (stream_name_equal(type_local_name, type_namespace_uri, candidate->name,
				      candidate->namespace_uri)) {
			dynamic = candidate;
			break;
		}
	}
	if (dynamic == NULL &&
	    stream_name_equal(type_local_name, type_namespace_uri, "anySimpleType", xsd)) {
		if (!unbound && (frame->type != MXSD_T_COMPLEX || frame->complex_type == NULL ||
				 !frame->complex_type->any_type)) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, type_local_name);
		}
		frame->type = MXSD_T_STRING;
		frame->complex_type = NULL;
		frame->simple_type = NULL;
		return stream->result;
	}
	if (dynamic == NULL &&
	    stream_name_equal(type_local_name, type_namespace_uri, "anyType", xsd)) {
		if (unbound || frame->type != MXSD_T_COMPLEX || frame->complex_type == NULL ||
		    !frame->complex_type->any_type) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, type_local_name);
		}
		return stream->result;
	}
	if (dynamic == NULL && unbound && stream_ns(type_namespace_uri) != NULL &&
	    strcmp(type_namespace_uri, xsd) == 0) {
		for (index = 0u; index < sizeof(builtins) / sizeof(builtins[0]); ++index) {
			if (strcmp(type_local_name, builtins[index].name) == 0) {
				frame->type = builtins[index].type;
				frame->complex_type = NULL;
				frame->simple_type = NULL;
				return stream->result;
			}
		}
	}
	if (dynamic == NULL) {
		return stream_fail(stream, MXSD_ERR_INVALID_VALUE, type_local_name);
	}
	if (dynamic->complex_type != NULL && unbound) {
		if (dynamic->complex_type->abstract_type) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, type_local_name);
		}
		frame->type = MXSD_T_COMPLEX;
		frame->complex_type = dynamic->complex_type;
		frame->simple_type = NULL;
	} else if (dynamic->complex_type != NULL) {
		int derives = frame->type == MXSD_T_COMPLEX
				      ? complex_derives_from(dynamic->complex_type,
							     frame->complex_type, 0u)
				      : dynamic->complex_type->has_simple_content &&
						(stream_is_any_simple_type(frame->simple_type) ||
						 (simple_derives_from(
							  dynamic->complex_type->simple_content,
							  frame->simple_type, 0u) &&
						  (frame->simple_type != NULL ||
						   dynamic->complex_type->simple_content_type ==
							   frame->type)));
		if (!derives || dynamic->complex_type->abstract_type) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, type_local_name);
		}
		frame->type = MXSD_T_COMPLEX;
		frame->complex_type = dynamic->complex_type;
		frame->simple_type = NULL;
	} else if (unbound) {
		frame->type = dynamic->type;
		frame->complex_type = NULL;
		frame->simple_type = dynamic->simple_type;
	} else {
		if (frame->type == MXSD_T_COMPLEX) {
			if (frame->complex_type == NULL || !frame->complex_type->any_type ||
			    !simple_derives_from(dynamic->simple_type, NULL, 0u)) {
				return stream_fail(stream, MXSD_ERR_INVALID_VALUE, type_local_name);
			}
		} else if (!simple_derives_from(dynamic->simple_type, frame->simple_type, 0u)) {
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
	if (!stream_xml_escaped(stream, text, length, 0)) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, frame->expected_name);
	}
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
		if (frame->complex_type->mixed_content) {
			return stream->result;
		}
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
	if (!frame->skip_validation && frame->nilled &&
	    (!frame->nillable || frame->fixed_value != NULL)) {
		return stream_fail(stream, MXSD_ERR_INVALID_VALUE, frame->expected_name);
	}
	if (!frame->skip_validation && !frame->nilled && frame->type == MXSD_T_COMPLEX &&
	    frame->complex_type != NULL && !frame->complex_type->has_simple_content) {
		if (frame->complex_type->particle != NULL) {
			if (!stream_validate_particle(frame)) {
				return stream_fail(stream,
						   frame->child_count == 0u
							   ? MXSD_ERR_MISSING_ELEMENT
							   : MXSD_ERR_ELEMENT_ORDER,
						   frame->expected_name);
			}
		} else if (frame->complex_type->model == MXSD_CHOICE) {
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
		if (frame->complex_type->mixed_content && frame->fixed_value != NULL) {
			if (frame->child_count != 0u) {
				return stream_fail(stream, MXSD_ERR_INVALID_VALUE,
						   frame->expected_name);
			}
			stream_prepare_parser(stream, &parser);
			value.data = frame->text;
			value.length = frame->text_length;
			if (!simple_values_equal(&parser, MXSD_T_STRING, NULL, value,
						 frame->fixed_value)) {
				return stream_fail(stream, MXSD_ERR_INVALID_VALUE,
						   frame->expected_name);
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
		stream_prepare_parser(stream, &parser);
		value.data = effective;
		value.length = strlen(effective);
		if (!validate_simple_value(&parser, type, simple, value) ||
		    (frame->fixed_value != NULL &&
		     !simple_values_equal(&parser, type, simple, value, frame->fixed_value))) {
			return stream_fail(stream, MXSD_ERR_INVALID_VALUE, frame->expected_name);
		}
	}
	if (stream_ns(namespace_uri) != NULL) {
		char prefix[32];
		(void)snprintf(prefix, sizeof(prefix), "_mxsde%lu",
			       (unsigned long)(stream->depth - 1u));
		if (!stream_xml_string(stream, "</") || !stream_xml_string(stream, prefix) ||
		    !stream_xml_string(stream, ":") || !stream_xml_string(stream, local_name) ||
		    !stream_xml_string(stream, ">")) {
			return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, local_name);
		}
	} else if (!stream_xml_string(stream, "</") || !stream_xml_string(stream, local_name) ||
		   !stream_xml_string(stream, ">")) {
		return stream_fail(stream, MXSD_ERR_STREAM_LIMIT, local_name);
	}
	stream->namespace_count = frame->namespace_base;
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
	if (stream->identity_xml != NULL) {
		mxsd_result_t identity = mxsd_validate_identities(
			stream->schema, stream->identity_xml, &stream->identity_allocator);
		stream_release_identity(stream);
		if (identity.code != MXSD_OK) {
			stream->result =
				make_result(identity.code, stream->event_offset, identity.path);
		}
	}
	return stream->result;
}
