/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Mini-XML 4 adapter for the zxsd streaming validator.
 */

#ifndef ZXSD_MXML_H
#define ZXSD_MXML_H

#include <mxml.h>
#include <zxsd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup zxsd_mxml_api zxsd Mini-XML adapter
 * @brief Validate Mini-XML trees with generated zxsd descriptors.
 * @{
 */

/**
 * @brief Validate a Mini-XML element tree through the event API.
 *
 * Namespace declarations are resolved while walking the tree and are not
 * reported as ordinary attributes.
 *
 * @param[in] schema Generated schema descriptor.
 * @param[in] node Document node or root element node.
 *
 * @return A structured validation result.
 */
zxsd_result_t zxsd_mxml_validate(const zxsd_schema_t *schema, mxml_node_t *node);

/** @} */

#ifdef __cplusplus
}
#endif
#endif
