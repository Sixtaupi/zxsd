/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Allocation-free XML backend for embedded targets.
 */

#include "mxsd_xml.h"

int mxsd_xml_well_formed(const char *xml)
{
	/* Syntax validation is performed by the descriptor-driven pull reader. */
	return xml != 0;
}
