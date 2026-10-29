/*
 * Copyright (c) 2026 zxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Private host-test configuration for exact content-model pool boundaries.
 *
 * Force-include this header before public declarations in both the standalone
 * test and its private copy of zxsd_model.c. Never apply it to the normal runtime.
 */

#ifndef ZXSD_TEST_MODEL_LIMITS_CONFIG_H
#define ZXSD_TEST_MODEL_LIMITS_CONFIG_H

#ifdef ZXSD_STREAM_MODEL_CAPACITY
#undef ZXSD_STREAM_MODEL_CAPACITY
#endif
#define ZXSD_STREAM_MODEL_CAPACITY 8

#endif
