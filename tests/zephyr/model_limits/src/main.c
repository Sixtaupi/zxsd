/*
 * Copyright (c) 2026 zxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Zephyr wrapper for the shared pool-boundary tests. */

#include <zephyr/ztest.h>

extern int zxsd_test_model_limits(void);

ZTEST(zxsd_model_limits, test_model_limits)
{
	zassert_equal(zxsd_test_model_limits(), 0, "shared model pool boundary failure");
}

ZTEST_SUITE(zxsd_model_limits, NULL, NULL, NULL, NULL, NULL);
