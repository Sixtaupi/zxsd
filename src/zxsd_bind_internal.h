/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Private arena and scalar conversion shared by both binding readers. */
#ifndef ZXSD_BIND_INTERNAL_H
#define ZXSD_BIND_INTERNAL_H

#include "zxsd_bind.h"

/** Allocate zeroed, maximally aligned bound object/array storage. */
void *zxsd_bind_arena_allocate(zxsd_bind_arena_t *arena, size_t size);
/** Copy, normalize, and convert already XML-decoded lexical text. */
zxsd_result_t zxsd_bind_store_decoded(zxsd_bind_arena_t *arena, const zxsd_bind_field_t *field,
				      void *address, const char *text, size_t length,
				      size_t offset);

#endif
