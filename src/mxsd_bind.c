/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Descriptor-driven reader and writer for generated typed bindings.
 */

#include "mxsd_bind.h"
#include "mxsd_internal.h"
#include "mxsd_regex.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/** Namespace of the xsi:type, xsi:nil and schema-location attributes. */
static const char xsi_namespace[] = "http://www.w3.org/2001/XMLSchema-instance";

/** Namespace bound to the reserved @c xml prefix. */
static const char xml_namespace[] = "http://www.w3.org/XML/1998/namespace";

/** Union of the types with the strictest alignment used by bound structures. */
typedef union {
	long double long_double_value;
	double double_value;
	uint64_t uint64_value;
	long long_value;
	void *object_pointer;
	void (*function_pointer)(void);
} bind_max_align_t;

/** Probe whose member offset yields the alignment of @ref bind_max_align_t. */
typedef struct {
	char padding;
	bind_max_align_t value;
} alignment_probe_t;

/** Alignment applied to every arena allocation holding structures or values. */
#define BIND_ALIGNMENT offsetof(alignment_probe_t, value)

/*
 * Keep a function out of its caller's frame. Inlining is only an optimization,
 * so other compilers are correct without the hint.
 */
#if defined(__GNUC__) || defined(__clang__)
#define MXSD_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define MXSD_NOINLINE __declspec(noinline)
#else
#define MXSD_NOINLINE
#endif

/** Largest decimal representation of a 64-bit integer, with sign and NUL. */
#define INTEGER_TEXT_CAPACITY 24u

/* ------------------------------------------------------------------------- */
/* Shared helpers                                                            */
/* ------------------------------------------------------------------------- */

/** Return the address of a member at @p offset inside a mutable structure. */
static unsigned char *member(void *object, size_t offset)
{
	return (unsigned char *)object + offset;
}

/** Return the address of a member at @p offset inside a read-only structure. */
static const unsigned char *const_member(const void *object, size_t offset)
{
	return (const unsigned char *)object + offset;
}

/** Load a @c const pointer stored in a structure member. */
static const void *load_pointer(const unsigned char *address)
{
	const void *pointer;

	memcpy(&pointer, address, sizeof(pointer));
	return pointer;
}

/**
 * @brief Load a pointer member that refers to arena storage owned by the reader.
 *
 * Bound structures expose @c const pointers, but the reader fills the arrays it
 * allocated itself, so the storage is writable.
 */
static void *load_arena_pointer(const unsigned char *address)
{
	void *pointer;

	memcpy(&pointer, address, sizeof(pointer));
	return pointer;
}

/** Store a @c const pointer into a structure member. */
static void store_pointer(unsigned char *address, const void *pointer)
{
	memcpy(address, &pointer, sizeof(pointer));
}

/** Load a @c size_t count stored in a structure member. */
static size_t load_count(const unsigned char *address)
{
	size_t count;

	memcpy(&count, address, sizeof(count));
	return count;
}

/** Store a @c size_t count into a structure member. */
static void store_count(unsigned char *address, size_t count)
{
	memcpy(address, &count, sizeof(count));
}

/** Load a @c bool stored in a structure member. */
static bool load_bool(const unsigned char *address)
{
	bool value;

	memcpy(&value, address, sizeof(value));
	return value;
}

/** Store a @c bool into a structure member. */
static void store_bool(unsigned char *address, bool value)
{
	memcpy(address, &value, sizeof(value));
}

/** Compare a nullable namespace URI with a resolved namespace span. */
static int namespace_matches(const char *expected, text_span_t actual)
{
	return expected == NULL ? actual.length == 0 : span_equals_string(actual, expected);
}

/** Return whether a character is XML whitespace. */
static int is_xml_space(char character)
{
	return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

/** Remove leading and trailing XML whitespace from a span. */
static text_span_t trim_span(text_span_t value)
{
	while (value.length != 0u && is_xml_space(value.data[0])) {
		value.data++;
		value.length--;
	}
	while (value.length != 0u && is_xml_space(value.data[value.length - 1u])) {
		value.length--;
	}
	return value;
}

/**
 * @brief Load the index stored in a generated C enumeration member.
 *
 * The C standard leaves the size of an enumeration to the implementation, so
 * the generated descriptor records it and every supported size is handled.
 */
static int load_enum_index(const unsigned char *address, size_t size, size_t *index)
{
	long value;

	if (size == sizeof(signed char)) {
		signed char stored;

		memcpy(&stored, address, sizeof(stored));
		value = stored;
	} else if (size == sizeof(short)) {
		short stored;

		memcpy(&stored, address, sizeof(stored));
		value = stored;
	} else if (size == sizeof(int)) {
		int stored;

		memcpy(&stored, address, sizeof(stored));
		value = stored;
	} else if (size == sizeof(long)) {
		memcpy(&value, address, sizeof(value));
	} else {
		return 0;
	}
	if (value < 0) {
		return 0;
	}
	*index = (size_t)value;
	return 1;
}

/** Store an enumeration index into a member of the recorded size. */
static int store_enum_index(unsigned char *address, size_t size, size_t index)
{
	if (size == sizeof(signed char) && index <= (size_t)SCHAR_MAX) {
		signed char stored = (signed char)index;

		memcpy(address, &stored, sizeof(stored));
	} else if (size == sizeof(short) && index <= (size_t)SHRT_MAX) {
		short stored = (short)index;

		memcpy(address, &stored, sizeof(stored));
	} else if (size == sizeof(int) && index <= (size_t)INT_MAX) {
		int stored = (int)index;

		memcpy(address, &stored, sizeof(stored));
	} else if (size == sizeof(long) && index <= (size_t)LONG_MAX) {
		long stored = (long)index;

		memcpy(address, &stored, sizeof(stored));
	} else {
		return 0;
	}
	return 1;
}

/** Return whether a field is optional in its enclosing structure. */
static int field_is_optional(const mxsd_bind_field_t *field)
{
	return field->min_occurs == 0u;
}

/* ------------------------------------------------------------------------- */
/* Writer                                                                    */
/* ------------------------------------------------------------------------- */

/** Mutable state of one serialization. */
typedef struct {
	const mxsd_bind_root_t *root;
	mxsd_bind_sink_fn sink;
	void *context;
	mxsd_stream_t *validator;
	size_t written;
	size_t depth;
} writer_t;

/** Build a writer result located at the current output offset. */
static mxsd_result_t writer_error(const writer_t *writer, mxsd_error_t code, const char *path)
{
	return make_result(code, writer->written, path);
}

/** Append raw bytes to the output. */
static mxsd_result_t emit(writer_t *writer, const char *data, size_t length, const char *path)
{
	if (length == 0u) {
		return make_result(MXSD_OK, writer->written, NULL);
	}
	if (writer->sink(writer->context, data, length) != 0) {
		return writer_error(writer, MXSD_ERR_OUTPUT, path);
	}
	writer->written += length;
	return make_result(MXSD_OK, writer->written, NULL);
}

/** Append a NUL-terminated string to the output. */
static mxsd_result_t emit_string(writer_t *writer, const char *text, const char *path)
{
	return emit(writer, text, strlen(text), path);
}

/** Test whether a Unicode scalar value is an XML 1.0 Char. */
static int is_xml_char(uint32_t codepoint)
{
	return codepoint == 0x9u || codepoint == 0xau || codepoint == 0xdu ||
	       (codepoint >= 0x20u && codepoint <= 0xd7ffu) ||
	       (codepoint >= 0xe000u && codepoint <= 0xfffdu) ||
	       (codepoint >= 0x10000u && codepoint <= 0x10ffffu);
}

/**
 * @brief Append text with the escaping required by its XML context.
 *
 * Attribute values also escape tab, line feed and carriage return so that
 * attribute-value normalization preserves them. Text escapes carriage return
 * so that end-of-line normalization preserves it.
 */
static mxsd_result_t emit_escaped(writer_t *writer, const char *text, size_t length,
				  int in_attribute, const char *path)
{
	size_t position = 0u;
	size_t run_start = 0u;
	mxsd_result_t result;

	while (position < length) {
		uint32_t codepoint;
		size_t consumed = mxsd_utf8_decode(text + position, length - position, &codepoint);
		const char *replacement = NULL;

		if (consumed == 0u || !is_xml_char(codepoint)) {
			return writer_error(writer, MXSD_ERR_INVALID_VALUE, path);
		}
		switch (codepoint) {
		case '&':
			replacement = "&amp;";
			break;
		case '<':
			replacement = "&lt;";
			break;
		case '>':
			replacement = "&gt;";
			break;
		case '"':
			replacement = in_attribute ? "&quot;" : NULL;
			break;
		case '\t':
			replacement = in_attribute ? "&#9;" : NULL;
			break;
		case '\n':
			replacement = in_attribute ? "&#10;" : NULL;
			break;
		case '\r':
			replacement = "&#13;";
			break;
		default:
			break;
		}
		if (replacement != NULL) {
			result = emit(writer, text + run_start, position - run_start, path);
			if (result.code != MXSD_OK) {
				return result;
			}
			result = emit_string(writer, replacement, path);
			if (result.code != MXSD_OK) {
				return result;
			}
			run_start = position + consumed;
		}
		position += consumed;
	}
	return emit(writer, text + run_start, position - run_start, path);
}

/** Return the prefix declared for a namespace, or @c NULL if none is known. */
static const char *namespace_prefix_for(const mxsd_bind_root_t *root, const char *namespace_uri)
{
	size_t index;

	if (strcmp(namespace_uri, xml_namespace) == 0) {
		return "xml";
	}
	for (index = 0u; index < root->namespace_count; ++index) {
		if (strcmp(root->namespaces[index].namespace_uri, namespace_uri) == 0) {
			return root->namespaces[index].prefix;
		}
	}
	return NULL;
}

/** Append the lexical form of an expanded name using the root prefixes. */
static mxsd_result_t emit_name(writer_t *writer, const char *namespace_uri, const char *name)
{
	mxsd_result_t result;

	if (namespace_uri != NULL) {
		const char *prefix = namespace_prefix_for(writer->root, namespace_uri);

		if (prefix == NULL) {
			return writer_error(writer, MXSD_ERR_ARGUMENT, name);
		}
		result = emit_string(writer, prefix, name);
		if (result.code != MXSD_OK) {
			return result;
		}
		result = emit(writer, ":", 1u, name);
		if (result.code != MXSD_OK) {
			return result;
		}
	}
	return emit_string(writer, name, name);
}

/** Format an unsigned integer in decimal, returning the text length. */
static size_t format_unsigned(uint64_t value, char *text)
{
	char reversed[INTEGER_TEXT_CAPACITY];
	size_t count = 0u;
	size_t index;

	do {
		reversed[count++] = (char)('0' + (int)(value % 10u));
		value /= 10u;
	} while (value != 0u);
	for (index = 0u; index < count; ++index) {
		text[index] = reversed[count - index - 1u];
	}
	text[count] = '\0';
	return count;
}

/** Format a signed integer in decimal, returning the text length. */
static size_t format_signed(int64_t value, char *text)
{
	if (value < 0) {
		/* Negate in unsigned arithmetic so that INT64_MIN is well defined. */
		text[0] = '-';
		return 1u + format_unsigned((uint64_t)0u - (uint64_t)value, text + 1);
	}
	return format_unsigned((uint64_t)value, text);
}

/**
 * @brief Produce the lexical text of a simple bound value.
 *
 * @param field Descriptor of the value.
 * @param address Address of one stored value.
 * @param scratch Storage for formatted numbers, @ref INTEGER_TEXT_CAPACITY bytes.
 * @param text Receives the NUL-terminated lexical value.
 */
static mxsd_error_t simple_text(const mxsd_bind_field_t *field, const unsigned char *address,
				char *scratch, const char **text)
{
	switch (field->kind) {
	case MXSD_BIND_STRING:
		*text = (const char *)load_pointer(address);
		return *text != NULL ? MXSD_OK : MXSD_ERR_ARGUMENT;
	case MXSD_BIND_BOOLEAN:
		*text = load_bool(address) ? "true" : "false";
		return MXSD_OK;
	case MXSD_BIND_INT8: {
		int8_t value;

		memcpy(&value, address, sizeof(value));
		format_signed(value, scratch);
		break;
	}
	case MXSD_BIND_UINT8: {
		uint8_t value;

		memcpy(&value, address, sizeof(value));
		format_unsigned(value, scratch);
		break;
	}
	case MXSD_BIND_INT16: {
		int16_t value;

		memcpy(&value, address, sizeof(value));
		format_signed(value, scratch);
		break;
	}
	case MXSD_BIND_UINT16: {
		uint16_t value;

		memcpy(&value, address, sizeof(value));
		format_unsigned(value, scratch);
		break;
	}
	case MXSD_BIND_INT32: {
		int32_t value;

		memcpy(&value, address, sizeof(value));
		format_signed(value, scratch);
		break;
	}
	case MXSD_BIND_UINT32: {
		uint32_t value;

		memcpy(&value, address, sizeof(value));
		format_unsigned(value, scratch);
		break;
	}
	case MXSD_BIND_INT64: {
		int64_t value;

		memcpy(&value, address, sizeof(value));
		format_signed(value, scratch);
		break;
	}
	case MXSD_BIND_UINT64: {
		uint64_t value;

		memcpy(&value, address, sizeof(value));
		format_unsigned(value, scratch);
		break;
	}
	case MXSD_BIND_ENUM: {
		size_t index;

		if (!load_enum_index(address, field->value_size, &index) ||
		    index >= field->enumeration_count) {
			return MXSD_ERR_INVALID_VALUE;
		}
		*text = field->enumeration[index];
		return MXSD_OK;
	}
	default:
		return MXSD_ERR_ARGUMENT;
	}
	*text = scratch;
	return MXSD_OK;
}

/** Return whether an inline or pointer field holds a value. */
static int field_present(const mxsd_bind_field_t *field, const void *object)
{
	const unsigned char *address = const_member(object, field->offset);

	switch (field->storage) {
	case MXSD_BIND_ARRAY:
		return load_count(const_member(object, field->count_offset)) != 0u;
	case MXSD_BIND_POINTER:
		return load_pointer(address) != NULL;
	case MXSD_BIND_INLINE:
	default:
		if (field->kind == MXSD_BIND_STRING) {
			return load_pointer(address) != NULL;
		}
		if (field->presence_offset != MXSD_BIND_NO_OFFSET) {
			return load_bool(const_member(object, field->presence_offset));
		}
		return 1;
	}
}

/** Return whether a structure produces element children or character content. */
static int type_has_content(const mxsd_bind_type_t *type, const void *object)
{
	size_t index;

	for (index = 0u; index < type->field_count; ++index) {
		const mxsd_bind_field_t *field = &type->fields[index];

		if (field->role == MXSD_BIND_ATTRIBUTE || !field_present(field, object)) {
			continue;
		}
		if (field->role == MXSD_BIND_CONTENT && field->kind == MXSD_BIND_STRING) {
			const char *text =
				(const char *)load_pointer(const_member(object, field->offset));

			if (text[0] == '\0') {
				continue;
			}
		}
		return 1;
	}
	return 0;
}

static mxsd_result_t write_complex(writer_t *writer, const char *namespace_uri, const char *name,
				   const mxsd_bind_type_t *type, const void *object, int is_root);

/** Write the attributes of one structure and forward them to the validator. */
static mxsd_result_t write_attributes(writer_t *writer, const mxsd_bind_type_t *type,
				      const void *object)
{
	size_t index;

	for (index = 0u; index < type->field_count; ++index) {
		const mxsd_bind_field_t *field = &type->fields[index];
		char scratch[INTEGER_TEXT_CAPACITY];
		const char *text;
		mxsd_error_t code;
		mxsd_result_t result;

		if (field->role != MXSD_BIND_ATTRIBUTE) {
			continue;
		}
		if (!field_present(field, object)) {
			if (!field_is_optional(field)) {
				return writer_error(writer, MXSD_ERR_MISSING_ATTRIBUTE,
						    field->name);
			}
			continue;
		}
		code = simple_text(field, const_member(object, field->offset), scratch, &text);
		if (code != MXSD_OK) {
			return writer_error(writer, code, field->name);
		}
		result = emit(writer, " ", 1u, field->name);
		if (result.code == MXSD_OK) {
			result = emit_name(writer, field->namespace_uri, field->name);
		}
		if (result.code == MXSD_OK) {
			result = emit(writer, "=\"", 2u, field->name);
		}
		if (result.code == MXSD_OK) {
			result = emit_escaped(writer, text, strlen(text), 1, field->name);
		}
		if (result.code == MXSD_OK) {
			result = emit(writer, "\"", 1u, field->name);
		}
		if (result.code == MXSD_OK && writer->validator != NULL) {
			result = mxsd_attribute(writer->validator, field->namespace_uri,
						field->name, text);
		}
		if (result.code != MXSD_OK) {
			return result;
		}
	}
	return make_result(MXSD_OK, writer->written, NULL);
}

/** Write escaped character data and forward it to the validator. */
static mxsd_result_t write_text(writer_t *writer, const char *text, const char *path)
{
	size_t length = strlen(text);
	mxsd_result_t result = emit_escaped(writer, text, length, 0, path);

	if (result.code == MXSD_OK && writer->validator != NULL && length != 0u) {
		result = mxsd_text(writer->validator, text, length);
	}
	return result;
}

/** Write one simple-typed child element. */
static mxsd_result_t write_simple_element(writer_t *writer, const mxsd_bind_field_t *field,
					  const unsigned char *address)
{
	char scratch[INTEGER_TEXT_CAPACITY];
	const char *text;
	mxsd_error_t code = simple_text(field, address, scratch, &text);
	mxsd_result_t result;

	if (code != MXSD_OK) {
		return writer_error(writer,
				    code == MXSD_ERR_ARGUMENT ? MXSD_ERR_MISSING_ELEMENT : code,
				    field->name);
	}
	result = emit(writer, "<", 1u, field->name);
	if (result.code == MXSD_OK) {
		result = emit_name(writer, field->namespace_uri, field->name);
	}
	if (result.code == MXSD_OK && writer->validator != NULL) {
		result = mxsd_start_element(writer->validator, field->namespace_uri, field->name);
		if (result.code == MXSD_OK) {
			result = mxsd_start_content(writer->validator);
		}
	}
	if (result.code != MXSD_OK) {
		return result;
	}
	if (text[0] == '\0') {
		result = emit(writer, "/>", 2u, field->name);
	} else {
		result = emit(writer, ">", 1u, field->name);
		if (result.code == MXSD_OK) {
			result = write_text(writer, text, field->name);
		}
		if (result.code == MXSD_OK) {
			result = emit(writer, "</", 2u, field->name);
		}
		if (result.code == MXSD_OK) {
			result = emit_name(writer, field->namespace_uri, field->name);
		}
		if (result.code == MXSD_OK) {
			result = emit(writer, ">", 1u, field->name);
		}
	}
	if (result.code == MXSD_OK && writer->validator != NULL) {
		result = mxsd_end_element(writer->validator, field->namespace_uri, field->name);
	}
	return result;
}

/** Write one occurrence of a child element field. */
static mxsd_result_t write_item(writer_t *writer, const mxsd_bind_field_t *field,
				const unsigned char *address)
{
	if (field->kind == MXSD_BIND_COMPLEX) {
		return write_complex(writer, field->namespace_uri, field->name, field->type,
				     address, 0);
	}
	return write_simple_element(writer, field, address);
}

/** Write every occurrence of one child element field. */
static mxsd_result_t write_element_field(writer_t *writer, const mxsd_bind_field_t *field,
					 const void *object)
{
	const unsigned char *address = const_member(object, field->offset);

	if (field->storage == MXSD_BIND_ARRAY) {
		size_t count = load_count(const_member(object, field->count_offset));
		const unsigned char *items = (const unsigned char *)load_pointer(address);
		size_t index;

		if ((unsigned long)count < field->min_occurs ||
		    (field->max_occurs != ULONG_MAX &&
		     (count > (size_t)ULONG_MAX || (unsigned long)count > field->max_occurs))) {
			return writer_error(writer, MXSD_ERR_OCCURRENCE, field->name);
		}
		if (count != 0u && items == NULL) {
			return writer_error(writer, MXSD_ERR_ARGUMENT, field->name);
		}
		for (index = 0u; index < count; ++index) {
			mxsd_result_t result =
				write_item(writer, field, items + index * field->value_size);

			if (result.code != MXSD_OK) {
				return result;
			}
		}
		return make_result(MXSD_OK, writer->written, NULL);
	}
	if (!field_present(field, object)) {
		if (!field_is_optional(field)) {
			return writer_error(writer, MXSD_ERR_MISSING_ELEMENT, field->name);
		}
		return make_result(MXSD_OK, writer->written, NULL);
	}
	if (field->storage == MXSD_BIND_POINTER) {
		return write_item(writer, field, (const unsigned char *)load_pointer(address));
	}
	return write_item(writer, field, address);
}

/** Write the character content of a simpleContent structure. */
static mxsd_result_t write_content_field(writer_t *writer, const mxsd_bind_field_t *field,
					 const void *object)
{
	char scratch[INTEGER_TEXT_CAPACITY];
	const char *text;
	mxsd_error_t code;

	if (!field_present(field, object)) {
		return field_is_optional(field)
			       ? make_result(MXSD_OK, writer->written, NULL)
			       : writer_error(writer, MXSD_ERR_INVALID_VALUE, field->name);
	}
	code = simple_text(field, const_member(object, field->offset), scratch, &text);
	if (code != MXSD_OK) {
		return writer_error(writer, code, field->name);
	}
	return write_text(writer, text, field->name);
}

/** Declare the generated namespace prefixes on the root start tag. */
static mxsd_result_t write_namespace_declarations(writer_t *writer)
{
	size_t index;

	for (index = 0u; index < writer->root->namespace_count; ++index) {
		const mxsd_bind_namespace_t *binding = &writer->root->namespaces[index];
		const char *uri = binding->namespace_uri;
		mxsd_result_t result = emit(writer, " xmlns:", 7u, writer->root->name);

		if (result.code == MXSD_OK) {
			result = emit_string(writer, binding->prefix, writer->root->name);
		}
		if (result.code == MXSD_OK) {
			result = emit(writer, "=\"", 2u, writer->root->name);
		}
		if (result.code == MXSD_OK) {
			result = emit_escaped(writer, uri, strlen(uri), 1, writer->root->name);
		}
		if (result.code == MXSD_OK) {
			result = emit(writer, "\"", 1u, writer->root->name);
		}
		if (result.code == MXSD_OK && writer->validator != NULL) {
			result = mxsd_namespace(writer->validator, binding->prefix, uri);
		}
		if (result.code != MXSD_OK) {
			return result;
		}
	}
	return make_result(MXSD_OK, writer->written, NULL);
}

/** Write one bound structure as an element with its attributes and content. */
static mxsd_result_t write_complex(writer_t *writer, const char *namespace_uri, const char *name,
				   const mxsd_bind_type_t *type, const void *object, int is_root)
{
	mxsd_result_t result;
	size_t index;

	if (object == NULL) {
		return writer_error(writer, MXSD_ERR_ARGUMENT, name);
	}
	/* The object graph is caller-provided and may contain cycles. */
	if (writer->depth >= MXSD_MAX_DEPTH) {
		return writer_error(writer, MXSD_ERR_EXCESSIVE_NESTING, name);
	}
	writer->depth++;
	result = emit(writer, "<", 1u, name);
	if (result.code == MXSD_OK) {
		result = emit_name(writer, namespace_uri, name);
	}
	if (result.code == MXSD_OK && writer->validator != NULL) {
		result = mxsd_start_element(writer->validator, namespace_uri, name);
	}
	if (result.code == MXSD_OK && is_root) {
		result = write_namespace_declarations(writer);
	}
	if (result.code == MXSD_OK) {
		result = write_attributes(writer, type, object);
	}
	if (result.code == MXSD_OK && writer->validator != NULL) {
		result = mxsd_start_content(writer->validator);
	}
	if (result.code != MXSD_OK) {
		return result;
	}
	if (!type_has_content(type, object)) {
		/* Required members are still checked when nothing would be written. */
		for (index = 0u; index < type->field_count; ++index) {
			const mxsd_bind_field_t *field = &type->fields[index];

			if (field->role == MXSD_BIND_ELEMENT) {
				result = write_element_field(writer, field, object);
			} else if (field->role == MXSD_BIND_CONTENT) {
				result = write_content_field(writer, field, object);
			}
			if (result.code != MXSD_OK) {
				return result;
			}
		}
		result = emit(writer, "/>", 2u, name);
	} else {
		result = emit(writer, ">", 1u, name);
		for (index = 0u; result.code == MXSD_OK && index < type->field_count; ++index) {
			const mxsd_bind_field_t *field = &type->fields[index];

			if (field->role == MXSD_BIND_ELEMENT) {
				result = write_element_field(writer, field, object);
			} else if (field->role == MXSD_BIND_CONTENT) {
				result = write_content_field(writer, field, object);
			}
		}
		if (result.code == MXSD_OK) {
			result = emit(writer, "</", 2u, name);
		}
		if (result.code == MXSD_OK) {
			result = emit_name(writer, namespace_uri, name);
		}
		if (result.code == MXSD_OK) {
			result = emit(writer, ">", 1u, name);
		}
	}
	if (result.code == MXSD_OK && writer->validator != NULL) {
		result = mxsd_end_element(writer->validator, namespace_uri, name);
	}
	writer->depth--;
	return result;
}

mxsd_result_t mxsd_bind_write(const mxsd_bind_root_t *root, const void *object,
			      mxsd_bind_sink_fn sink, void *context, mxsd_stream_t *validator)
{
	static const char declaration[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	writer_t writer;
	mxsd_result_t result;

	if (root == NULL || root->type == NULL || object == NULL || sink == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0u, NULL);
	}
	/* The validator must be fresh: initialized for this root, without events or errors. */
	if (validator != NULL &&
	    (root->schema == NULL || validator->schema != root->schema() ||
	     validator->result.code != MXSD_OK || validator->depth != 0u || validator->finished)) {
		return make_result(MXSD_ERR_ARGUMENT, 0u, root->name);
	}
	memset(&writer, 0, sizeof(writer));
	writer.root = root;
	writer.sink = sink;
	writer.context = context;
	writer.validator = validator;

	result = emit(&writer, declaration, sizeof(declaration) - 1u, root->name);
	if (result.code == MXSD_OK) {
		result = write_complex(&writer, root->namespace_uri, root->name, root->type, object,
				       1);
	}
	if (result.code == MXSD_OK && validator != NULL) {
		result = mxsd_stream_finish(validator);
	}
	if (result.code != MXSD_OK) {
		return result;
	}
	return make_result(MXSD_OK, writer.written, NULL);
}

/** Bounded destination used by @ref mxsd_bind_write_buffer. */
typedef struct {
	char *data;
	size_t capacity;
	size_t length;
} buffer_sink_t;

/** Append to a buffer while always keeping room for the terminating NUL. */
static int buffer_sink(void *context, const char *data, size_t length)
{
	buffer_sink_t *buffer = (buffer_sink_t *)context;

	if (length >= buffer->capacity - buffer->length) {
		return -1;
	}
	memcpy(buffer->data + buffer->length, data, length);
	buffer->length += length;
	return 0;
}

/** Validate a document with the default policy or with an explicit allocator. */
static mxsd_result_t validate_document(const mxsd_schema_t *schema, const char *xml,
				       int use_allocator, const mxsd_allocator_t *allocator)
{
	return use_allocator ? mxsd_validate_with_allocator(schema, xml, allocator)
			     : mxsd_validate(schema, xml);
}

/** Write into a buffer, then validate the document. */
static mxsd_result_t write_buffer(const mxsd_bind_root_t *root, const void *object, char *buffer,
				  size_t capacity, size_t *length, int use_allocator,
				  const mxsd_allocator_t *allocator)
{
	buffer_sink_t sink;
	mxsd_result_t result;

	if (length != NULL) {
		*length = 0u;
	}
	if (root == NULL || root->schema == NULL || buffer == NULL || capacity == 0u) {
		return make_result(MXSD_ERR_ARGUMENT, 0u, NULL);
	}
	sink.data = buffer;
	sink.capacity = capacity;
	sink.length = 0u;
	buffer[0] = '\0';
	result = mxsd_bind_write(root, object, buffer_sink, &sink, NULL);
	buffer[sink.length] = '\0';
	if (length != NULL) {
		*length = sink.length;
	}
	if (result.code != MXSD_OK) {
		return result;
	}
	return validate_document(root->schema(), buffer, use_allocator, allocator);
}

mxsd_result_t mxsd_bind_write_buffer(const mxsd_bind_root_t *root, const void *object, char *buffer,
				     size_t capacity, size_t *length)
{
	return write_buffer(root, object, buffer, capacity, length, 0, NULL);
}

mxsd_result_t mxsd_bind_write_buffer_with_allocator(const mxsd_bind_root_t *root,
						    const void *object, char *buffer,
						    size_t capacity, size_t *length,
						    const mxsd_allocator_t *allocator)
{
	return write_buffer(root, object, buffer, capacity, length, 1, allocator);
}

/* ------------------------------------------------------------------------- */
/* Reader                                                                    */
/* ------------------------------------------------------------------------- */

/** Mutable state of one deserialization. */
typedef struct {
	xml_parser_t parser;
	mxsd_bind_arena_t *arena;
} reader_t;

void mxsd_bind_arena_init(mxsd_bind_arena_t *arena, void *storage, size_t capacity)
{
	if (arena == NULL) {
		return;
	}
	arena->data = (unsigned char *)storage;
	arena->capacity = storage != NULL ? capacity : 0u;
	arena->used = 0u;
}

/** Allocate zeroed arena storage, returning @c NULL when it is exhausted. */
static void *arena_allocate(mxsd_bind_arena_t *arena, size_t size, size_t alignment)
{
	size_t start = arena->used;
	size_t misalignment = (size_t)((uintptr_t)(arena->data + start) % alignment);

	if (misalignment != 0u) {
		size_t padding = alignment - misalignment;

		if (padding > arena->capacity - start) {
			return NULL;
		}
		start += padding;
	}
	if (size > arena->capacity - start) {
		return NULL;
	}
	arena->used = start + size;
	memset(arena->data + start, 0, size);
	return arena->data + start;
}

/** Apply the XML Schema whiteSpace facet in place, returning the new length. */
static size_t normalize_in_place(char *text, size_t length, mxsd_whitespace_t mode)
{
	size_t source;
	size_t destination = 0u;
	int pending_space = 0;

	if (mode == MXSD_WS_PRESERVE) {
		return length;
	}
	for (source = 0u; source < length; ++source) {
		char character = text[source];
		int space = is_xml_space(character);

		if (mode == MXSD_WS_REPLACE) {
			text[destination++] = space ? ' ' : character;
		} else if (space) {
			pending_space = destination != 0u;
		} else {
			if (pending_space) {
				text[destination++] = ' ';
				pending_space = 0;
			}
			text[destination++] = character;
		}
	}
	return destination;
}

/** Parse an optionally signed decimal integer into a sign and magnitude. */
static int parse_magnitude(const char *text, int *negative, uint64_t *magnitude)
{
	uint64_t value = 0u;
	size_t index = 0u;

	*negative = 0;
	if (text[index] == '+' || text[index] == '-') {
		*negative = text[index] == '-';
		index++;
	}
	if (text[index] == '\0') {
		return 0;
	}
	for (; text[index] != '\0'; ++index) {
		unsigned digit;

		if (text[index] < '0' || text[index] > '9') {
			return 0;
		}
		digit = (unsigned)(text[index] - '0');
		if (value > (UINT64_MAX - digit) / 10u) {
			return 0;
		}
		value = value * 10u + digit;
	}
	*magnitude = value;
	return 1;
}

/** Parse a signed decimal integer constrained to [@p minimum, @p maximum]. */
static int parse_signed(const char *text, int64_t minimum, int64_t maximum, int64_t *value)
{
	int negative;
	uint64_t magnitude;

	if (!parse_magnitude(text, &negative, &magnitude)) {
		return 0;
	}
	if (negative) {
		if (magnitude > (uint64_t)0u - (uint64_t)minimum) {
			return 0;
		}
		*value = magnitude == 0u ? 0 : -(int64_t)(magnitude - 1u) - 1;
	} else {
		if (magnitude > (uint64_t)maximum) {
			return 0;
		}
		*value = (int64_t)magnitude;
	}
	return 1;
}

/** Parse an unsigned decimal integer no greater than @p maximum. */
static int parse_unsigned(const char *text, uint64_t maximum, uint64_t *value)
{
	int negative;
	uint64_t magnitude;

	if (!parse_magnitude(text, &negative, &magnitude) || magnitude > maximum ||
	    (negative && magnitude != 0u)) {
		return 0;
	}
	*value = magnitude;
	return 1;
}

/**
 * @brief Convert normalized lexical text and store it as a simple bound value.
 *
 * @param field Descriptor of the value.
 * @param address Address of the stored value.
 * @param text NUL-terminated normalized text. For strings it must live in the
 *        arena or in static storage, because the pointer is retained.
 * @return Non-zero when the text is valid for the representation.
 */
static int store_simple(const mxsd_bind_field_t *field, unsigned char *address, const char *text)
{
	int64_t signed_value;
	uint64_t unsigned_value;

	switch (field->kind) {
	case MXSD_BIND_STRING:
		store_pointer(address, text);
		return 1;
	case MXSD_BIND_BOOLEAN:
		if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0) {
			store_bool(address, true);
		} else if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0) {
			store_bool(address, false);
		} else {
			return 0;
		}
		return 1;
	case MXSD_BIND_INT8: {
		int8_t value;

		if (!parse_signed(text, INT8_MIN, INT8_MAX, &signed_value)) {
			return 0;
		}
		value = (int8_t)signed_value;
		memcpy(address, &value, sizeof(value));
		return 1;
	}
	case MXSD_BIND_UINT8: {
		uint8_t value;

		if (!parse_unsigned(text, UINT8_MAX, &unsigned_value)) {
			return 0;
		}
		value = (uint8_t)unsigned_value;
		memcpy(address, &value, sizeof(value));
		return 1;
	}
	case MXSD_BIND_INT16: {
		int16_t value;

		if (!parse_signed(text, INT16_MIN, INT16_MAX, &signed_value)) {
			return 0;
		}
		value = (int16_t)signed_value;
		memcpy(address, &value, sizeof(value));
		return 1;
	}
	case MXSD_BIND_UINT16: {
		uint16_t value;

		if (!parse_unsigned(text, UINT16_MAX, &unsigned_value)) {
			return 0;
		}
		value = (uint16_t)unsigned_value;
		memcpy(address, &value, sizeof(value));
		return 1;
	}
	case MXSD_BIND_INT32: {
		int32_t value;

		if (!parse_signed(text, INT32_MIN, INT32_MAX, &signed_value)) {
			return 0;
		}
		value = (int32_t)signed_value;
		memcpy(address, &value, sizeof(value));
		return 1;
	}
	case MXSD_BIND_UINT32: {
		uint32_t value;

		if (!parse_unsigned(text, UINT32_MAX, &unsigned_value)) {
			return 0;
		}
		value = (uint32_t)unsigned_value;
		memcpy(address, &value, sizeof(value));
		return 1;
	}
	case MXSD_BIND_INT64:
		if (!parse_signed(text, INT64_MIN, INT64_MAX, &signed_value)) {
			return 0;
		}
		memcpy(address, &signed_value, sizeof(signed_value));
		return 1;
	case MXSD_BIND_UINT64:
		if (!parse_unsigned(text, UINT64_MAX, &unsigned_value)) {
			return 0;
		}
		memcpy(address, &unsigned_value, sizeof(unsigned_value));
		return 1;
	case MXSD_BIND_ENUM: {
		size_t index;

		for (index = 0u; index < field->enumeration_count; ++index) {
			if (strcmp(field->enumeration[index], text) == 0) {
				return store_enum_index(address, field->value_size, index);
			}
		}
		return 0;
	}
	default:
		return 0;
	}
}

/**
 * @brief Normalize text already copied into the arena and store it.
 *
 * Arena storage holding the text is released again for non-string values,
 * whose converted representation does not reference it.
 */
static mxsd_result_t finish_simple(reader_t *reader, const mxsd_bind_field_t *field,
				   unsigned char *address, char *text, size_t length, size_t mark,
				   size_t offset)
{
	/* The arena holds length + 1 bytes for the text; normalization only shrinks it. */
	length = normalize_in_place(text, length, field->whitespace);
	text[length] = '\0';
	if (!store_simple(field, address, text)) {
		return make_result(MXSD_ERR_INVALID_VALUE, offset, field->name);
	}
	if (field->kind != MXSD_BIND_STRING) {
		reader->arena->used = mark;
	} else {
		reader->arena->used =
			(size_t)((unsigned char *)text - reader->arena->data) + length + 1u;
	}
	return make_result(MXSD_OK, offset, NULL);
}

/** Copy a schema-provided lexical value into the arena and store it. */
static mxsd_result_t store_lexical(reader_t *reader, const mxsd_bind_field_t *field,
				   unsigned char *address, const char *value, size_t offset)
{
	size_t mark = reader->arena->used;
	size_t length = strlen(value);
	char *text = (char *)arena_allocate(reader->arena, length + 1u, 1u);

	if (text == NULL) {
		return make_result(MXSD_ERR_RESOURCE_LIMIT, offset, field->name);
	}
	memcpy(text, value, length);
	return finish_simple(reader, field, address, text, length, mark, offset);
}

/**
 * @brief Decode an attribute value into the arena and store it.
 *
 * Literal tab, line feed and carriage return become spaces as required by
 * XML attribute-value normalization; character references keep them.
 */
static mxsd_result_t store_attribute(reader_t *reader, const mxsd_bind_field_t *field,
				     unsigned char *address, text_span_t raw, size_t offset)
{
	size_t mark = reader->arena->used;
	char *text = (char *)arena_allocate(reader->arena, raw.length + 1u, 1u);
	text_span_t copy;
	text_span_t decoded;
	size_t index;

	if (text == NULL) {
		return make_result(MXSD_ERR_RESOURCE_LIMIT, offset, field->name);
	}
	for (index = 0u; index < raw.length; ++index) {
		char character = raw.data[index];

		text[index] = is_xml_space(character) ? ' ' : character;
	}
	copy.data = text;
	copy.length = raw.length;
	/* Decoding never lengthens text, so it can safely run in place. */
	if (!mxsd_decode_xml_references(copy, &decoded, text)) {
		reader->arena->used = mark;
		return make_result(MXSD_ERR_INVALID_VALUE, offset, field->name);
	}
	return finish_simple(reader, field, address, text, decoded.length, mark, offset);
}

/**
 * @brief Read the character content of the current element into the arena.
 *
 * Text is entity-decoded, CDATA sections are copied verbatim, and comments and
 * processing instructions are skipped. The parser stops on the end tag.
 */
static mxsd_result_t read_text(reader_t *reader, const char *path, char **text, size_t *length)
{
	xml_parser_t *parser = &reader->parser;
	mxsd_bind_arena_t *arena = reader->arena;
	size_t start = arena->used;
	size_t total = 0u;

	*text = (char *)(arena->data + start);
	while (parser->position < parser->length) {
		const char *input = parser->input + parser->position;
		size_t available = arena->capacity - arena->used;
		const char *end;

		if (*input != '<') {
			size_t segment_start = parser->position;
			text_span_t raw;
			text_span_t decoded;

			while (parser->position < parser->length &&
			       parser->input[parser->position] != '<') {
				parser->position++;
			}
			raw.data = parser->input + segment_start;
			raw.length = parser->position - segment_start;
			/* Room for the raw bytes plus the decoder's terminating NUL. */
			if (raw.length >= available) {
				return make_result(MXSD_ERR_RESOURCE_LIMIT, segment_start, path);
			}
			if (!mxsd_decode_xml_references(raw, &decoded,
							(char *)(arena->data + arena->used))) {
				return make_result(MXSD_ERR_INVALID_VALUE, segment_start, path);
			}
			arena->used += decoded.length;
			total += decoded.length;
			continue;
		}
		if (parser->position + 1u < parser->length && input[1] == '/') {
			break;
		}
		if (strncmp(input, "<!--", 4u) == 0) {
			end = strstr(input + 4, "-->");
			if (end == NULL) {
				return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
			}
			parser->position = (size_t)(end - parser->input) + 3u;
			continue;
		}
		if (strncmp(input, "<?", 2u) == 0) {
			end = strstr(input + 2, "?>");
			if (end == NULL) {
				return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
			}
			parser->position = (size_t)(end - parser->input) + 2u;
			continue;
		}
		if (strncmp(input, "<![CDATA[", 9u) == 0) {
			size_t section;

			end = strstr(input + 9, "]]>");
			if (end == NULL) {
				return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
			}
			section = (size_t)(end - (input + 9));
			if (section >= available) {
				return make_result(MXSD_ERR_RESOURCE_LIMIT, parser->position, path);
			}
			memcpy(arena->data + arena->used, input + 9, section);
			arena->used += section;
			total += section;
			parser->position = (size_t)(end - parser->input) + 3u;
			continue;
		}
		return make_result(MXSD_ERR_UNEXPECTED_ELEMENT, parser->position, path);
	}
	if (arena->used >= arena->capacity) {
		return make_result(MXSD_ERR_RESOURCE_LIMIT, parser->position, path);
	}
	/* Reserve the terminating NUL written by finish_simple(). */
	arena->data[arena->used++] = '\0';
	*length = total;
	return make_result(MXSD_OK, parser->position, NULL);
}

/** Consume the end tag matching @p tag. */
static mxsd_result_t consume_end_tag(reader_t *reader, const start_tag_t *tag, const char *path)
{
	xml_parser_t *parser = &reader->parser;
	text_span_t name;

	if (!at_end_element(parser)) {
		return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
	}
	parser->position += 2u;
	if (!parse_xml_name(parser, &name) || name.length != tag->name.length ||
	    memcmp(name.data, tag->name.data, name.length) != 0) {
		return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
	}
	skip_whitespace(parser);
	if (parser->position >= parser->length || parser->input[parser->position] != '>') {
		return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
	}
	parser->position++;
	return make_result(MXSD_OK, parser->position, NULL);
}

/** Skip comments, processing instructions and whitespace-only CDATA between children. */
static void skip_between_children(xml_parser_t *parser)
{
	for (;;) {
		skip_ignorable_markup(parser);
		if (parser->position + 9u <= parser->length &&
		    memcmp(parser->input + parser->position, "<![CDATA[", 9u) == 0) {
			const char *end = strstr(parser->input + parser->position + 9u, "]]>");

			if (end == NULL) {
				parser->position = parser->length;
				return;
			}
			parser->position = (size_t)(end - parser->input) + 3u;
			continue;
		}
		return;
	}
}

/** Resolve the expanded name of a start tag and find the matching element field. */
static const mxsd_bind_field_t *find_element_field(const xml_parser_t *parser,
						   const mxsd_bind_type_t *type,
						   text_span_t qualified_name)
{
	text_span_t prefix;
	text_span_t local_name;
	text_span_t namespace_uri;
	size_t index;

	split_qualified_name(qualified_name, &prefix, &local_name);
	namespace_uri = resolve_namespace(parser, prefix, 0);
	if (prefix.length != 0u && namespace_uri.length == 0u) {
		return NULL;
	}
	for (index = 0u; index < type->field_count; ++index) {
		const mxsd_bind_field_t *field = &type->fields[index];

		if (field->role == MXSD_BIND_ELEMENT &&
		    span_equals_string(local_name, field->name) &&
		    namespace_matches(field->namespace_uri, namespace_uri)) {
			return field;
		}
	}
	return NULL;
}

/**
 * @brief Check an @c xsi:type value against the structure being filled.
 *
 * A valid document may substitute a type derived from the declared one. Such
 * a document is valid but cannot be represented by the declared structure.
 */
static int xsi_type_matches(const xml_parser_t *parser, const mxsd_bind_type_t *type,
			    text_span_t value)
{
	text_span_t prefix;
	text_span_t local_name;
	text_span_t namespace_uri;

	if (type->type_name == NULL) {
		return 0;
	}
	split_qualified_name(trim_span(value), &prefix, &local_name);
	namespace_uri = resolve_namespace(parser, prefix, 0);
	if (prefix.length != 0u && namespace_uri.length == 0u) {
		return 0;
	}
	return span_equals_string(local_name, type->type_name) &&
	       namespace_matches(type->type_namespace, namespace_uri);
}

/** Return whether an @c xsi:nil value is true. */
static int xsi_nil_is_true(text_span_t value)
{
	value = trim_span(value);
	return span_equals_string(value, "true") || span_equals_string(value, "1");
}

/**
 * @brief Store the attributes of the current start tag into a structure.
 *
 * @param type Structure descriptor, or @c NULL for a simple-typed element that
 *        can only carry @c xsi attributes.
 * @param object Structure being filled, or @c NULL with @p type.
 */
static mxsd_result_t read_attributes(reader_t *reader, const start_tag_t *tag,
				     const mxsd_bind_type_t *type, void *object, const char *path)
{
	xml_parser_t *parser = &reader->parser;
	size_t saved = parser->position;
	size_t index;

	parser->position = tag->attributes_offset;
	for (;;) {
		text_span_t name;
		text_span_t value;
		text_span_t prefix;
		text_span_t local_name;
		text_span_t namespace_uri;
		const mxsd_bind_field_t *match = NULL;
		size_t offset;
		mxsd_result_t result;

		skip_whitespace(parser);
		if (at_end_of_start_tag(parser)) {
			break;
		}
		offset = parser->position;
		if (!parse_attribute(parser, &name, &value)) {
			return make_result(MXSD_ERR_MALFORMED_XML, offset, path);
		}
		if (is_namespace_declaration(name)) {
			continue;
		}
		split_qualified_name(name, &prefix, &local_name);
		namespace_uri = resolve_namespace(parser, prefix, 1);
		if (span_equals_string(namespace_uri, xsi_namespace)) {
			if (span_equals_string(local_name, "type")) {
				if (type != NULL && !xsi_type_matches(parser, type, value)) {
					return make_result(MXSD_ERR_UNSUPPORTED, offset, path);
				}
			} else if (span_equals_string(local_name, "nil")) {
				if (xsi_nil_is_true(value)) {
					return make_result(MXSD_ERR_UNSUPPORTED, offset, path);
				}
			}
			continue;
		}
		for (index = 0u; type != NULL && index < type->field_count; ++index) {
			const mxsd_bind_field_t *field = &type->fields[index];

			if (field->role == MXSD_BIND_ATTRIBUTE &&
			    span_equals_string(local_name, field->name) &&
			    namespace_matches(field->namespace_uri, namespace_uri)) {
				match = field;
				break;
			}
		}
		if (match == NULL) {
			return make_result(MXSD_ERR_UNEXPECTED_ATTRIBUTE, offset, path);
		}
		result = store_attribute(reader, match, member(object, match->offset), value,
					 offset);
		if (result.code != MXSD_OK) {
			return result;
		}
		if (match->presence_offset != MXSD_BIND_NO_OFFSET) {
			store_bool(member(object, match->presence_offset), true);
		}
	}
	parser->position = saved;

	/* Absent attributes take their schema default or fixed value. */
	for (index = 0u; type != NULL && index < type->field_count; ++index) {
		const mxsd_bind_field_t *field = &type->fields[index];
		const char *value =
			field->fixed_value != NULL ? field->fixed_value : field->default_value;
		mxsd_result_t result;

		if (field->role != MXSD_BIND_ATTRIBUTE || value == NULL ||
		    field_present(field, object)) {
			continue;
		}
		result = store_lexical(reader, field, member(object, field->offset), value,
				       tag->start_offset);
		if (result.code != MXSD_OK) {
			return result;
		}
		if (field->presence_offset != MXSD_BIND_NO_OFFSET) {
			store_bool(member(object, field->presence_offset), true);
		}
	}
	return make_result(MXSD_OK, parser->position, NULL);
}

/**
 * @brief Read simple content and store it, applying a default or fixed value.
 *
 * Element default and fixed values apply when the element is present with
 * empty content, as specified by XML Schema.
 */
static mxsd_result_t read_simple_content(reader_t *reader, const start_tag_t *tag,
					 const mxsd_bind_field_t *field, unsigned char *address,
					 const char *default_value, const char *fixed_value)
{
	size_t mark = reader->arena->used;
	const char *fallback = fixed_value != NULL ? fixed_value : default_value;
	char *text = NULL;
	size_t length = 0u;
	mxsd_result_t result;

	if (!tag->is_empty) {
		result = read_text(reader, field->name, &text, &length);
		if (result.code != MXSD_OK) {
			return result;
		}
		if (length != 0u || fallback == NULL) {
			return finish_simple(reader, field, address, text, length, mark,
					     tag->content_offset);
		}
		reader->arena->used = mark;
	}
	return store_lexical(reader, field, address, fallback != NULL ? fallback : "",
			     tag->start_offset);
}

/**
 * @brief Count repeated children and allocate their arrays before filling them.
 *
 * Arrays must be contiguous while nested values are allocated from the same
 * arena, so their sizes are determined by a first pass over the children. The
 * count members serve as counters and are reset for the filling pass.
 */
static mxsd_result_t allocate_arrays(reader_t *reader, const mxsd_bind_type_t *type, void *object,
				     const char *path)
{
	xml_parser_t *parser = &reader->parser;
	size_t saved_position = parser->position;
	size_t saved_namespaces = parser->namespace_count;
	size_t index;
	int has_arrays = 0;

	for (index = 0u; index < type->field_count; ++index) {
		if (type->fields[index].storage == MXSD_BIND_ARRAY) {
			has_arrays = 1;
			store_count(member(object, type->fields[index].count_offset), 0u);
		}
	}
	if (!has_arrays) {
		return make_result(MXSD_OK, parser->position, NULL);
	}
	for (;;) {
		start_tag_t child;
		const mxsd_bind_field_t *field;
		size_t child_position;
		mxsd_result_t result;

		skip_between_children(parser);
		if (parser->position >= parser->length || at_end_element(parser)) {
			break;
		}
		child_position = parser->position;
		result = parse_start_tag(parser, path, &child);
		if (result.code != MXSD_OK) {
			return result;
		}
		field = find_element_field(parser, type, child.name);
		parser->namespace_count = child.namespace_base;
		if (field != NULL && field->storage == MXSD_BIND_ARRAY) {
			unsigned char *count_address = member(object, field->count_offset);
			size_t count = load_count(count_address);

			if (count == (size_t)-1) {
				return make_result(MXSD_ERR_RESOURCE_LIMIT, child_position, path);
			}
			store_count(count_address, count + 1u);
		}
		parser->position = child_position;
		result = skip_any_element(parser, path);
		if (result.code != MXSD_OK) {
			return result;
		}
	}
	parser->position = saved_position;
	parser->namespace_count = saved_namespaces;

	for (index = 0u; index < type->field_count; ++index) {
		const mxsd_bind_field_t *field = &type->fields[index];
		unsigned char *count_address;
		size_t count;
		void *items;

		if (field->storage != MXSD_BIND_ARRAY) {
			continue;
		}
		count_address = member(object, field->count_offset);
		count = load_count(count_address);
		store_count(count_address, 0u);
		if (count == 0u) {
			continue;
		}
		if (count > ((size_t)-1) / field->value_size) {
			return make_result(MXSD_ERR_RESOURCE_LIMIT, saved_position, field->name);
		}
		items = arena_allocate(reader->arena, count * field->value_size, BIND_ALIGNMENT);
		if (items == NULL) {
			return make_result(MXSD_ERR_RESOURCE_LIMIT, saved_position, field->name);
		}
		store_pointer(member(object, field->offset), items);
	}
	return make_result(MXSD_OK, parser->position, NULL);
}

static mxsd_result_t read_complex(reader_t *reader, const start_tag_t *tag,
				  const mxsd_bind_type_t *type, void *object,
				  const char *default_value, const char *fixed_value);

/** Return the storage receiving the next occurrence of a child element field. */
static unsigned char *child_target(reader_t *reader, const mxsd_bind_field_t *field, void *object,
				   size_t offset, mxsd_result_t *result)
{
	unsigned char *address = member(object, field->offset);

	*result = make_result(MXSD_OK, offset, NULL);
	if (field->storage == MXSD_BIND_ARRAY) {
		unsigned char *count_address = member(object, field->count_offset);
		size_t count = load_count(count_address);
		unsigned char *items = (unsigned char *)load_arena_pointer(address);

		/* The first pass allocated exactly one slot for every occurrence. */
		if (items == NULL) {
			*result = make_result(MXSD_ERR_RESOURCE_LIMIT, offset, field->name);
			return NULL;
		}
		store_count(count_address, count + 1u);
		return items + count * field->value_size;
	}
	if (field->storage == MXSD_BIND_POINTER) {
		void *item = arena_allocate(reader->arena, field->value_size, BIND_ALIGNMENT);

		if (item == NULL) {
			*result = make_result(MXSD_ERR_RESOURCE_LIMIT, offset, field->name);
			return NULL;
		}
		store_pointer(address, item);
		return (unsigned char *)item;
	}
	if (field->presence_offset != MXSD_BIND_NO_OFFSET) {
		store_bool(member(object, field->presence_offset), true);
	}
	return address;
}

/** Fill the element children of a structure. */
static mxsd_result_t read_children(reader_t *reader, const mxsd_bind_type_t *type, void *object,
				   const char *path)
{
	xml_parser_t *parser = &reader->parser;
	mxsd_result_t result = allocate_arrays(reader, type, object, path);

	if (result.code != MXSD_OK) {
		return result;
	}
	for (;;) {
		start_tag_t child;
		const mxsd_bind_field_t *field;
		unsigned char *target;

		skip_between_children(parser);
		if (parser->position >= parser->length) {
			return make_result(MXSD_ERR_MALFORMED_XML, parser->position, path);
		}
		if (at_end_element(parser)) {
			return make_result(MXSD_OK, parser->position, NULL);
		}
		if (parser->input[parser->position] != '<') {
			return make_result(MXSD_ERR_TEXT_NOT_ALLOWED, parser->position, path);
		}
		result = parse_start_tag(parser, path, &child);
		if (result.code != MXSD_OK) {
			return result;
		}
		field = find_element_field(parser, type, child.name);
		if (field == NULL) {
			return make_result(MXSD_ERR_UNEXPECTED_ELEMENT, child.start_offset, path);
		}
		target = child_target(reader, field, object, child.start_offset, &result);
		if (target == NULL) {
			return result;
		}
		if (field->kind == MXSD_BIND_COMPLEX) {
			result = read_complex(reader, &child, field->type, target,
					      field->default_value, field->fixed_value);
		} else {
			result = read_attributes(reader, &child, NULL, NULL, field->name);
			if (result.code == MXSD_OK) {
				result = read_simple_content(reader, &child, field, target,
							     field->default_value,
							     field->fixed_value);
			}
		}
		if (result.code == MXSD_OK && !child.is_empty) {
			result = consume_end_tag(reader, &child, field->name);
		}
		if (result.code != MXSD_OK) {
			return result;
		}
		parser->namespace_count = child.namespace_base;
	}
}

/** Find the character-content field of a simpleContent structure, if any. */
static const mxsd_bind_field_t *content_field(const mxsd_bind_type_t *type)
{
	size_t index;

	for (index = 0u; index < type->field_count; ++index) {
		if (type->fields[index].role == MXSD_BIND_CONTENT) {
			return &type->fields[index];
		}
	}
	return NULL;
}

/**
 * @brief Fill a structure from the element whose start tag was just parsed.
 *
 * The parser is left on the element's end tag, or after the start tag of an
 * empty element. Recursion is bounded by the validated document depth.
 */
static mxsd_result_t read_complex(reader_t *reader, const start_tag_t *tag,
				  const mxsd_bind_type_t *type, void *object,
				  const char *default_value, const char *fixed_value)
{
	const mxsd_bind_field_t *content = content_field(type);
	mxsd_result_t result = read_attributes(reader, tag, type, object, type->name);

	if (result.code != MXSD_OK) {
		return result;
	}
	if (content != NULL) {
		return read_simple_content(reader, tag, content, member(object, content->offset),
					   default_value, fixed_value);
	}
	if (tag->is_empty) {
		/* Array counts and pointers are already zero in a zeroed structure. */
		return make_result(MXSD_OK, tag->content_offset, NULL);
	}
	return read_children(reader, type, object, type->name);
}

/** Position the parser on the root start tag, as the validator does. */
static mxsd_result_t reader_start(reader_t *reader, const mxsd_schema_t *schema, const char *xml)
{
	xml_parser_t *parser = &reader->parser;
	mxsd_result_t result;

	memset(parser, 0, sizeof(*parser));
	parser->input = xml;
	parser->length = strlen(xml);
	parser->global_elements = schema->global_elements;
	parser->global_element_count = schema->global_element_count;
	parser->global_attributes = schema->global_attributes;
	parser->global_attribute_count = schema->global_attribute_count;
	parser->named_types = schema->named_types;
	parser->named_type_count = schema->named_type_count;
	parser->notations = schema->notations;
	parser->notation_count = schema->notation_count;
	if (parser->length >= 3u && (unsigned char)xml[0] == 0xefu &&
	    (unsigned char)xml[1] == 0xbbu && (unsigned char)xml[2] == 0xbfu) {
		parser->position = 3u;
	}
	skip_whitespace(parser);
	result = skip_xml_declaration(parser);
	if (result.code == MXSD_OK) {
		skip_ignorable_markup(parser);
	}
	return result;
}

/**
 * @brief Fill a structure from an already validated document.
 *
 * The parser state lives in this frame only. Keeping it out of the caller,
 * which runs validation first, prevents the two peak stack usages from adding
 * up, so this function must not be inlined.
 */
static MXSD_NOINLINE mxsd_result_t fill_object(const mxsd_bind_root_t *root,
					       const mxsd_schema_t *schema, const char *xml,
					       void *object, mxsd_bind_arena_t *arena)
{
	reader_t reader;
	start_tag_t tag;
	mxsd_result_t result;

	reader.arena = arena;
	result = reader_start(&reader, schema, xml);
	if (result.code == MXSD_OK) {
		result = parse_start_tag(&reader.parser, root->name, &tag);
	}
	if (result.code == MXSD_OK &&
	    !expanded_name_equals(&reader.parser, tag.name, root->name, root->namespace_uri, 0)) {
		result = make_result(MXSD_ERR_WRONG_ROOT, tag.start_offset, root->name);
	}
	if (result.code == MXSD_OK) {
		result = read_complex(&reader, &tag, root->type, object, root->default_value,
				      root->fixed_value);
	}
	if (result.code == MXSD_OK && !tag.is_empty) {
		result = consume_end_tag(&reader, &tag, root->name);
	}
	if (result.code != MXSD_OK) {
		return result;
	}
	return make_result(MXSD_OK, reader.parser.position, NULL);
}

/** Validate a document, then fill a structure from it. */
static mxsd_result_t bind_read(const mxsd_bind_root_t *root, const char *xml, void *object,
			       mxsd_bind_arena_t *arena, int use_allocator,
			       const mxsd_allocator_t *allocator)
{
	const mxsd_schema_t *schema;
	size_t mark;
	mxsd_result_t result;

	if (root == NULL || root->type == NULL || root->schema == NULL || xml == NULL ||
	    object == NULL || arena == NULL || arena->used > arena->capacity ||
	    (arena->data == NULL && arena->capacity != 0u)) {
		return make_result(MXSD_ERR_ARGUMENT, 0u, NULL);
	}
	schema = root->schema();
	memset(object, 0, root->type->size);
	result = validate_document(schema, xml, use_allocator, allocator);
	if (result.code != MXSD_OK) {
		return result;
	}
	mark = arena->used;
	result = fill_object(root, schema, xml, object, arena);
	if (result.code != MXSD_OK) {
		/* Never leave pointers into released arena storage. */
		memset(object, 0, root->type->size);
		arena->used = mark;
	}
	return result;
}

mxsd_result_t mxsd_bind_read(const mxsd_bind_root_t *root, const char *xml, void *object,
			     mxsd_bind_arena_t *arena)
{
	return bind_read(root, xml, object, arena, 0, NULL);
}

mxsd_result_t mxsd_bind_read_with_allocator(const mxsd_bind_root_t *root, const char *xml,
					    void *object, mxsd_bind_arena_t *arena,
					    const mxsd_allocator_t *allocator)
{
	return bind_read(root, xml, object, arena, 1, allocator);
}
