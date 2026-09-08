/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Mini-XML 4 well-formedness and streaming-event adapter.
 */

#include "mxsd_xml.h"
#include "mxsd_mxml.h"
#include <mxml.h>
#include <string.h>

/** Resolve a QName prefix against declarations on an element and its parents. */
static const char *namespace_for(mxml_node_t *node, const char *prefix, int attribute)
{
	char declaration[MXSD_STREAM_NAME_CAPACITY];
	const char *value;
	if (prefix == NULL && attribute) {
		return NULL;
	}
	if (prefix == NULL) {
		strcpy(declaration, "xmlns");
	} else {
		size_t length = strlen(prefix);
		if (length + 7u > sizeof(declaration)) {
			return NULL;
		}
		memcpy(declaration, "xmlns:", 6u);
		memcpy(declaration + 6u, prefix, length + 1u);
	}
	while (node != NULL) {
		if (mxmlGetType(node) == MXML_TYPE_ELEMENT &&
		    (value = mxmlElementGetAttr(node, declaration)) != NULL) {
			return value[0] == '\0' ? NULL : value;
		}
		node = mxmlGetParent(node);
	}
	return NULL;
}

/** Split a lexical QName into bounded prefix and local-name buffers. */
static int split_name(const char *name, char *prefix, char *local)
{
	const char *colon = strchr(name, ':');
	size_t prefix_length = colon == NULL ? 0u : (size_t)(colon - name);
	const char *local_start = colon == NULL ? name : colon + 1;
	size_t local_length = strlen(local_start);
	if (prefix_length >= MXSD_STREAM_NAME_CAPACITY ||
	    local_length >= MXSD_STREAM_NAME_CAPACITY) {
		return 0;
	}
	if (prefix_length != 0u) {
		memcpy(prefix, name, prefix_length);
	}
	prefix[prefix_length] = '\0';
	memcpy(local, local_start, local_length + 1u);
	return 1;
}

/** Recursively translate one Mini-XML element subtree into mxsd events. */
static mxsd_result_t emit_element(mxsd_stream_t *stream, mxml_node_t *node)
{
	static const char xsi[] = "http://www.w3.org/2001/XMLSchema-instance";
	char prefix[MXSD_STREAM_NAME_CAPACITY], local[MXSD_STREAM_NAME_CAPACITY];
	const char *uri;
	size_t index, count;
	mxml_node_t *child;
	mxsd_result_t result;
	if (!split_name(mxmlGetElement(node), prefix, local)) {
		return (mxsd_result_t){MXSD_ERR_STREAM_LIMIT, 0u, NULL,
				       "streaming capacity exceeded"};
	}
	uri = namespace_for(node, prefix[0] == '\0' ? NULL : prefix, 0);
	result = mxsd_start_element(stream, uri, local);
	if (result.code != MXSD_OK) {
		return result;
	}
	for (index = 0u, count = mxmlElementGetAttrCount(node); index < count; index++) {
		const char *attribute_name,
			*value = mxmlElementGetAttrByIndex(node, index, &attribute_name);
		char attribute_prefix[MXSD_STREAM_NAME_CAPACITY];
		char attribute_local[MXSD_STREAM_NAME_CAPACITY];
		const char *attribute_uri;
		if (!strncmp(attribute_name, "xmlns", 5u) &&
		    (attribute_name[5] == '\0' || attribute_name[5] == ':')) {
			continue;
		}
		if (!split_name(attribute_name, attribute_prefix, attribute_local)) {
			return (mxsd_result_t){MXSD_ERR_STREAM_LIMIT, 0u, NULL,
					       "streaming capacity exceeded"};
		}
		attribute_uri = namespace_for(
			node, attribute_prefix[0] == '\0' ? NULL : attribute_prefix, 1);
		if (attribute_uri != NULL && !strcmp(attribute_uri, xsi) &&
		    !strcmp(attribute_local, "type")) {
			char type_prefix[MXSD_STREAM_NAME_CAPACITY];
			char type_local[MXSD_STREAM_NAME_CAPACITY];
			if (!split_name(value, type_prefix, type_local)) {
				return (mxsd_result_t){MXSD_ERR_STREAM_LIMIT, 0u, NULL,
						       "streaming capacity exceeded"};
			}
			result = mxsd_xsi_type(
				stream,
				namespace_for(node, type_prefix[0] == '\0' ? NULL : type_prefix, 0),
				type_local);
		} else {
			result = mxsd_attribute(stream, attribute_uri, attribute_local, value);
		}
		if (result.code != MXSD_OK) {
			return result;
		}
	}
	result = mxsd_start_content(stream);
	if (result.code != MXSD_OK) {
		return result;
	}
	for (child = mxmlGetFirstChild(node); child != NULL; child = mxmlGetNextSibling(child)) {
		if (mxmlGetType(child) == MXML_TYPE_ELEMENT) {
			result = emit_element(stream, child);
		} else if (mxmlGetType(child) == MXML_TYPE_OPAQUE) {
			const char *text = mxmlGetOpaque(child);
			result = mxsd_text(stream, text, strlen(text));
		} else if (mxmlGetType(child) == MXML_TYPE_CDATA) {
			const char *text = mxmlGetCDATA(child);
			result = mxsd_text(stream, text, strlen(text));
		} else {
			continue;
		}
		if (result.code != MXSD_OK) {
			return result;
		}
	}
	return mxsd_end_element(stream, uri, local);
}

mxsd_result_t mxsd_mxml_validate(const mxsd_schema_t *schema, mxml_node_t *node)
{
	mxsd_stream_t stream;
	mxsd_result_t result = mxsd_stream_init(&stream, schema);
	if (result.code != MXSD_OK || node == NULL) {
		return result.code == MXSD_OK
			       ? (mxsd_result_t){MXSD_ERR_ARGUMENT, 0u, NULL, "invalid argument"}
			       : result;
	}
	if (mxmlGetType(node) != MXML_TYPE_ELEMENT) {
		node = mxmlFindElement(node, node, NULL, NULL, NULL, MXML_DESCEND_ALL);
	}
	if (node == NULL) {
		return (mxsd_result_t){MXSD_ERR_MALFORMED_XML, 0u, NULL, "malformed XML"};
	}
	result = emit_element(&stream, node);
	return result.code == MXSD_OK ? mxsd_stream_finish(&stream) : result;
}

int mxsd_xml_well_formed(const char *xml)
{
	mxml_options_t *options;
	mxml_node_t *document;
	if (xml == NULL || (options = mxmlOptionsNew()) == NULL) {
		return 0;
	}
	mxmlOptionsSetTypeValue(options, MXML_TYPE_OPAQUE);
	document = mxmlLoadString(NULL, options, xml);
	if (document != NULL) {
		mxmlDelete(document);
	}
	mxmlOptionsDelete(options);
	return document != NULL;
}
