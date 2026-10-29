/*
 * Copyright (c) 2026 zxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Allocation-free runtime compatibility checks and link guards. */

#include "zxsd_internal.h"

const char *zxsd_version(void)
{
	return ZXSD_VERSION;
}

void ZXSD_RUNTIME_ABI_GUARD(void)
{
}

void ZXSD_DESCRIPTOR_ABI_GUARD(void)
{
}

zxsd_result_t zxsd_check_runtime(const zxsd_runtime_config_t *expected)
{
	static const zxsd_runtime_config_t actual = ZXSD_RUNTIME_CONFIG_INIT;
	size_t index;

	if (expected == NULL) {
		return make_result(ZXSD_ERR_ARGUMENT, 0u, NULL);
	}
	if (expected->runtime_abi != actual.runtime_abi ||
	    expected->descriptor_abi != actual.descriptor_abi ||
	    expected->no_heap != actual.no_heap) {
		return make_result(ZXSD_ERR_INCOMPATIBLE_ABI, 0u, NULL);
	}
	for (index = 0u; index < sizeof(actual.limits) / sizeof(actual.limits[0]); ++index) {
		if (expected->limits[index] != actual.limits[index]) {
			return make_result(ZXSD_ERR_INCOMPATIBLE_ABI, 0u, "limits");
		}
	}
	for (index = 0u; index < sizeof(actual.layouts) / sizeof(actual.layouts[0]); ++index) {
		if (expected->layouts[index] != actual.layouts[index]) {
			return make_result(ZXSD_ERR_INCOMPATIBLE_ABI, 0u, "layouts");
		}
	}
	return make_result(ZXSD_OK, 0u, NULL);
}
