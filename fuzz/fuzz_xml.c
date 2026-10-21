/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief libFuzzer entry point for portable XML validation.
 */

#include "device.mxsd.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** Feed one arbitrary NUL-terminated byte sequence to the validator. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *xml;

	if (size > MXSD_MAX_INPUT) {
		return 0;
	}
	xml = malloc(size + 1u);
	if (xml == NULL) {
		return 0;
	}
	memcpy(xml, data, size);
	xml[size] = '\0';
	(void)device_validate(xml);
	free(xml);
	return 0;
}
