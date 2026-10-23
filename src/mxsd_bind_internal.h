/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Private arena and scalar conversion shared by both binding readers. */
#ifndef MXSD_BIND_INTERNAL_H
#define MXSD_BIND_INTERNAL_H

#include "mxsd_bind.h"

/** Allocate zeroed, maximally aligned bound object/array storage. */
void *mxsd_bind_arena_allocate(mxsd_bind_arena_t *arena, size_t size);
/** Copy, normalize, and convert already XML-decoded lexical text. */
mxsd_result_t mxsd_bind_store_decoded(mxsd_bind_arena_t *arena, const mxsd_bind_field_t *field,
				      void *address, const char *text, size_t length,
				      size_t offset);

#endif
