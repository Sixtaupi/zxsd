/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Mini-XML 4 adapter for the mxsd streaming validator.
 */

#ifndef MXSD_MXML_H
#define MXSD_MXML_H

#include <mxml.h>
#include <mxsd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validate a Mini-XML element tree through the event API.
 *
 * Namespace declarations are resolved while walking the tree and are not
 * reported as ordinary attributes.
 *
 * @param schema Generated schema descriptor.
 * @param node Document node or root element node.
 * @return A structured validation result.
 */
mxsd_result_t mxsd_mxml_validate(const mxsd_schema_t *schema, mxml_node_t *node);

#ifdef __cplusplus
}
#endif
#endif
