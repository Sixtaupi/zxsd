/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Zephyr wrappers for the shared portable C test entry points. */

#include <zephyr/ztest.h>

extern int mxsd_test_runtime(void);
extern int mxsd_test_advanced(void);
extern int mxsd_test_derivation(void);
extern int mxsd_test_simple_types(void);
extern int mxsd_test_wildcards(void);
extern int mxsd_test_identity(void);
extern int mxsd_test_composition(void);
extern int mxsd_test_element_semantics(void);
extern int mxsd_test_groups(void);
extern int mxsd_test_mixed_any(void);
extern int mxsd_test_recursive(void);
extern int mxsd_test_regex(void);
extern int mxsd_test_notation(void);
extern int mxsd_test_declarations(void);
extern int mxsd_test_bindings(void);

#define MXSD_RUN(entry) zassert_equal(entry(), 0, #entry " failed")

ZTEST(mxsd_validation, test_runtime)
{
	MXSD_RUN(mxsd_test_runtime);
}

ZTEST(mxsd_validation, test_advanced)
{
	MXSD_RUN(mxsd_test_advanced);
}

ZTEST(mxsd_validation, test_derivation)
{
	MXSD_RUN(mxsd_test_derivation);
}

ZTEST(mxsd_validation, test_simple_types)
{
	MXSD_RUN(mxsd_test_simple_types);
}

ZTEST(mxsd_validation, test_wildcards)
{
	MXSD_RUN(mxsd_test_wildcards);
}

ZTEST(mxsd_validation, test_identity)
{
	MXSD_RUN(mxsd_test_identity);
}

ZTEST(mxsd_validation, test_composition)
{
	MXSD_RUN(mxsd_test_composition);
}

ZTEST(mxsd_validation, test_element_semantics)
{
	MXSD_RUN(mxsd_test_element_semantics);
}

ZTEST(mxsd_validation, test_groups)
{
	MXSD_RUN(mxsd_test_groups);
}

ZTEST(mxsd_validation, test_mixed_any)
{
	MXSD_RUN(mxsd_test_mixed_any);
}

ZTEST(mxsd_validation, test_recursive)
{
	MXSD_RUN(mxsd_test_recursive);
}

ZTEST(mxsd_validation, test_regex)
{
	MXSD_RUN(mxsd_test_regex);
}

ZTEST(mxsd_validation, test_notation)
{
	MXSD_RUN(mxsd_test_notation);
}

ZTEST(mxsd_validation, test_declarations)
{
	MXSD_RUN(mxsd_test_declarations);
}

ZTEST(mxsd_validation, test_bindings)
{
	MXSD_RUN(mxsd_test_bindings);
}

ZTEST_SUITE(mxsd_validation, NULL, NULL, NULL, NULL, NULL);
