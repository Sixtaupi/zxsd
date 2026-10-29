/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Zephyr wrappers for the shared portable C test entry points. */

#include <zephyr/ztest.h>

extern int zxsd_test_compatibility(void);

extern int zxsd_test_runtime(void);
extern int zxsd_test_advanced(void);
extern int zxsd_test_derivation(void);
extern int zxsd_test_simple_types(void);
extern int zxsd_test_wildcards(void);
extern int zxsd_test_identity(void);
extern int zxsd_test_composition(void);
extern int zxsd_test_element_semantics(void);
extern int zxsd_test_groups(void);
extern int zxsd_test_stream_models(void);
extern int zxsd_test_mixed_any(void);
extern int zxsd_test_recursive(void);
extern int zxsd_test_regex(void);
extern int zxsd_test_notation(void);
extern int zxsd_test_declarations(void);
extern int zxsd_test_bindings(void);
extern int zxsd_test_stream_bindings(void);
extern int zxsd_test_weather_binding(void);

#define ZXSD_RUN(entry) zassert_equal(entry(), 0, #entry " failed")

ZTEST(zxsd_validation, test_compatibility)
{
	ZXSD_RUN(zxsd_test_compatibility);
}

ZTEST(zxsd_validation, test_runtime)
{
	ZXSD_RUN(zxsd_test_runtime);
}

ZTEST(zxsd_validation, test_advanced)
{
	ZXSD_RUN(zxsd_test_advanced);
}

ZTEST(zxsd_validation, test_derivation)
{
	ZXSD_RUN(zxsd_test_derivation);
}

ZTEST(zxsd_validation, test_simple_types)
{
	ZXSD_RUN(zxsd_test_simple_types);
}

ZTEST(zxsd_validation, test_wildcards)
{
	ZXSD_RUN(zxsd_test_wildcards);
}

ZTEST(zxsd_validation, test_identity)
{
	ZXSD_RUN(zxsd_test_identity);
}

ZTEST(zxsd_validation, test_composition)
{
	ZXSD_RUN(zxsd_test_composition);
}

ZTEST(zxsd_validation, test_element_semantics)
{
	ZXSD_RUN(zxsd_test_element_semantics);
}

ZTEST(zxsd_validation, test_groups)
{
	ZXSD_RUN(zxsd_test_groups);
}

ZTEST(zxsd_validation, test_stream_models)
{
	ZXSD_RUN(zxsd_test_stream_models);
}

ZTEST(zxsd_validation, test_mixed_any)
{
	ZXSD_RUN(zxsd_test_mixed_any);
}

ZTEST(zxsd_validation, test_recursive)
{
	ZXSD_RUN(zxsd_test_recursive);
}

ZTEST(zxsd_validation, test_regex)
{
	ZXSD_RUN(zxsd_test_regex);
}

ZTEST(zxsd_validation, test_notation)
{
	ZXSD_RUN(zxsd_test_notation);
}

ZTEST(zxsd_validation, test_declarations)
{
	ZXSD_RUN(zxsd_test_declarations);
}

ZTEST(zxsd_validation, test_bindings)
{
	ZXSD_RUN(zxsd_test_bindings);
}

ZTEST(zxsd_validation, test_stream_bindings)
{
	ZXSD_RUN(zxsd_test_stream_bindings);
}

ZTEST(zxsd_validation, test_weather_binding)
{
	ZXSD_RUN(zxsd_test_weather_binding);
}

ZTEST_SUITE(zxsd_validation, NULL, NULL, NULL, NULL, NULL);
