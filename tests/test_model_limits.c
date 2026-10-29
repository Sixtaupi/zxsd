/*
 * Copyright (c) 2026 zxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Exact shared-model pool boundaries, reference ownership, and reclamation. */

#include "zxsd_internal.h"

#include <stdio.h>
#include <string.h>

#if ZXSD_STREAM_MODEL_CAPACITY != 8
#error "The exact pool-boundary test requires eight model slots"
#endif

/** Initialize a particle's root continuation and its frontier (two pool slots). */
static zxsd_error_t initialize(zxsd_stream_t *stream, zxsd_stream_frame_t *frame,
			       const zxsd_complex_t *type)
{
	frame->complex_type = type;
	return zxsd_model_init(stream, frame);
}

/** Count in-use slots to detect leaks on successful and exhausted work lists. */
static size_t used(const zxsd_stream_t *stream)
{
	size_t index;
	size_t count = 0u;

	for (index = 0u; index < ZXSD_STREAM_MODEL_CAPACITY; ++index) {
		count += stream->model_nodes[index].references != 0u;
	}
	return count;
}

/** Run pool exhaustion, interning, and partial-allocation cleanup checks. */
int zxsd_test_model_limits(void)
{
	static zxsd_stream_t stream;
	zxsd_particle_t particles[5] = {{0}};
	zxsd_complex_t types[5] = {{0}};
	zxsd_stream_frame_t frames[5] = {{0}};
	size_t index;

	/* Each distinct particle gets one root and one frontier entry. Four roots
	 * fit exactly in the eight-slot pool; the first additional root fails. */
	for (index = 0u; index < 5u; ++index) {
		particles[index].kind = ZXSD_PARTICLE_SEQUENCE;
		particles[index].min_occurs = 1u;
		particles[index].max_occurs = 1u;
		types[index].particle = &particles[index];
	}
	for (index = 0u; index < 4u; ++index) {
		if (initialize(&stream, &frames[index], &types[index]) != ZXSD_OK ||
		    used(&stream) != 2u * (index + 1u)) {
			return 1;
		}
	}
	if (stream.model_free != 0u ||
	    initialize(&stream, &frames[4], &types[4]) != ZXSD_ERR_STREAM_LIMIT ||
	    used(&stream) != 8u) {
		return 1;
	}
	/* Freeing one active frame immediately makes its slots reusable. */
	zxsd_model_release(&stream, &frames[1]);
	if (used(&stream) != 6u || initialize(&stream, &frames[4], &types[4]) != ZXSD_OK ||
	    used(&stream) != 8u) {
		return 1;
	}
	for (index = 0u; index < 5u; ++index) {
		zxsd_model_release(&stream, &frames[index]);
	}
	if (used(&stream) != 0u) {
		return 1;
	}
	/* One free slot lets the root allocation succeed, but not its frontier.
	 * The partially initialized root must be reclaimed on that failure. */
	memset(&stream, 0, sizeof(stream));
	memset(frames, 0, sizeof(frames));
	for (index = 0u; index < 4u; ++index) {
		if (initialize(&stream, &frames[index], &types[0]) != ZXSD_OK) {
			return 1;
		}
	}
	/* Interning shares a root between these four frontier entries: five slots. */
	if (used(&stream) != 5u || initialize(&stream, &frames[4], &types[4]) != ZXSD_OK ||
	    used(&stream) != 7u) {
		return 1;
	}
	{
		zxsd_stream_frame_t extra = {0};

		if (initialize(&stream, &extra, &types[3]) != ZXSD_ERR_STREAM_LIMIT ||
		    used(&stream) != 7u) {
			return 1;
		}
	}
	for (index = 0u; index < 5u; ++index) {
		zxsd_model_release(&stream, &frames[index]);
	}
	if (used(&stream) != 0u) {
		return 1;
	}
	/* A closure that cannot allocate its intermediate cells also releases the
	 * borrowed stack and every pending branch, without changing other frames. */
	memset(&stream, 0, sizeof(stream));
	memset(frames, 0, sizeof(frames));
	particles[1].kind = ZXSD_PARTICLE_ELEMENT;
	particles[0].children = &particles[1];
	particles[0].child_count = 1u;
	for (index = 0u; index < 4u; ++index) {
		if (initialize(&stream, &frames[index], &types[index]) != ZXSD_OK) {
			return 1;
		}
	}
	if (zxsd_model_consume(&stream, &frames[0], &particles[1]) != ZXSD_ERR_STREAM_LIMIT ||
	    used(&stream) != 6u) {
		return 1;
	}
	for (index = 0u; index < 4u; ++index) {
		zxsd_model_release(&stream, &frames[index]);
	}
	return used(&stream) != 0u;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_model_limits();
}
#endif
