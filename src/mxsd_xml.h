/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Internal XML well-formedness backend interface.
 */

#ifndef MXSD_XML_H
#define MXSD_XML_H

/**
 * @brief Check whether an XML string is well formed.
 *
 * @param xml NUL-terminated XML document.
 *
 * @retval 1 The document is well formed.
 * @retval 0 The document is malformed or @p xml is @c NULL.
 */
int mxsd_xml_well_formed(const char *xml);

#endif
