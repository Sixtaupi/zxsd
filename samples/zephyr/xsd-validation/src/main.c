/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Validate XML documents against a schema compiled on the build host.
 *
 * device.xsd is converted to C descriptors at build time, so the target only
 * links the generated validator and the mxsd runtime. No heap is used.
 */

#include <zephyr/kernel.h>

#include "device.mxsd.h"

/** Document that conforms to device.xsd. */
static const char valid_document[] = "<device id='7'>"
				     "<name>node</name>"
				     "<enabled>true</enabled>"
				     "<sample>21</sample>"
				     "<sample>-4</sample>"
				     "</device>";

/** Document that lacks the required sample element. */
static const char invalid_document[] = "<device id='7'><name>node</name></device>";

/** Validate one document and print the outcome. */
static void validate(const char *label, const char *xml)
{
	mxsd_result_t result = device_validate(xml);

	if (result.code == MXSD_OK) {
		printk("%s: valid\n", label);
		return;
	}

	printk("%s: invalid, %s", label, result.message);
	if (result.path != NULL) {
		printk(" (%s)", result.path);
	}
	printk(" at offset %u\n", (unsigned int)result.offset);
}

int main(void)
{
	validate("valid document", valid_document);
	validate("invalid document", invalid_document);
	printk("mxsd XSD validation sample done\n");

	return 0;
}
