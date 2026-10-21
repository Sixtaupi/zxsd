/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Zephyr ztest coverage for the mxsd runtime.
 */

#include <zephyr/ztest.h>
#include "device.mxsd.h"

/** Verify that a schema-conforming document is accepted. */
ZTEST(mxsd, test_valid_document)
{
	mxsd_result_t r =
		device_validate("<device id='7'><name>node</name><sample>1</sample></device>");
	zassert_equal(r.code, MXSD_OK, "validation failed: %s", r.message);
}

/** Verify that a document missing a required element is rejected. */
ZTEST(mxsd, test_invalid_document)
{
	mxsd_result_t r = device_validate("<device id='7'><name>node</name></device>");
	zassert_equal(r.code, MXSD_ERR_MISSING_ELEMENT, "unexpected result: %s", r.message);
}

/** Register the mxsd validation test suite. */
ZTEST_SUITE(mxsd, NULL, NULL, NULL, NULL, NULL);
