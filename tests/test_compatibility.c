/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Runtime compatibility stamps, mismatched epochs, and exact limits. */

#include "mxsd_bind.h"

#include <stdio.h>
#include <string.h>

/** Check errors without depending on assert() or build-mode-specific behavior. */
static int expect(mxsd_result_t result, mxsd_error_t code)
{
	if (result.code == code) {
		return 0;
	}
	fprintf(stderr, "compatibility: wanted %d, got %d (%s)\n", (int)code, (int)result.code,
		result.message);
	return 1;
}

/** @brief Shared host/Twister entry point for the compatibility contract. */
int mxsd_test_compatibility(void)
{
	const mxsd_runtime_config_t core = MXSD_RUNTIME_CONFIG_INIT;
	const mxsd_bind_runtime_config_t binding = MXSD_BIND_RUNTIME_CONFIG_INIT;
	mxsd_runtime_config_t changed;
	mxsd_bind_runtime_config_t changed_binding;
	size_t index;
	int failures = 0;

	MXSD_RUNTIME_ABI_GUARD();
	MXSD_DESCRIPTOR_ABI_GUARD();
	MXSD_BINDING_ABI_GUARD();
	failures += strcmp(mxsd_version(), MXSD_VERSION) != 0;
	failures += expect(mxsd_check_runtime(&core), MXSD_OK);
	failures += expect(mxsd_bind_check_runtime(&binding), MXSD_OK);
	failures += expect(mxsd_check_runtime(NULL), MXSD_ERR_ARGUMENT);
	failures += expect(mxsd_bind_check_runtime(NULL), MXSD_ERR_ARGUMENT);
	changed = core;
	changed.runtime_abi++;
	failures += expect(mxsd_check_runtime(&changed), MXSD_ERR_INCOMPATIBLE_ABI);
	changed = core;
	changed.descriptor_abi++;
	failures += expect(mxsd_check_runtime(&changed), MXSD_ERR_INCOMPATIBLE_ABI);
	changed = core;
	changed.no_heap = !changed.no_heap;
	failures += expect(mxsd_check_runtime(&changed), MXSD_ERR_INCOMPATIBLE_ABI);
	for (index = 0u; index < sizeof(core.limits) / sizeof(core.limits[0]); ++index) {
		changed = core;
		changed.limits[index]++;
		failures += expect(mxsd_check_runtime(&changed), MXSD_ERR_INCOMPATIBLE_ABI);
		changed_binding = binding;
		changed_binding.runtime = changed;
		failures += expect(mxsd_bind_check_runtime(&changed_binding),
				   MXSD_ERR_INCOMPATIBLE_ABI);
	}
	for (index = 0u; index < sizeof(core.layouts) / sizeof(core.layouts[0]); ++index) {
		changed = core;
		changed.layouts[index]++;
		failures += expect(mxsd_check_runtime(&changed), MXSD_ERR_INCOMPATIBLE_ABI);
	}
	changed_binding = binding;
	changed_binding.binding_abi++;
	failures += expect(mxsd_bind_check_runtime(&changed_binding), MXSD_ERR_INCOMPATIBLE_ABI);
	for (index = 0u; index < sizeof(binding.layouts) / sizeof(binding.layouts[0]); ++index) {
		changed_binding = binding;
		changed_binding.layouts[index]++;
		failures += expect(mxsd_bind_check_runtime(&changed_binding),
				   MXSD_ERR_INCOMPATIBLE_ABI);
	}
	failures += strcmp(mxsd_error_string(MXSD_ERR_INCOMPATIBLE_ABI), "unknown error") == 0;
	return failures != 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_compatibility();
}
#endif
