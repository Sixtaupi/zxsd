/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Application-owned values copied from a typed forecast callback. */
#ifndef WEATHER_SUMMARY_H
#define WEATHER_SUMMARY_H

#include "met_weather.bind.h"

#define WEATHER_VALUE_CAPACITY 32u

/** First location containing every displayed measurement, independent of arena lifetime. */
struct weather_summary {
	char time[WEATHER_VALUE_CAPACITY];
	char temperature[WEATHER_VALUE_CAPACITY];
	char temperature_unit[WEATHER_VALUE_CAPACITY];
	char humidity[WEATHER_VALUE_CAPACITY];
	char humidity_unit[WEATHER_VALUE_CAPACITY];
	char wind_speed[WEATHER_VALUE_CAPACITY];
	char wind_name[WEATHER_VALUE_CAPACITY];
	char pressure[WEATHER_VALUE_CAPACITY];
	char pressure_unit[WEATHER_VALUE_CAPACITY];
	char cloudiness[WEATHER_VALUE_CAPACITY];
	bool complete;
};

/**
 * @brief Copy the first complete typed forecast, then request validation-only processing.
 * @param[in,out] user_data Application-owned weather_summary.
 * @param[in] forecast Transient generated forecast structure.
 * @return Continue, validate only, or callback error if a displayed value will not fit.
 */
mxsd_bind_action_t weather_summary_capture(void *user_data, const met_weather_timeType_t *forecast);

#endif
