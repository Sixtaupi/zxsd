/*
 * Copyright (c) 2026 mxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Single-pass typed subtree delivery over validated raw XML events. */

#include "mxsd_bind_internal.h"
#include "mxsd_internal.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

/** Compare expanded names, treating NULL and an empty namespace identically. */
static int binding_name(const char *name, const char *uri, const char *expected,
			const char *expected_uri)
{
	return strcmp(name, expected) == 0 &&
	       strcmp(uri != NULL ? uri : "", expected_uri != NULL ? expected_uri : "") == 0;
}

/** Return a writable member; pointers and scalars are loaded/stored through memcpy. */
static unsigned char *binding_member(void *object, size_t offset)
{
	return (unsigned char *)object + offset;
}

/** Restore the arena and invalidate all transient pointers in the output object. */
static void binding_reset(mxsd_bind_stream_t *reader)
{
	reader->arena->used = reader->arena_mark;
	memset(reader->object, 0, reader->selection->type->size);
	reader->active_depth = 0u;
}

/** Store the first error at the current raw token, using only descriptor-owned paths. */
static void binding_fail(mxsd_bind_stream_t *reader, mxsd_error_t error, const char *path)
{
	if (reader->xml.result.code == MXSD_OK) {
		reader->xml.result = make_result(error, reader->xml.token_offset, path);
	}
	binding_reset(reader);
}

/** Reject locally valid dynamic/nilled content that the selected type cannot represent. */
static int binding_type_matches(mxsd_bind_stream_t *reader, size_t depth)
{
	const mxsd_bind_stream_frame_t *frame = &reader->frames[depth];
	const mxsd_stream_frame_t *validated = &reader->xml.validator.frames[depth];
	const mxsd_complex_t *actual = validated->complex_type;

	if (validated->nilled ||
	    (frame->type != NULL && actual != NULL &&
	     !binding_name(actual->type_name != NULL ? actual->type_name : "",
			   actual->type_namespace,
			   frame->type->type_name != NULL ? frame->type->type_name : "",
			   frame->type->type_namespace))) {
		binding_fail(reader, MXSD_ERR_UNSUPPORTED, validated->expected_name);

		return 0;
	}

	return 1;
}

/** Store an already validated attribute/scalar and update optional native presence. */
static int binding_value(mxsd_bind_stream_t *reader, const mxsd_bind_field_t *field, void *address,
			 void *owner, const char *value, size_t length)
{
	mxsd_result_t result = mxsd_bind_store_decoded(reader->arena, field, address, value, length,
						       reader->xml.token_offset);

	if (result.code != MXSD_OK) {
		binding_fail(reader, result.code, field->name);
		return 0;
	}

	if (owner != NULL && field->presence_offset != MXSD_BIND_NO_OFFSET) {
		bool present = true;

		memcpy(binding_member(owner, field->presence_offset), &present, sizeof(present));
	}

	return 1;
}

/** Allocate one occurrence, growing repeated arrays geometrically within the arena. */
static void *binding_target(mxsd_bind_stream_t *reader, mxsd_bind_stream_frame_t *parent,
			    const mxsd_bind_field_t *field)
{
	unsigned char *address = binding_member(parent->object, field->offset);
	void *target = address;

	if (field->storage == MXSD_BIND_ARRAY) {
		size_t count;
		void *items;

		memcpy(&count, binding_member(parent->object, field->count_offset), sizeof(count));
		memcpy(&items, address, sizeof(items));

		if (count == (size_t)-1 || field->value_size == 0u ||
		    (field->max_occurs != ULONG_MAX && count >= field->max_occurs)) {
			binding_fail(reader, MXSD_ERR_RESOURCE_LIMIT, field->name);
			return NULL;
		}

		if (count == 0u || (count & (count - 1u)) == 0u) {
			size_t capacity;
			void *grown;

			if (count > (size_t)-1 / 2u) {
				binding_fail(reader, MXSD_ERR_RESOURCE_LIMIT, field->name);
				return NULL;
			}

			capacity = count == 0u ? 1u : 2u * count;

			if (field->max_occurs != ULONG_MAX && capacity > field->max_occurs) {
				capacity = (size_t)field->max_occurs;
			}

			if (capacity > (size_t)-1 / field->value_size ||
			    (grown = mxsd_bind_arena_allocate(
				     reader->arena, capacity * field->value_size)) == NULL) {
				binding_fail(reader, MXSD_ERR_RESOURCE_LIMIT, field->name);
				return NULL;
			}

			if (count != 0u) {
				memcpy(grown, items, count * field->value_size);
			}

			items = grown;
			memcpy(address, &items, sizeof(items));
		}
		target = (unsigned char *)items + count * field->value_size;
		count++;
		memcpy(binding_member(parent->object, field->count_offset), &count, sizeof(count));
	} else if (field->storage == MXSD_BIND_POINTER) {
		target = mxsd_bind_arena_allocate(reader->arena, field->value_size);
		if (target == NULL) {
			binding_fail(reader, MXSD_ERR_RESOURCE_LIMIT, field->name);
			return NULL;
		}
		memcpy(address, &target, sizeof(target));
	}

	if (field->presence_offset != MXSD_BIND_NO_OFFSET) {
		bool present = true;

		memcpy(binding_member(parent->object, field->presence_offset), &present,
		       sizeof(present));
	}

	return target;
}

/** Begin a selected occurrence or one of its bound child elements. */
static void binding_start(mxsd_bind_stream_t *reader, const char *uri, const char *name)
{
	size_t depth = reader->xml.validator.depth - 1u;
	mxsd_bind_stream_frame_t *frame = &reader->frames[depth];
	const mxsd_bind_selection_t *selection = reader->selection;
	size_t index;

	memset(frame, 0, sizeof(*frame));

	if (depth < selection->step_count &&
	    (depth == 0u || reader->frames[depth - 1u].path_match)) {
		frame->path_match =
			(unsigned char)binding_name(name, uri, selection->steps[depth].name,
						    selection->steps[depth].namespace_uri);
	}

	if (reader->active_depth == 0u) {
		if (!reader->validate_only && depth + 1u == selection->step_count &&
		    frame->path_match) {
			binding_reset(reader);
			reader->active_depth = depth + 1u;
			frame->object = reader->object;
			frame->type = selection->type;
		}
		return;
	}

	if (!binding_type_matches(reader, depth - 1u)) {
		return;
	}

	mxsd_bind_stream_frame_t *parent = &reader->frames[depth - 1u];

	for (index = 0u; parent->type != NULL && index < parent->type->field_count; ++index) {
		const mxsd_bind_field_t *field = &parent->type->fields[index];

		if (field->role != MXSD_BIND_ELEMENT ||
		    !binding_name(name, uri, field->name, field->namespace_uri)) {
			continue;
		}

		frame->object = binding_target(reader, parent, field);
		frame->field = field;
		frame->type = field->kind == MXSD_BIND_COMPLEX ? field->type : NULL;
		return;
	}

	binding_fail(reader, MXSD_ERR_UNSUPPORTED,
		     selection->steps[selection->step_count - 1u].name);
}

/** Convert explicit attributes; namespaces and xsi instructions stay with the validator. */
static void binding_attribute(mxsd_bind_stream_t *reader, const char *uri, const char *name,
			      const char *value, size_t length)
{
	size_t depth = reader->xml.validator.depth - 1u;
	mxsd_bind_stream_frame_t *frame = &reader->frames[depth];
	size_t index, attribute = 0u;

	if (reader->active_depth == 0u || !binding_type_matches(reader, depth)) {
		return;
	}

	if (strcmp(uri, "http://www.w3.org/2001/XMLSchema-instance") == 0) {
		return;
	}

	for (index = 0u; frame->type != NULL && index < frame->type->field_count; ++index) {
		const mxsd_bind_field_t *field = &frame->type->fields[index];

		if (field->role != MXSD_BIND_ATTRIBUTE) {
			continue;
		}

		if (attribute >= MXSD_MAX_ATTRIBUTES) {
			break;
		}

		if (binding_name(name, uri, field->name, field->namespace_uri)) {
			if (binding_value(reader, field,
					  binding_member(frame->object, field->offset),
					  frame->object, value, length)) {
				frame->seen_attributes[attribute] = 1u;
			}
			return;
		}
		attribute++;
	}

	binding_fail(reader, MXSD_ERR_UNSUPPORTED, reader->selection->type->name);
}

/** Apply absent attribute defaults and convert complete decoded simple content. */
static int binding_complete(mxsd_bind_stream_t *reader, size_t depth)
{
	mxsd_bind_stream_frame_t *frame = &reader->frames[depth];
	const mxsd_stream_frame_t *validated = &reader->xml.validator.frames[depth];
	size_t index, attribute = 0u;
	const mxsd_bind_field_t *content = frame->type == NULL ? frame->field : NULL;

	if (!binding_type_matches(reader, depth)) {
		return 0;
	}
	for (index = 0u; frame->type != NULL && index < frame->type->field_count; ++index) {
		const mxsd_bind_field_t *field = &frame->type->fields[index];
		const char *fallback =
			field->fixed_value != NULL ? field->fixed_value : field->default_value;

		if (field->role == MXSD_BIND_CONTENT) {
			content = field;
		} else if (field->role == MXSD_BIND_ATTRIBUTE) {
			if (attribute >= MXSD_MAX_ATTRIBUTES) {
				binding_fail(reader, MXSD_ERR_RESOURCE_LIMIT, field->name);
				return 0;
			}
			if (!frame->seen_attributes[attribute] && fallback != NULL &&
			    !binding_value(reader, field,
					   binding_member(frame->object, field->offset),
					   frame->object, fallback, strlen(fallback))) {
				return 0;
			}
			attribute++;
		}
	}
	if (content != NULL) {
		const char *value = validated->text;
		size_t length = validated->text_length;
		const char *fallback = validated->fixed_value != NULL ? validated->fixed_value
								      : validated->default_value;
		void *address = frame->type == NULL
					? frame->object
					: binding_member(frame->object, content->offset);

		if (length == 0u && fallback != NULL) {
			value = fallback;
			length = strlen(fallback);
		}
		return binding_value(reader, content, address, NULL, value, length);
	}

	return 1;
}

/** Interpret accepted parser events; a conversion failure stops the same input fragment. */
static void binding_event(void *context, mxsd_xml_event_t event, const char *uri, const char *name,
			  const char *value, size_t length)
{
	mxsd_bind_stream_t *reader = context;

	if (reader->xml.result.code != MXSD_OK) {
		return;
	}

	if (event == MXSD_XML_EVENT_START_ELEMENT) {
		binding_start(reader, uri, name);
	} else if (event == MXSD_XML_EVENT_ATTRIBUTE) {
		binding_attribute(reader, uri, name, value, length);
	} else if (event == MXSD_XML_EVENT_END_ELEMENT && reader->active_depth != 0u) {
		size_t depth = reader->xml.validator.depth;

		if (!binding_complete(reader, depth)) {
			return;
		}
		if (depth + 1u == reader->active_depth) {
			mxsd_bind_action_t action =
				reader->callback(reader->user_data, reader->object);

			if (action != MXSD_BIND_CONTINUE && action != MXSD_BIND_VALIDATE_ONLY) {
				binding_fail(reader, MXSD_ERR_OUTPUT,
					     reader->selection->type->name);
				return;
			}
			reader->validate_only = action == MXSD_BIND_VALIDATE_ONLY;
			binding_reset(reader);
		}
	}
}

mxsd_result_t mxsd_bind_stream_init(mxsd_bind_stream_t *reader,
				    const mxsd_bind_selection_t *selection, void *object,
				    mxsd_bind_arena_t *arena, mxsd_bind_item_fn callback,
				    void *user_data, const mxsd_allocator_t *allocator)
{
	mxsd_result_t result;

	if (reader == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0u, NULL);
	}

	memset(reader, 0, sizeof(*reader));

	if (selection == NULL || selection->schema == NULL || selection->steps == NULL ||
	    selection->step_count == 0u || selection->step_count > MXSD_MAX_DEPTH ||
	    selection->type == NULL || object == NULL || callback == NULL || arena == NULL ||
	    arena->used > arena->capacity || (arena->data == NULL && arena->capacity != 0u)) {
		reader->xml.result = make_result(MXSD_ERR_ARGUMENT, 0u, NULL);
		return reader->xml.result;
	}

	reader->selection = selection;
	reader->arena = arena;
	reader->arena_mark = arena->used;
	reader->object = object;
	reader->callback = callback;
	reader->user_data = user_data;
	binding_reset(reader);

	result = mxsd_xml_reader_init_with_allocator(&reader->xml, selection->schema(), allocator);

	if (result.code == MXSD_OK) {
		mxsd_xml_reader_set_event_callback(&reader->xml, binding_event, reader);
	}

	return result;
}

mxsd_result_t mxsd_bind_stream_feed(mxsd_bind_stream_t *reader, const void *data, size_t length)
{
	mxsd_result_t result;

	if (reader == NULL || reader->selection == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0u, NULL);
	}

	if (data == NULL && length != 0u && reader->xml.result.code == MXSD_OK) {
		binding_fail(reader, MXSD_ERR_ARGUMENT, reader->selection->type->name);
	}

	if (reader->finished || reader->xml.result.code != MXSD_OK) {
		if (reader->finished && reader->xml.result.code == MXSD_OK && length != 0u) {
			binding_fail(reader, MXSD_ERR_ARGUMENT, reader->selection->type->name);
		}
		return reader->xml.result;
	}

	result = mxsd_xml_reader_feed(&reader->xml, data, length);

	if (result.code != MXSD_OK) {
		binding_reset(reader);
	}

	return result;
}

mxsd_result_t mxsd_bind_stream_finish(mxsd_bind_stream_t *reader)
{
	mxsd_result_t result;

	if (reader == NULL || reader->selection == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0u, NULL);
	}

	if (reader->finished || reader->xml.result.code != MXSD_OK) {
		return reader->xml.result;
	}

	result = mxsd_xml_reader_finish(&reader->xml);
	reader->finished = result.code == MXSD_OK;
	binding_reset(reader);

	return result;
}

void mxsd_bind_stream_dispose(mxsd_bind_stream_t *reader)
{
	if (reader != NULL && reader->selection != NULL) {
		mxsd_xml_reader_dispose(&reader->xml);
		binding_reset(reader);
		reader->selection = NULL;
	}
}
