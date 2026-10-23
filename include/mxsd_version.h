/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Release identifiers and generated-code compatibility epochs.
 */

#ifndef MXSD_VERSION_H
#define MXSD_VERSION_H

/** Public header release; not an ABI compatibility test. */
#define MXSD_VERSION                "0.20.0"
/** Public function and caller-owned structure ABI epoch. */
#define MXSD_RUNTIME_ABI_VERSION    1
/** Immutable schema descriptor layout and interpretation epoch. */
#define MXSD_DESCRIPTOR_ABI_VERSION 1
/** Typed binding descriptor and generated representation epoch. */
#define MXSD_BINDING_ABI_VERSION    1

/** @cond INTERNAL */
#define MXSD_ABI_JOIN_IMPL(prefix, epoch) prefix##epoch
#define MXSD_ABI_JOIN(prefix, epoch)      MXSD_ABI_JOIN_IMPL(prefix, epoch)
#define MXSD_RUNTIME_ABI_GUARD            MXSD_ABI_JOIN(mxsd_runtime_abi_, MXSD_RUNTIME_ABI_VERSION)
#define MXSD_DESCRIPTOR_ABI_GUARD MXSD_ABI_JOIN(mxsd_descriptor_abi_, MXSD_DESCRIPTOR_ABI_VERSION)
#define MXSD_BINDING_ABI_GUARD    MXSD_ABI_JOIN(mxsd_binding_abi_, MXSD_BINDING_ABI_VERSION)
/** @endcond */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the linked runtime release, which may differ from the headers.
 * @return Static release string. This is diagnostic information, not an ABI check.
 */
const char *mxsd_version(void);

/** @brief Link-time runtime ABI guard used by generated code; no side effects. */
void MXSD_RUNTIME_ABI_GUARD(void);

/** @brief Link-time descriptor ABI guard used by generated code; no side effects. */
void MXSD_DESCRIPTOR_ABI_GUARD(void);

#ifdef __cplusplus
}
#endif

#endif
