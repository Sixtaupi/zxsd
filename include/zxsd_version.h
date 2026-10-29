/*
 * Copyright (c) 2026 zxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Release identifiers and generated-code compatibility epochs.
 */

#ifndef ZXSD_VERSION_H
#define ZXSD_VERSION_H

/** Public header release; not an ABI compatibility test. */
#define ZXSD_VERSION                "0.20.0"
/** Public function and caller-owned structure ABI epoch. */
#define ZXSD_RUNTIME_ABI_VERSION    1
/** Immutable schema descriptor layout and interpretation epoch. */
#define ZXSD_DESCRIPTOR_ABI_VERSION 1
/** Typed binding descriptor and generated representation epoch. */
#define ZXSD_BINDING_ABI_VERSION    1

/** @cond INTERNAL */
#define ZXSD_ABI_JOIN_IMPL(prefix, epoch) prefix##epoch
#define ZXSD_ABI_JOIN(prefix, epoch)      ZXSD_ABI_JOIN_IMPL(prefix, epoch)

#define ZXSD_RUNTIME_ABI_GUARD    ZXSD_ABI_JOIN(zxsd_runtime_abi_, ZXSD_RUNTIME_ABI_VERSION)
#define ZXSD_DESCRIPTOR_ABI_GUARD ZXSD_ABI_JOIN(zxsd_descriptor_abi_, ZXSD_DESCRIPTOR_ABI_VERSION)
#define ZXSD_BINDING_ABI_GUARD    ZXSD_ABI_JOIN(zxsd_binding_abi_, ZXSD_BINDING_ABI_VERSION)
/** @endcond */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the linked runtime release, which may differ from the headers.
 * @return Static release string. This is diagnostic information, not an ABI check.
 */
const char *zxsd_version(void);

/** @brief Link-time runtime ABI guard used by generated code; no side effects. */
void ZXSD_RUNTIME_ABI_GUARD(void);

/** @brief Link-time descriptor ABI guard used by generated code; no side effects. */
void ZXSD_DESCRIPTOR_ABI_GUARD(void);

#ifdef __cplusplus
}
#endif

#endif
