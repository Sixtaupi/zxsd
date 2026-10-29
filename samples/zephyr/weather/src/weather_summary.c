/*
 * Copyright (c) 2026 zxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file @brief Typed forecast selection without XML names, attributes, or parser events. */

#include "weather_summary.h"

#include <string.h>

/** Copy one normalized lexical value, never silently truncating application data. */
static int copy_measurement(char *destination, const char *value)
{
	size_t length = value != NULL ? strlen(value) : 0u;

	if (length >= WEATHER_VALUE_CAPACITY) {
		return 0;
	}
	if (length != 0u) {
		memcpy(destination, value, length);
	}
	destination[length] = '\0';
	return 1;
}

zxsd_bind_action_t weather_summary_capture(void *user_data, const met_weather_timeType_t *forecast)
{
	struct weather_summary *summary = user_data;
	size_t index;

	if (summary->complete) {
		return ZXSD_BIND_VALIDATE_ONLY;
	}
	for (index = 0u; index < forecast->location_count; ++index) {
		const met_weather_locationType_t *location = &forecast->location[index];
		struct weather_summary candidate = {0};

		if (location->temperature_count == 0u || location->humidity_count == 0u ||
		    location->windSpeed_count == 0u || location->pressure_count == 0u ||
		    location->cloudiness_count == 0u || location->cloudiness[0].percent == NULL) {
			continue;
		}
		if (!copy_measurement(candidate.time, forecast->from) ||
		    !copy_measurement(candidate.temperature, location->temperature[0].value) ||
		    !copy_measurement(candidate.temperature_unit, location->temperature[0].unit) ||
		    !copy_measurement(candidate.humidity, location->humidity[0].value) ||
		    !copy_measurement(candidate.humidity_unit, location->humidity[0].unit) ||
		    !copy_measurement(candidate.wind_speed, location->windSpeed[0].mps) ||
		    !copy_measurement(candidate.wind_name, location->windSpeed[0].name) ||
		    !copy_measurement(candidate.pressure, location->pressure[0].value) ||
		    !copy_measurement(candidate.pressure_unit, location->pressure[0].unit) ||
		    !copy_measurement(candidate.cloudiness, location->cloudiness[0].percent)) {
			return ZXSD_BIND_CALLBACK_ERROR;
		}
		candidate.complete = true;
		*summary = candidate;
		return ZXSD_BIND_VALIDATE_ONLY;
	}
	return ZXSD_BIND_CONTINUE;
}
