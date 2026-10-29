/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Validate Mini-XML trees against a schema compiled on the build host.
 *
 * The application parses XML with Mini-XML, then validates the resulting tree
 * with the zxsd Mini-XML adapter and the descriptors generated from device.xsd.
 * Mini-XML allocates the tree from the libc heap; zxsd validation does not
 * allocate.
 */

#include <zephyr/kernel.h>

#include <zxsd_mxml.h>

#include "device.zxsd.h"

/** Document that conforms to device.xsd. */
static const char valid_document[] = "<device id='7'>"
				     "<name>node</name>"
				     "<enabled>true</enabled>"
				     "<sample>21</sample>"
				     "<sample>-4</sample>"
				     "</device>";

/** Document that lacks the required sample element. */
static const char invalid_document[] = "<device id='7'><name>node</name></device>";

/** Print the outcome of one validation. */
static void print_result(const char *label, zxsd_result_t result)
{
	if (result.code == ZXSD_OK) {
		printk("%s: valid\n", label);
		return;
	}

	printk("%s: invalid, %s", label, result.message);
	if (result.path != NULL) {
		printk(" (%s)", result.path);
	}
	printk(" at offset %u\n", (unsigned int)result.offset);
}

/** Parse a document with Mini-XML, then validate the resulting tree. */
static void validate(const char *label, const char *xml)
{
	mxml_options_t *options = mxmlOptionsNew();
	mxml_node_t *document;

	if (options == NULL) {
		printk("%s: could not allocate Mini-XML options\n", label);
		return;
	}
	/* Keep text nodes intact so that zxsd sees the original character data. */
	mxmlOptionsSetTypeValue(options, MXML_TYPE_OPAQUE);
	document = mxmlLoadString(NULL, options, xml);
	mxmlOptionsDelete(options);
	if (document == NULL) {
		printk("%s: Mini-XML could not parse the document\n", label);
		return;
	}

	print_result(label, zxsd_mxml_validate(device_schema(), document));
	mxmlDelete(document);
}

int main(void)
{
	validate("valid document", valid_document);
	validate("invalid document", invalid_document);
	printk("zxsd Mini-XML validation sample done\n");

	return 0;
}
