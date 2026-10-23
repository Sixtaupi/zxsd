/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Private host-test configuration for exact content-model pool boundaries.
 *
 * Force-include this header before public declarations in both the standalone
 * test and its private copy of mxsd_model.c. Never apply it to the normal runtime.
 */

#ifndef MXSD_TEST_MODEL_LIMITS_CONFIG_H
#define MXSD_TEST_MODEL_LIMITS_CONFIG_H

#ifdef MXSD_STREAM_MODEL_CAPACITY
#undef MXSD_STREAM_MODEL_CAPACITY
#endif
#define MXSD_STREAM_MODEL_CAPACITY 8

#endif
