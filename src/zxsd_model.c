/*
 * Copyright (c) 2026 zxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Bounded continuation interpreter for nested content models. */

#include "zxsd_internal.h"

#include <limits.h>

#if ZXSD_STREAM_MODEL_CAPACITY > 65534u
#error "ZXSD_STREAM_MODEL_CAPACITY exceeds the 16-bit continuation index range"
#endif

#if ZXSD_STREAM_MODEL_CAPACITY > 0

/** A continuation either repeats a particle, resumes a group, or links a frontier. */
enum {
	MODEL_REPEAT,
	MODEL_SEQUENCE,
	MODEL_END,
	MODEL_STATE
};

/** Per-event epsilon work list, with no recursion or input-proportional storage. */
typedef struct {
	zxsd_stream_t *stream;
	const zxsd_particle_t *token;
	uint16_t pending;
	uint16_t output;
	size_t steps;
	zxsd_error_t error;
	int accepted;
} model_work_t;

/** Resolve a nonzero, pool-owned continuation index. */
static zxsd_stream_model_node_t *model_node(zxsd_stream_t *stream, uint16_t id)
{
	return &stream->model_nodes[id - 1u];
}

/** Drop one owning reference, iteratively reclaiming its now-unreferenced tail. */
static void model_drop(zxsd_stream_t *stream, uint16_t id)
{
	while (id != 0u) {
		zxsd_stream_model_node_t *node = model_node(stream, id);
		uint16_t parent = node->parent;

		if (--node->references != 0u) {
			break;
		}
		node->next = stream->model_free;
		stream->model_free = id;
		id = parent;
	}
}

/** Release every owning state in a frontier or pending work list. */
static void model_drop_list(zxsd_stream_t *stream, uint16_t list)
{
	while (list != 0u) {
		uint16_t next = model_node(stream, list)->next;

		model_drop(stream, list);
		list = next;
	}
}

/** Intern an immutable stack cell; frontier links are deliberately never shared. */
static uint16_t model_make(model_work_t *work, const zxsd_particle_t *particle, unsigned long count,
			   size_t index, unsigned char phase, unsigned char started,
			   uint16_t parent)
{
	zxsd_stream_t *stream = work->stream;
	zxsd_stream_model_node_t *node;
	uint16_t id;
	size_t slot;

	if (work->error != ZXSD_OK) {
		return 0u;
	}
	if (phase != MODEL_STATE) {
		for (slot = 0u; slot < stream->model_high_water; ++slot) {
			node = &stream->model_nodes[slot];
			if (node->references != 0u && node->phase == phase &&
			    node->particle == particle && node->count == count &&
			    node->index == index && node->started == started &&
			    node->parent == parent) {
				node->references++;
				return (uint16_t)(slot + 1u);
			}
		}
	}
	id = stream->model_free;
	if (id == 0u) {
		work->error = ZXSD_ERR_STREAM_LIMIT;
		return 0u;
	}
	node = model_node(stream, id);
	stream->model_free = node->next;
	if (id > stream->model_high_water) {
		stream->model_high_water = id;
	}
	node->particle = particle;
	node->count = count;
	node->index = index;
	node->phase = phase;
	node->started = started;
	node->parent = parent;
	node->next = 0u;
	node->references = 1u;
	if (parent != 0u) {
		model_node(stream, parent)->references++;
	}
	return id;
}

/** Queue a borrowed stack, merging identical alternatives within this work list. */
static void model_queue(model_work_t *work, uint16_t *list, uint16_t head)
{
	uint16_t state;

	if (work->error != ZXSD_OK) {
		return;
	}
	for (state = *list; state != 0u; state = model_node(work->stream, state)->next) {
		if (model_node(work->stream, state)->parent == head) {
			return;
		}
	}
	state = model_make(work, NULL, 0u, 0u, MODEL_STATE, 0u, head);
	if (state != 0u) {
		model_node(work->stream, state)->next = *list;
		*list = state;
	}
}

/** Determine nullability without enumerating empty repetition counts. */
static int model_nullable(const zxsd_particle_t *particle)
{
	size_t index;

	if (particle->min_occurs == 0u) {
		return 1;
	}
	if (particle->kind == ZXSD_PARTICLE_ELEMENT || particle->kind == ZXSD_PARTICLE_ANY) {
		return 0;
	}
	for (index = 0u; index < particle->child_count; ++index) {
		int empty = model_nullable(&particle->children[index]);

		if (particle->kind == ZXSD_PARTICLE_CHOICE ? empty : !empty) {
			return empty;
		}
	}
	return particle->kind != ZXSD_PARTICLE_CHOICE;
}

/** Skip alternatives whose first set cannot consume this already-resolved leaf. */
static int model_can_start(const zxsd_particle_t *particle, const zxsd_particle_t *token)
{
	size_t index;

	if (particle->max_occurs == 0u) {
		return 0;
	}
	if (particle->kind == ZXSD_PARTICLE_ELEMENT || particle->kind == ZXSD_PARTICLE_ANY) {
		return particle == token;
	}
	for (index = 0u; index < particle->child_count; ++index) {
		const zxsd_particle_t *child = &particle->children[index];

		if (model_can_start(child, token)) {
			return 1;
		}
		if (particle->kind == ZXSD_PARTICLE_SEQUENCE && !model_nullable(child)) {
			break;
		}
	}
	return 0;
}

/** Mark all active group iterations as nonempty, sharing already-marked tails. */
static uint16_t model_progress(model_work_t *work, uint16_t head)
{
	uint16_t path[ZXSD_MAX_DEPTH];
	size_t depth = 0u;
	uint16_t tail = head;

	while (tail != 0u && !model_node(work->stream, tail)->started) {
		if (depth >= ZXSD_MAX_DEPTH) {
			work->error = ZXSD_ERR_STREAM_LIMIT;
			return 0u;
		}
		path[depth++] = tail;
		tail = model_node(work->stream, tail)->parent;
	}
	if (tail != 0u) {
		model_node(work->stream, tail)->references++;
	}
	while (depth != 0u && work->error == ZXSD_OK) {
		zxsd_stream_model_node_t node = *model_node(work->stream, path[--depth]);
		uint16_t marked = model_make(work, node.particle, node.count, node.index,
					     node.phase, 1u, tail);

		model_drop(work->stream, tail);
		tail = marked;
	}
	return tail;
}

/** Increment an occurrence count, merging equivalent unbounded-count states. */
static unsigned long model_increment(const zxsd_particle_t *particle, unsigned long count)
{
	if (count != ULONG_MAX) {
		count++;
	}
	if (particle->max_occurs == ULONG_MAX && count > particle->min_occurs) {
		count = particle->min_occurs;
	}
	if (particle->max_occurs == ULONG_MAX && model_nullable(particle)) {
		count = 0u;
	}
	return count;
}

/** Expand one stack cell or consume one matching leaf; all queues own their heads. */
static void model_step(model_work_t *work, uint16_t head)
{
	zxsd_stream_model_node_t node;
	const zxsd_particle_t *particle;
	uint16_t next;
	uint16_t child;
	size_t index;

	if (head == 0u) {
		work->accepted = work->token == NULL;
		return;
	}
	node = *model_node(work->stream, head);
	particle = node.particle;
	if (node.phase == MODEL_REPEAT) {
		if (node.count >= particle->min_occurs || model_nullable(particle)) {
			model_queue(work, &work->pending, node.parent);
		}
		if (work->token == NULL || particle->max_occurs == 0u ||
		    (particle->max_occurs != ULONG_MAX && node.count >= particle->max_occurs) ||
		    !model_can_start(particle, work->token)) {
			return;
		}
		if (particle->kind == ZXSD_PARTICLE_ELEMENT ||
		    particle->kind == ZXSD_PARTICLE_ANY) {
			if (particle != work->token) {
				return;
			}
			next = model_progress(work, node.parent);
			child = model_make(work, particle, model_increment(particle, node.count),
					   0u, MODEL_REPEAT, 0u, next);
			model_queue(work, &work->output, child);
			model_drop(work->stream, child);
			model_drop(work->stream, next);
			return;
		}
		next = model_make(work, particle, node.count, 0u,
				  particle->kind == ZXSD_PARTICLE_SEQUENCE ? MODEL_SEQUENCE
									   : MODEL_END,
				  0u, node.parent);
		if (particle->kind == ZXSD_PARTICLE_SEQUENCE) {
			model_queue(work, &work->pending, next);
		} else {
			for (index = 0u; index < particle->child_count; ++index) {
				if (!model_can_start(&particle->children[index], work->token)) {
					continue;
				}
				child = model_make(work, &particle->children[index], 0u, 0u,
						   MODEL_REPEAT, 0u, next);
				model_queue(work, &work->pending, child);
				model_drop(work->stream, child);
			}
		}
		model_drop(work->stream, next);
	} else if (node.phase == MODEL_SEQUENCE && node.index < particle->child_count) {
		next = model_make(work, particle, node.count, node.index + 1u, MODEL_SEQUENCE,
				  node.started, node.parent);
		child = model_make(work, &particle->children[node.index], 0u, 0u, MODEL_REPEAT, 0u,
				   next);
		model_queue(work, &work->pending, child);
		model_drop(work->stream, child);
		model_drop(work->stream, next);
	} else if (node.started) {
		next = model_make(work, particle, model_increment(particle, node.count), 0u,
				  MODEL_REPEAT, 0u, node.parent);
		model_queue(work, &work->pending, next);
		model_drop(work->stream, next);
	}
	/* Empty iterations are represented by the nullable exit of MODEL_REPEAT.
	 * Never enumerate them: minOccurs can be huge and their epsilon loop infinite. */
}

/** Run a bounded epsilon closure, releasing all intermediate state on any outcome. */
static zxsd_error_t model_run(zxsd_stream_t *stream, zxsd_stream_frame_t *frame,
			      const zxsd_particle_t *token)
{
	model_work_t work = {stream, token, frame->model_states, 0u, 0u, ZXSD_OK, 0};

	frame->model_states = 0u;
	while (work.pending != 0u && work.error == ZXSD_OK && !work.accepted) {
		uint16_t state = work.pending;
		zxsd_stream_model_node_t node = *model_node(stream, state);

		work.pending = node.next;
		if (++work.steps >
		    (size_t)ZXSD_STREAM_MODEL_CAPACITY * ZXSD_STREAM_MODEL_CAPACITY) {
			work.error = ZXSD_ERR_STREAM_LIMIT;
		} else {
			model_step(&work, node.parent);
		}
		model_drop(stream, state);
	}
	model_drop_list(stream, work.pending);
	if (work.error != ZXSD_OK || token == NULL) {
		model_drop_list(stream, work.output);
	} else {
		frame->model_states = work.output;
	}
	if (work.error != ZXSD_OK) {
		return work.error;
	}
	return (token == NULL ? work.accepted : work.output != 0u) ? ZXSD_OK
								   : ZXSD_ERR_ELEMENT_ORDER;
}

/** Reject non-XSD nested all groups and bound trusted-descriptor recursion. */
static int model_supported(const zxsd_particle_t *particle, size_t depth)
{
	size_t index;

	if (depth >= ZXSD_MAX_DEPTH || particle == NULL) {
		return 0;
	}
	if (particle->kind == ZXSD_PARTICLE_ELEMENT || particle->kind == ZXSD_PARTICLE_ANY) {
		return 1;
	}
	if (particle->kind != ZXSD_PARTICLE_SEQUENCE && particle->kind != ZXSD_PARTICLE_CHOICE) {
		return 0;
	}
	for (index = 0u; index < particle->child_count; ++index) {
		if (!model_supported(&particle->children[index], depth + 1u)) {
			return 0;
		}
	}
	return 1;
}

int zxsd_model_supported(const zxsd_particle_t *particle)
{
	return model_supported(particle, 0u);
}

zxsd_error_t zxsd_model_init(zxsd_stream_t *stream, zxsd_stream_frame_t *frame)
{
	model_work_t work = {stream, NULL, 0u, 0u, 0u, ZXSD_OK, 0};
	uint16_t root;
	size_t slot;

	if (!stream->model_pool_initialized) {
		for (slot = 0u; slot < ZXSD_STREAM_MODEL_CAPACITY; ++slot) {
			stream->model_nodes[slot].next =
				slot + 1u < ZXSD_STREAM_MODEL_CAPACITY ? (uint16_t)(slot + 2u) : 0u;
		}
		stream->model_free = 1u;
		stream->model_pool_initialized = 1;
	}
	root = model_make(&work, frame->complex_type->particle, 0u, 0u, MODEL_REPEAT, 0u, 0u);
	model_queue(&work, &frame->model_states, root);
	model_drop(stream, root);
	return work.error;
}

zxsd_error_t zxsd_model_consume(zxsd_stream_t *stream, zxsd_stream_frame_t *frame,
				const zxsd_particle_t *leaf)
{
	return model_run(stream, frame, leaf);
}

zxsd_error_t zxsd_model_finish(zxsd_stream_t *stream, zxsd_stream_frame_t *frame)
{
	return model_run(stream, frame, NULL);
}

void zxsd_model_release(zxsd_stream_t *stream, zxsd_stream_frame_t *frame)
{
	model_drop_list(stream, frame->model_states);
	frame->model_states = 0u;
}

#else

int zxsd_model_supported(const zxsd_particle_t *particle)
{
	(void)particle;
	return 0;
}

zxsd_error_t zxsd_model_init(zxsd_stream_t *stream, zxsd_stream_frame_t *frame)
{
	(void)stream;
	(void)frame;
	return ZXSD_ERR_STREAM_LIMIT;
}

zxsd_error_t zxsd_model_consume(zxsd_stream_t *stream, zxsd_stream_frame_t *frame,
				const zxsd_particle_t *leaf)
{
	(void)leaf;
	return zxsd_model_init(stream, frame);
}

zxsd_error_t zxsd_model_finish(zxsd_stream_t *stream, zxsd_stream_frame_t *frame)
{
	return zxsd_model_init(stream, frame);
}

void zxsd_model_release(zxsd_stream_t *stream, zxsd_stream_frame_t *frame)
{
	(void)stream;
	(void)frame;
}

#endif
