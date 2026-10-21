/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Read, update and write XML settings through generated typed bindings.
 *
 * The application never handles XML text directly: a received document is
 * validated and converted to C structures, updated in C, and serialized again
 * into a validated document. No heap is used.
 */

#include <zephyr/kernel.h>

#include <string.h>

#include "settings.bind.h"
#include "settings.mxsd.h"

/** Settings document as received, for instance from a network or a file. */
static const char received[] = "<?xml version='1.0' encoding='UTF-8'?>\n"
			       "<settings xmlns='urn:mxsd:sample:settings' version='3'>\n"
			       "  <mode>performance</mode>\n"
			       "  <sensor id='1'>\n"
			       "    <label>Boiler temperature</label>\n"
			       "    <period>500</period>\n"
			       "    <threshold>-20</threshold>\n"
			       "  </sensor>\n"
			       "  <sensor id='2' enabled='false'>\n"
			       "    <label>Room &amp; hallway humidity</label>\n"
			       "    <period>2000</period>\n"
			       "  </sensor>\n"
			       "</settings>\n";

/*
 * Storage for strings and arrays produced by reading. Size it from the largest
 * accepted document; MXSD_ERR_RESOURCE_LIMIT reports a too-small arena.
 */
static unsigned char arena_storage[512];

/** Output buffer for the serialized document, including its terminating NUL. */
static char output[1024];

/** Mode names indexed by the generated enumeration. */
static const char *const mode_names[] = {"off", "eco", "performance"};

/** Print the decoded settings. */
static void print_settings(const settings_settings_t *settings)
{
	size_t index;

	printk("read: version %u, mode %s, %u sensor(s)\n", (unsigned int)settings->version,
	       mode_names[settings->mode], (unsigned int)settings->sensor_count);
	for (index = 0u; index < settings->sensor_count; ++index) {
		const settings_Sensor_t *sensor = &settings->sensor[index];

		printk("  sensor %u \"%s\": every %u ms, %s", (unsigned int)sensor->id,
		       sensor->label, (unsigned int)sensor->period,
		       sensor->enabled ? "enabled" : "disabled");
		if (sensor->has_threshold) {
			printk(", threshold %d", (int)sensor->threshold);
		}
		printk("\n");
	}
}

int main(void)
{
	/* Arrays read from the document live in the arena; build a new one to add a sensor. */
	static settings_Sensor_t sensors[3];
	settings_settings_t settings;
	settings_settings_t rejected;
	mxsd_bind_arena_t arena;
	mxsd_bind_arena_t scratch;
	size_t length;
	mxsd_result_t result;

	/* 1. Validate the received document and convert it to C structures. */
	mxsd_bind_arena_init(&arena, arena_storage, sizeof(arena_storage));
	result = settings_settings_read(received, &settings, &arena);
	if (result.code != MXSD_OK) {
		printk("read failed: %s at offset %u\n", result.message,
		       (unsigned int)result.offset);
		return 0;
	}
	print_settings(&settings);
	printk("arena: %u of %u bytes used\n", (unsigned int)arena.used,
	       (unsigned int)arena.capacity);

	/* 2. Update the settings in C: change the mode and add a sensor. */
	if (settings.sensor_count >= ARRAY_SIZE(sensors)) {
		printk("too many sensors to add one\n");
		return 0;
	}
	memcpy(sensors, settings.sensor, settings.sensor_count * sizeof(sensors[0]));
	sensors[settings.sensor_count] = (settings_Sensor_t){
		.id = 3u,
		.label = "Outdoor pressure",
		.period = 10000u,
	};
	settings.sensor = sensors;
	settings.sensor_count++;
	settings.mode = SETTINGS_MODE_ECO;
	settings.version++;

	/* 3. Serialize; the produced document is validated before it is reported as ok. */
	result = settings_settings_write_buffer(&settings, output, sizeof(output), &length);
	printk("write: %s, %u bytes\n%s\n", result.message, (unsigned int)length, output);
	if (result.code != MXSD_OK) {
		return 0;
	}

	/*
	 * Validate the output explicitly against settings.xsd with the generated
	 * validator, as a receiver would do. The *_write_buffer() call above
	 * already performs this validation; *_write() to a sink does not unless a
	 * streaming validator is passed.
	 */
	result = settings_settings_validate(output);
	printk("validate: %s\n", result.message);

	/*
	 * 4. Invalid input is rejected by validation; the target structure is then
	 *    zeroed and the arena left unchanged, so use separate ones here.
	 */
	mxsd_bind_arena_init(&scratch, arena_storage + arena.used,
			     sizeof(arena_storage) - arena.used);
	result = settings_settings_read("<settings xmlns='urn:mxsd:sample:settings' version='4'>"
					"<sensor id='1'><label>x</label><period>1</period></sensor>"
					"</settings>",
					&rejected, &scratch);
	printk("rejected: %s (%s)\n", result.message, result.path != NULL ? result.path : "-");

	/* 5. Structural mistakes in C are caught by the writer: a sensor needs a label. */
	sensors[2].label = NULL;
	result = settings_settings_write_buffer(&settings, output, sizeof(output), NULL);
	printk("refused: %s (%s)\n", result.message, result.path != NULL ? result.path : "-");

	printk("mxsd bindings sample done\n");
	return 0;
}
