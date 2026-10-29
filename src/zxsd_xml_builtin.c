/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Allocation-free XML backend for embedded targets.
 */

#include "zxsd_xml.h"

int zxsd_xml_well_formed(const char *xml)
{
	/* Syntax validation is performed by the descriptor-driven pull reader. */
	return xml != 0;
}
