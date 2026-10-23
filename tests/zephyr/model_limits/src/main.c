/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Zephyr wrapper for the shared pool-boundary tests. */

#include <zephyr/ztest.h>

extern int mxsd_test_model_limits(void);

ZTEST(mxsd_model_limits, test_model_limits)
{
	zassert_equal(mxsd_test_model_limits(), 0, "shared model pool boundary failure");
}

ZTEST_SUITE(mxsd_model_limits, NULL, NULL, NULL, NULL, NULL);
