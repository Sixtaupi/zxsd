/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Read, update and write XML settings in fragments through typed bindings.
 *
 * No complete XML document is ever held in memory: the received document is
 * fed in small fragments and each sensor is delivered as a C structure, the
 * updated settings are serialized through a sink callback, and the receiving
 * side validates the output fragment by fragment. No heap is used.
 */

#include <zephyr/kernel.h>

#include <string.h>

#include "settings.bind.h"
#include "settings.zxsd.h"
#include "settings_documents.h"

/** Fragment size used to simulate data arriving from a network or a file. */
#define FRAGMENT_SIZE 16u

/** Largest label accepted by settings.xsd, plus the terminating NUL. */
#define LABEL_CAPACITY 33u

/** Maximum number of sensors accepted by settings.xsd. */
#define SENSOR_CAPACITY 8u

/** Sensor copied into application storage, independent of the reader arena. */
struct app_sensor {
	settings_Sensor_t value;
	char label[LABEL_CAPACITY];
};

/** Sensors retained from the received document. */
struct app_settings {
	struct app_sensor sensor[SENSOR_CAPACITY];
	size_t sensor_count;
};

/** Receiving side of the written document, validating each fragment. */
struct peer {
	zxsd_xml_reader_t reader;
	size_t bytes;
	size_t fragments;
};

/*
 * Readers and validators embed fixed-capacity parsing state; keep them out of
 * the stack.
 */
static settings_settings_sensor_reader_t sensor_reader;
static zxsd_stream_t write_validator;
static struct peer peer;
static struct app_settings received;

/*
 * Storage for the strings of one sensor. The arena is restored after each
 * delivery, so it is sized for the largest sensor, not for the document.
 */
static unsigned char arena_storage[64];

/** Mode names indexed by the generated enumeration. */
static const char *const mode_names[] = {"off", "eco", "performance"};

/** Copy one delivered sensor; its pointers expire when this callback returns. */
static zxsd_bind_action_t on_sensor(void *user_data, const settings_Sensor_t *sensor)
{
	struct app_settings *settings = user_data;
	struct app_sensor *copy;

	if (settings->sensor_count >= SENSOR_CAPACITY) {
		return ZXSD_BIND_CALLBACK_ERROR;
	}
	copy = &settings->sensor[settings->sensor_count++];
	copy->value = *sensor;
	strncpy(copy->label, sensor->label, sizeof(copy->label) - 1u);
	copy->label[sizeof(copy->label) - 1u] = '\0';
	copy->value.label = copy->label;
	return ZXSD_BIND_CONTINUE;
}

/**
 * Feed a document to the sensor reader in fragments.
 *
 * @param[in] xml Document; only FRAGMENT_SIZE bytes are passed per call.
 * @param[out] settings Sensors delivered by the reader.
 * @param[out] fragments Number of fragments fed before the result was known.
 * @return The final document result.
 */
static zxsd_result_t read_in_fragments(const char *xml, struct app_settings *settings,
				       size_t *fragments)
{
	zxsd_bind_arena_t arena;
	settings_Sensor_t sensor;
	size_t remaining = strlen(xml);
	zxsd_result_t result;

	settings->sensor_count = 0u;
	*fragments = 0u;
	zxsd_bind_arena_init(&arena, arena_storage, sizeof(arena_storage));
	result = settings_settings_sensor_reader_init(&sensor_reader, &sensor, &arena, on_sensor,
						      settings, NULL);
	while (result.code == ZXSD_OK && remaining > 0u) {
		size_t length = MIN(remaining, FRAGMENT_SIZE);

		/* The first error is sticky: later feeds and finish return it again. */
		result = settings_settings_sensor_reader_feed(&sensor_reader, xml, length);
		xml += length;
		remaining -= length;
		++*fragments;
	}
	if (result.code == ZXSD_OK) {
		/* Deliveries are provisional until the whole document is validated. */
		result = settings_settings_sensor_reader_finish(&sensor_reader);
	}
	settings_settings_sensor_reader_dispose(&sensor_reader);
	return result;
}

/** Forward one written fragment to the peer, which validates it immediately. */
static int send_to_peer(void *context, const char *data, size_t length)
{
	struct peer *receiver = context;
	zxsd_result_t result = zxsd_xml_reader_feed(&receiver->reader, data, length);

	receiver->bytes += length;
	receiver->fragments++;
	/* A nonzero return stops the writer with ZXSD_ERR_OUTPUT. */
	return result.code == ZXSD_OK ? 0 : -1;
}

/** Count written fragments without forwarding them. */
static int count_only(void *context, const char *data, size_t length)
{
	size_t *bytes = context;

	(void)data;
	*bytes += length;
	return 0;
}

/** Print the outcome of a step, with the schema item involved when known. */
static void print_result(const char *step, zxsd_result_t result)
{
	printk("%s: %s (%s)\n", step, result.message, result.path != NULL ? result.path : "-");
}

int main(void)
{
	settings_Sensor_t sensors[SENSOR_CAPACITY];
	settings_settings_t settings = {0};
	size_t fragments;
	size_t bytes;
	size_t index;
	zxsd_result_t result;

	/* 1. Read the received document fragment by fragment, one sensor at a time. */
	result = read_in_fragments(received_document, &received, &fragments);
	if (result.code != ZXSD_OK) {
		print_result("read failed", result);
		return 0;
	}
	printk("read: %u sensor(s) from %u fragments of %u bytes\n",
	       (unsigned int)received.sensor_count, (unsigned int)fragments,
	       (unsigned int)FRAGMENT_SIZE);
	for (index = 0u; index < received.sensor_count; ++index) {
		const settings_Sensor_t *sensor = &received.sensor[index].value;

		printk("  sensor %u \"%s\": every %u ms\n", (unsigned int)sensor->id, sensor->label,
		       (unsigned int)sensor->period);
	}

	/*
	 * 2. Build the settings to send in C. This selection delivers sensors only;
	 *    the root version and mode are validated but not materialized, so the
	 *    application sets them.
	 */
	if (received.sensor_count >= SENSOR_CAPACITY) {
		printk("too many sensors to add one\n");
		return 0;
	}
	for (index = 0u; index < received.sensor_count; ++index) {
		sensors[index] = received.sensor[index].value;
	}
	sensors[received.sensor_count] = (settings_Sensor_t){
		.id = 3u,
		.label = "Outdoor pressure",
		.period = 10000u,
	};
	settings.version = 4u;
	settings.mode = SETTINGS_MODE_ECO;
	settings.sensor = sensors;
	settings.sensor_count = received.sensor_count + 1u;

	/*
	 * 3. Write through a sink. The writer validates each item with a fresh
	 *    streaming validator and stops at the first error; the peer validates
	 *    the raw bytes it receives, fragment by fragment.
	 */
	result = zxsd_xml_reader_init(&peer.reader, settings_settings_schema());
	if (result.code == ZXSD_OK) {
		result = settings_settings_stream_init(&write_validator);
	}
	if (result.code == ZXSD_OK) {
		result = settings_settings_write(&settings, send_to_peer, &peer, &write_validator);
	}
	zxsd_stream_dispose(&write_validator);
	printk("write: %s, mode %s, %u bytes in %u fragments\n", result.message,
	       mode_names[settings.mode], (unsigned int)peer.bytes, (unsigned int)peer.fragments);
	if (result.code == ZXSD_OK) {
		result = zxsd_xml_reader_finish(&peer.reader);
		print_result("peer", result);
	}
	zxsd_xml_reader_dispose(&peer.reader);

	/* 4. Invalid input is rejected while it is fed; no sensor is delivered. */
	result = read_in_fragments(invalid_document, &received, &fragments);
	printk("rejected after %u fragment(s), %u sensor(s) delivered: %s (%s)\n",
	       (unsigned int)fragments, (unsigned int)received.sensor_count, result.message,
	       result.path != NULL ? result.path : "-");

	/*
	 * 5. The writer refuses a structure with a missing required value. Bytes
	 *    already passed to the sink cannot be recalled: a receiver must only
	 *    use a document whose writer and finish both succeeded.
	 */
	sensors[2].label = NULL;
	bytes = 0u;
	result = settings_settings_write(&settings, count_only, &bytes, NULL);
	printk("refused after %u bytes: %s (%s)\n", (unsigned int)bytes, result.message,
	       result.path != NULL ? result.path : "-");

	printk("zxsd streaming bindings sample done\n");
	return 0;
}
