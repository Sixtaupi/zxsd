/*
 * Copyright (c) 2026 zxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Runtime compatibility stamps, mismatched epochs, and exact limits. */

#include "zxsd_bind.h"

#include <stdio.h>
#include <string.h>

/** Check errors without depending on assert() or build-mode-specific behavior. */
static int expect(zxsd_result_t result, zxsd_error_t code)
{
	if (result.code == code) {
		return 0;
	}
	fprintf(stderr, "compatibility: wanted %d, got %d (%s)\n", (int)code, (int)result.code,
		result.message);
	return 1;
}

/** @brief Shared host/Twister entry point for the compatibility contract. */
int zxsd_test_compatibility(void)
{
	const zxsd_runtime_config_t core = ZXSD_RUNTIME_CONFIG_INIT;
	const zxsd_bind_runtime_config_t binding = ZXSD_BIND_RUNTIME_CONFIG_INIT;
	zxsd_runtime_config_t changed;
	zxsd_bind_runtime_config_t changed_binding;
	size_t index;
	int failures = 0;

	ZXSD_RUNTIME_ABI_GUARD();
	ZXSD_DESCRIPTOR_ABI_GUARD();
	ZXSD_BINDING_ABI_GUARD();
	failures += strcmp(zxsd_version(), ZXSD_VERSION) != 0;
	failures += expect(zxsd_check_runtime(&core), ZXSD_OK);
	failures += expect(zxsd_bind_check_runtime(&binding), ZXSD_OK);
	failures += expect(zxsd_check_runtime(NULL), ZXSD_ERR_ARGUMENT);
	failures += expect(zxsd_bind_check_runtime(NULL), ZXSD_ERR_ARGUMENT);
	changed = core;
	changed.runtime_abi++;
	failures += expect(zxsd_check_runtime(&changed), ZXSD_ERR_INCOMPATIBLE_ABI);
	changed = core;
	changed.descriptor_abi++;
	failures += expect(zxsd_check_runtime(&changed), ZXSD_ERR_INCOMPATIBLE_ABI);
	changed = core;
	changed.no_heap = !changed.no_heap;
	failures += expect(zxsd_check_runtime(&changed), ZXSD_ERR_INCOMPATIBLE_ABI);
	for (index = 0u; index < sizeof(core.limits) / sizeof(core.limits[0]); ++index) {
		changed = core;
		changed.limits[index]++;
		failures += expect(zxsd_check_runtime(&changed), ZXSD_ERR_INCOMPATIBLE_ABI);
		changed_binding = binding;
		changed_binding.runtime = changed;
		failures += expect(zxsd_bind_check_runtime(&changed_binding),
				   ZXSD_ERR_INCOMPATIBLE_ABI);
	}
	for (index = 0u; index < sizeof(core.layouts) / sizeof(core.layouts[0]); ++index) {
		changed = core;
		changed.layouts[index]++;
		failures += expect(zxsd_check_runtime(&changed), ZXSD_ERR_INCOMPATIBLE_ABI);
	}
	changed_binding = binding;
	changed_binding.binding_abi++;
	failures += expect(zxsd_bind_check_runtime(&changed_binding), ZXSD_ERR_INCOMPATIBLE_ABI);
	for (index = 0u; index < sizeof(binding.layouts) / sizeof(binding.layouts[0]); ++index) {
		changed_binding = binding;
		changed_binding.layouts[index]++;
		failures += expect(zxsd_bind_check_runtime(&changed_binding),
				   ZXSD_ERR_INCOMPATIBLE_ABI);
	}
	failures += strcmp(zxsd_error_string(ZXSD_ERR_INCOMPATIBLE_ABI), "unknown error") == 0;
	return failures != 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_compatibility();
}
#endif
