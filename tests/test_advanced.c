/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Host tests for advanced schema constructs and namespaces.
 */

#include "advanced.zxsd.h"
#include <stdio.h>

/** Compare one validation result with its expected status. */
static int check(zxsd_result_t r, zxsd_error_t e)
{
	if (r.code != e) {
		fprintf(stderr, "expected %d got %d: %s\n", (int)e, (int)r.code, r.message);
		return 1;
	}
	return 0;
}

/** Run the advanced host validation test cases. */
int zxsd_test_advanced(void)
{
	int n = 0;
	n += check(advanced_root_validate("<t:root xmlns:t='urn:zxsd:test' "
					  "tag='ABC'><t:shared>XYZ</t:shared><t:level>9</"
					  "t:level><t:inner><t:right>R</t:right><t:left>L</"
					  "t:left></t:inner></t:root>"),
		   ZXSD_OK);
	n += check(advanced_root_validate(
			   "<root xmlns='urn:zxsd:test' "
			   "tag='ABC'><shared>XYZ</shared><level>10</level><inner><left>L</"
			   "left><right>R</right></inner></root>"),
		   ZXSD_ERR_INVALID_VALUE);
	n += check(advanced_root_validate("<root xmlns='urn:wrong' tag='ABC'/>"),
		   ZXSD_ERR_WRONG_ROOT);
	n += check(advanced_root_validate(
			   "<root xmlns='urn:zxsd:test' "
			   "tag='BAD'><shared>XYZ</shared><level>2</level><inner><left>L</"
			   "left><right>R</right></inner></root>"),
		   ZXSD_ERR_INVALID_VALUE);
	n += check(advanced_pick_validate("<pick xmlns='urn:zxsd:test'><two>7</two></pick>"),
		   ZXSD_OK);
	n += check(advanced_pick_validate("<pick xmlns='urn:zxsd:test'/>"),
		   ZXSD_ERR_MISSING_ELEMENT);
	return n ? 1 : 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_advanced();
}
#endif
