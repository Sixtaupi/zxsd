/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Allocation-free incremental XML tokenizer for streamed validation.
 */

#include "mxsd_internal.h"

#include <ctype.h>
#include <string.h>

#define XMLNS_URI "http://www.w3.org/2000/xmlns/"
#define XSI_URI   "http://www.w3.org/2001/XMLSchema-instance"

/** Store the first reader failure with an absolute byte offset. */
static mxsd_result_t reader_fail(mxsd_xml_reader_t *reader, mxsd_error_t code, size_t offset,
				 const char *path)
{
	if (reader->result.code == MXSD_OK) {
		reader->result = make_result(code, offset, path);
	}
	return reader->result;
}

/** Forward a validator result while translating its event index to an input offset. */
static mxsd_result_t reader_forward(mxsd_xml_reader_t *reader, mxsd_result_t result)
{
	if (result.code != MXSD_OK) {
		return reader_fail(reader, result.code, reader->token_offset, result.path);
	}
	return reader->result;
}

/** Notify the optional observer about an event accepted by the validator. */
static void notify_event(mxsd_xml_reader_t *reader, mxsd_xml_event_t event,
			 const char *namespace_uri, const char *local_name, const char *value,
			 size_t value_length)
{
	if (reader->event_callback != NULL) {
		reader->event_callback(reader->event_user_data, event, namespace_uri, local_name,
				       value, value_length);
	}
}

/** Validate and observe one end-element event. */
static mxsd_result_t emit_end_element(mxsd_xml_reader_t *reader, const char *namespace_uri,
				      const char *local_name)
{
	mxsd_result_t result = reader_forward(
		reader, mxsd_end_element(&reader->validator, namespace_uri, local_name));

	if (result.code == MXSD_OK) {
		notify_event(reader, MXSD_XML_EVENT_END_ELEMENT, namespace_uri, local_name, NULL,
			     0u);
		return reader->result;
	}
	return result;
}

/** Copy one parser span into bounded NUL-terminated storage. */
static int span_copy(text_span_t span, char *destination, size_t capacity)
{
	if (span.length >= capacity) {
		return 0;
	}
	if (span.length != 0u) {
		memcpy(destination, span.data, span.length);
	}
	destination[span.length] = '\0';
	return 1;
}

/** Initialize a pull-parser view with namespaces currently active in the validator. */
static void prepare_parser(const mxsd_xml_reader_t *reader, xml_parser_t *parser)
{
	size_t index;

	parser->input = reader->token;
	parser->length = reader->token_length;
	parser->position = 0u;
	parser->depth = 0u;
	parser->namespace_count = 0u;
	parser->global_elements = NULL;
	parser->global_element_count = 0u;
	parser->global_attributes = NULL;
	parser->global_attribute_count = 0u;
	parser->named_types = NULL;
	parser->named_type_count = 0u;
	parser->notations = NULL;
	parser->notation_count = 0u;
	parser->mixed_content = 0;
	parser->values_are_decoded = 0;
	parser->id_count = 0u;
	parser->idref_count = 0u;
	parser->identity_text_length = 0u;
	for (index = 0u; index < reader->validator.namespace_count; ++index) {
		parser->namespaces[index].prefix.data = reader->validator.namespaces[index].prefix;
		parser->namespaces[index].prefix.length =
			strlen(reader->validator.namespaces[index].prefix);
		parser->namespaces[index].uri.data = reader->validator.namespaces[index].uri;
		parser->namespaces[index].uri.length =
			strlen(reader->validator.namespaces[index].uri);
	}
	parser->namespace_count = reader->validator.namespace_count;
}

/** Decode XML references and apply XML attribute whitespace normalization. */
static int decode_attribute(mxsd_xml_reader_t *reader, text_span_t raw, char *output,
			    size_t capacity)
{
	text_span_t decoded;
	size_t source;
	size_t target = 0u;

	if (raw.length > MXSD_MAX_TEXT_LENGTH ||
	    !mxsd_decode_xml_references(raw, &decoded, reader->decoded) ||
	    decoded.length >= capacity) {
		return 0;
	}
	for (source = 0u; source < decoded.length; ++source) {
		unsigned char value = (unsigned char)decoded.data[source];

		if (value == '\r' && source + 1u < decoded.length &&
		    decoded.data[source + 1u] == '\n') {
			source++;
		}
		output[target++] =
			value == '\t' || value == '\r' || value == '\n' ? ' ' : (char)value;
	}
	output[target] = '\0';
	return 1;
}

/** Resolve a lexical QName through a prepared parser namespace stack. */
static int resolve_name(const xml_parser_t *parser, text_span_t lexical, int attribute, char *local,
			size_t local_capacity, char *uri, size_t uri_capacity)
{
	text_span_t prefix;
	text_span_t local_span;
	text_span_t uri_span;

	mxsd_split_qualified_name(lexical, &prefix, &local_span);
	uri_span = mxsd_resolve_namespace(parser, prefix, attribute);
	if (prefix.length != 0u && uri_span.length == 0u) {
		return 0;
	}
	return span_copy(local_span, local, local_capacity) &&
	       span_copy(uri_span, uri, uri_capacity);
}

/** Emit an xsi:type attribute before attributes of the selected dynamic type. */
static mxsd_result_t emit_xsi_type(mxsd_xml_reader_t *reader, xml_parser_t *parser,
				   const start_tag_t *tag)
{
	size_t count = 0u;

	parser->position = tag->attributes_offset;
	while (count < tag->attribute_count) {
		text_span_t name;
		text_span_t value;
		text_span_t prefix;
		text_span_t local;
		text_span_t uri;

		mxsd_skip_whitespace(parser);
		if (mxsd_at_end_of_start_tag(parser) ||
		    !mxsd_parse_attribute(parser, &name, &value)) {
			return reader_fail(reader, MXSD_ERR_MALFORMED_XML, reader->token_offset,
					   NULL);
		}
		if (mxsd_is_namespace_declaration(name)) {
			continue;
		}
		count++;
		mxsd_split_qualified_name(name, &prefix, &local);
		uri = mxsd_resolve_namespace(parser, prefix, 1);
		if (prefix.length != 0u && uri.length == 0u) {
			return reader_fail(reader, MXSD_ERR_MALFORMED_XML, reader->token_offset,
					   NULL);
		}
		if (local.length == 4u && !memcmp(local.data, "type", 4u) &&
		    mxsd_span_equals_string(uri, XSI_URI)) {
			char lexical[MXSD_MAX_TEXT_LENGTH + 1u];
			xml_parser_t value_parser = *parser;
			text_span_t qualified;
			text_span_t type_prefix;
			text_span_t type_local;
			text_span_t type_uri;
			char local_name[MXSD_STREAM_NAME_CAPACITY];
			char namespace_uri[MXSD_STREAM_NAME_CAPACITY];

			if (!decode_attribute(reader, value, lexical, sizeof(lexical))) {
				return reader_fail(reader, MXSD_ERR_STREAM_LIMIT,
						   reader->token_offset, "xsi:type");
			}
			qualified.data = lexical;
			qualified.length = strlen(lexical);
			mxsd_split_qualified_name(qualified, &type_prefix, &type_local);
			type_uri = mxsd_resolve_namespace(&value_parser, type_prefix, 0);
			if (type_local.length == 0u ||
			    (type_prefix.length != 0u && type_uri.length == 0u) ||
			    !span_copy(type_local, local_name, sizeof(local_name)) ||
			    !span_copy(type_uri, namespace_uri, sizeof(namespace_uri))) {
				return reader_fail(reader, MXSD_ERR_MALFORMED_XML,
						   reader->token_offset, "xsi:type");
			}
			return reader_forward(reader, mxsd_xsi_type(&reader->validator,
								    namespace_uri, local_name));
		}
	}
	return reader->result;
}

/** Emit every non-namespace, non-xsi:type attribute of a start tag. */
static mxsd_result_t emit_attributes(mxsd_xml_reader_t *reader, xml_parser_t *parser,
				     const start_tag_t *tag)
{
	size_t count = 0u;

	parser->position = tag->attributes_offset;
	while (count < tag->attribute_count) {
		text_span_t name;
		text_span_t value;
		char local[MXSD_STREAM_NAME_CAPACITY];
		char uri[MXSD_STREAM_NAME_CAPACITY];
		char decoded[MXSD_MAX_TEXT_LENGTH + 1u];

		mxsd_skip_whitespace(parser);
		if (mxsd_at_end_of_start_tag(parser) ||
		    !mxsd_parse_attribute(parser, &name, &value)) {
			return reader_fail(reader, MXSD_ERR_MALFORMED_XML, reader->token_offset,
					   NULL);
		}
		if (mxsd_is_namespace_declaration(name)) {
			continue;
		}
		count++;
		if (!resolve_name(parser, name, 1, local, sizeof(local), uri, sizeof(uri)) ||
		    !decode_attribute(reader, value, decoded, sizeof(decoded))) {
			return reader_fail(reader, MXSD_ERR_STREAM_LIMIT, reader->token_offset,
					   NULL);
		}
		if (!strcmp(uri, XSI_URI) && !strcmp(local, "type")) {
			continue;
		}
		if (!strcmp(uri, XMLNS_URI)) {
			return reader_fail(reader, MXSD_ERR_MALFORMED_XML, reader->token_offset,
					   local);
		}
		if (reader_forward(reader, mxsd_attribute(&reader->validator, uri, local, decoded))
			    .code != MXSD_OK) {
			return reader->result;
		}
		notify_event(reader, MXSD_XML_EVENT_ATTRIBUTE, uri, local, decoded,
			     strlen(decoded));
		if (reader->result.code != MXSD_OK) {
			return reader->result;
		}
	}
	return reader->result;
}

/** Parse and emit a complete start tag retained in the reader token. */
static mxsd_result_t process_start_tag(mxsd_xml_reader_t *reader)
{
	xml_parser_t parser;
	start_tag_t tag;
	text_span_t prefix;
	text_span_t local_span;
	text_span_t uri_span;
	char local[MXSD_STREAM_NAME_CAPACITY];
	char uri[MXSD_STREAM_NAME_CAPACITY];
	char namespace_uris[MXSD_MAX_NAMESPACES][MXSD_STREAM_NAME_CAPACITY];
	size_t namespace_base;
	size_t index;
	mxsd_result_t result;

	prepare_parser(reader, &parser);
	result = mxsd_parse_start_tag(&parser, NULL, &tag);
	if (result.code != MXSD_OK || parser.position != parser.length) {
		return reader_fail(reader, MXSD_ERR_MALFORMED_XML, reader->token_offset, NULL);
	}
	namespace_base = tag.namespace_base;
	for (index = namespace_base; index < parser.namespace_count; ++index) {
		text_span_t decoded;

		if (parser.namespaces[index].uri.length > MXSD_MAX_TEXT_LENGTH ||
		    !mxsd_decode_xml_references(parser.namespaces[index].uri, &decoded,
						reader->decoded) ||
		    !span_copy(decoded, namespace_uris[index], sizeof(namespace_uris[index]))) {
			return reader_fail(reader, MXSD_ERR_STREAM_LIMIT, reader->token_offset,
					   "xmlns");
		}
		parser.namespaces[index].uri.data = namespace_uris[index];
		parser.namespaces[index].uri.length = strlen(namespace_uris[index]);
	}
	mxsd_split_qualified_name(tag.name, &prefix, &local_span);
	uri_span = mxsd_resolve_namespace(&parser, prefix, 0);
	if ((prefix.length != 0u && uri_span.length == 0u) ||
	    !span_copy(local_span, local, sizeof(local)) ||
	    !span_copy(uri_span, uri, sizeof(uri))) {
		return reader_fail(reader, MXSD_ERR_MALFORMED_XML, reader->token_offset, NULL);
	}
	result = reader_forward(reader, mxsd_start_element(&reader->validator, uri, local));
	if (result.code != MXSD_OK) {
		return result;
	}
	notify_event(reader, MXSD_XML_EVENT_START_ELEMENT, uri, local, NULL, 0u);
	if (reader->result.code != MXSD_OK) {
		return reader->result;
	}
	for (index = namespace_base; index < parser.namespace_count; ++index) {
		char namespace_prefix[MXSD_STREAM_NAME_CAPACITY];

		if (!span_copy(parser.namespaces[index].prefix, namespace_prefix,
			       sizeof(namespace_prefix))) {
			return reader_fail(reader, MXSD_ERR_STREAM_LIMIT, reader->token_offset,
					   "xmlns");
		}
		result = reader_forward(reader, mxsd_namespace(&reader->validator, namespace_prefix,
							       namespace_uris[index]));
		if (result.code != MXSD_OK) {
			return result;
		}
	}
	result = emit_xsi_type(reader, &parser, &tag);
	if (result.code == MXSD_OK) {
		result = emit_attributes(reader, &parser, &tag);
	}
	if (result.code == MXSD_OK) {
		result = reader_forward(reader, mxsd_start_content(&reader->validator));
	}
	if (result.code == MXSD_OK && tag.is_empty) {
		result = emit_end_element(reader, uri, local);
	}
	return result;
}

/** Parse and emit a complete end tag retained in the reader token. */
static mxsd_result_t process_end_tag(mxsd_xml_reader_t *reader)
{
	xml_parser_t parser;
	text_span_t name;
	char local[MXSD_STREAM_NAME_CAPACITY];
	char uri[MXSD_STREAM_NAME_CAPACITY];

	prepare_parser(reader, &parser);
	parser.position = 2u;
	if (!mxsd_parse_xml_name(&parser, &name)) {
		return reader_fail(reader, MXSD_ERR_MALFORMED_XML, reader->token_offset, NULL);
	}
	mxsd_skip_whitespace(&parser);
	if (parser.position + 1u != parser.length || parser.input[parser.position] != '>' ||
	    !resolve_name(&parser, name, 0, local, sizeof(local), uri, sizeof(uri))) {
		return reader_fail(reader, MXSD_ERR_MALFORMED_XML, reader->token_offset, NULL);
	}
	return emit_end_element(reader, uri, local);
}

/** Normalize and emit decoded character data retained between tags. */
static mxsd_result_t process_text(mxsd_xml_reader_t *reader, int cdata)
{
	text_span_t raw;
	text_span_t decoded;
	size_t source;
	size_t target = 0u;

	if (reader->token_length == 0u) {
		return reader->result;
	}
	raw.data = reader->token;
	raw.length = reader->token_length;
	if (raw.length > MXSD_MAX_TEXT_LENGTH) {
		return reader_fail(reader, MXSD_ERR_STREAM_LIMIT, reader->token_offset, NULL);
	}
	if (cdata) {
		decoded = raw;
	} else if (!mxsd_decode_xml_references(raw, &decoded, reader->decoded)) {
		return reader_fail(reader, MXSD_ERR_MALFORMED_XML, reader->token_offset, NULL);
	}
	for (source = 0u; source < decoded.length; ++source) {
		char value = decoded.data[source];

		if (value == '\r') {
			if (source + 1u < decoded.length && decoded.data[source + 1u] == '\n') {
				source++;
			}
			value = '\n';
		}
		reader->decoded[target++] = value;
	}
	reader->decoded[target] = '\0';
	if (reader->validator.depth == 0u) {
		for (source = 0u; source < target; ++source) {
			if (!isspace((unsigned char)reader->decoded[source])) {
				return reader_fail(reader, MXSD_ERR_MALFORMED_XML,
						   reader->token_offset, NULL);
			}
		}
		return reader->result;
	}
	{
		mxsd_result_t result = reader_forward(
			reader, mxsd_text(&reader->validator, reader->decoded, target));

		if (result.code == MXSD_OK) {
			notify_event(reader, MXSD_XML_EVENT_TEXT, "", NULL, reader->decoded,
				     target);
			return reader->result;
		}
		return result;
	}
}

/** Dispatch one complete markup token. */
static mxsd_result_t process_markup(mxsd_xml_reader_t *reader)
{
	if (reader->token_length >= 4u && !memcmp(reader->token, "<!--", 4u)) {
		return reader->result;
	}
	if (reader->token_length >= 9u && !memcmp(reader->token, "<![CDATA[", 9u)) {
		size_t content_length = reader->token_length - 12u;

		memmove(reader->token, reader->token + 9u, content_length);
		reader->token_length = content_length;
		reader->token[content_length] = '\0';
		return process_text(reader, 1);
	}
	if (reader->token_length >= 2u && !memcmp(reader->token, "<?", 2u)) {
		return reader->result;
	}
	if (reader->token_length >= 2u && !memcmp(reader->token, "</", 2u)) {
		return process_end_tag(reader);
	}
	if (reader->token_length >= 2u && !memcmp(reader->token, "<!", 2u)) {
		return reader_fail(reader, MXSD_ERR_UNSUPPORTED, reader->token_offset, "DOCTYPE");
	}
	return process_start_tag(reader);
}

/** Return whether the retained markup token is complete. */
static int markup_complete(mxsd_xml_reader_t *reader)
{
	const char *token = reader->token;
	size_t length = reader->token_length;
	char value = token[length - 1u];

	if (length >= 4u && !memcmp(token, "<!--", 4u)) {
		return length >= 7u && !memcmp(token + length - 3u, "-->", 3u);
	}
	if (length >= 9u && !memcmp(token, "<![CDATA[", 9u)) {
		return length >= 12u && !memcmp(token + length - 3u, "]]>", 3u);
	}
	if (length >= 2u && !memcmp(token, "<?", 2u)) {
		return length >= 4u && token[length - 2u] == '?' && value == '>';
	}
	if (reader->quote != 0u) {
		if (value == (char)reader->quote) {
			reader->quote = 0u;
		}
		return 0;
	}
	if (value == '\'' || value == '"') {
		reader->quote = (unsigned char)value;
		return 0;
	}
	return value == '>';
}

mxsd_result_t mxsd_xml_reader_init_with_allocator(mxsd_xml_reader_t *reader,
						  const mxsd_schema_t *schema,
						  const mxsd_allocator_t *allocator)
{
	mxsd_result_t result;

	if (reader == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0u, NULL);
	}
	memset(reader, 0, sizeof(*reader));
	result = mxsd_stream_init_with_allocator(&reader->validator, schema, allocator);
	reader->result = result;
	return result;
}

mxsd_result_t mxsd_xml_reader_init(mxsd_xml_reader_t *reader, const mxsd_schema_t *schema)
{
	return mxsd_xml_reader_init_with_allocator(reader, schema, NULL);
}

void mxsd_xml_reader_set_event_callback(mxsd_xml_reader_t *reader, mxsd_xml_event_fn callback,
					void *user_data)
{
	if (reader != NULL) {
		reader->event_callback = callback;
		reader->event_user_data = user_data;
	}
}

mxsd_result_t mxsd_xml_reader_feed(mxsd_xml_reader_t *reader, const void *data, size_t length)
{
	const unsigned char *bytes = data;
	size_t index;

	if (reader == NULL || (data == NULL && length != 0u) || reader->validator.schema == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0u, NULL);
	}
	if (reader->result.code != MXSD_OK) {
		return reader->result;
	}
	if (length > MXSD_MAX_INPUT - reader->offset) {
		return reader_fail(reader, MXSD_ERR_INPUT_TOO_LARGE, reader->offset, NULL);
	}
	for (index = 0u; index < length; ++index) {
		unsigned char value = bytes[index];

		if (!reader->markup && value == '<') {
			mxsd_result_t result = process_text(reader, 0);

			if (result.code != MXSD_OK) {
				return result;
			}
			reader->token_length = 0u;
			reader->token_offset = reader->offset + index;
			reader->markup = 1u;
		}
		if (reader->token_length >= MXSD_XML_TOKEN_CAPACITY) {
			return reader_fail(reader, MXSD_ERR_STREAM_LIMIT, reader->token_offset,
					   NULL);
		}
		reader->token[reader->token_length++] = (char)value;
		reader->token[reader->token_length] = '\0';
		if (reader->markup && markup_complete(reader)) {
			mxsd_result_t result = process_markup(reader);

			if (result.code != MXSD_OK) {
				return result;
			}
			reader->token_length = 0u;
			reader->markup = 0u;
			reader->quote = 0u;
			reader->token_offset = reader->offset + index + 1u;
		}
	}
	reader->offset += length;
	return reader->result;
}

mxsd_result_t mxsd_xml_reader_finish(mxsd_xml_reader_t *reader)
{
	mxsd_result_t result;

	if (reader == NULL || reader->validator.schema == NULL) {
		return make_result(MXSD_ERR_ARGUMENT, 0u, NULL);
	}
	if (reader->result.code != MXSD_OK) {
		return reader->result;
	}
	if (reader->markup) {
		return reader_fail(reader, MXSD_ERR_MALFORMED_XML, reader->token_offset, NULL);
	}
	result = process_text(reader, 0);
	if (result.code != MXSD_OK) {
		return result;
	}
	reader->token_length = 0u;
	result = mxsd_stream_finish(&reader->validator);
	if (result.code != MXSD_OK) {
		return reader_fail(reader, result.code, reader->offset, result.path);
	}
	reader->result = make_result(MXSD_OK, reader->offset, NULL);
	return reader->result;
}

void mxsd_xml_reader_dispose(mxsd_xml_reader_t *reader)
{
	if (reader != NULL && reader->validator.schema != NULL) {
		mxsd_stream_dispose(&reader->validator);
	}
}
