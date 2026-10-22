/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Host tests for advanced schema constructs and namespaces.
 */

#include "advanced.mxsd.h"
#include <stdio.h>

/** Compare one validation result with its expected status. */
static int check(mxsd_result_t r, mxsd_error_t e)
{
	if (r.code != e) {
		fprintf(stderr, "expected %d got %d: %s\n", (int)e, (int)r.code, r.message);
		return 1;
	}
	return 0;
}

/** Run the advanced host validation test cases. */
int mxsd_test_advanced(void)
{
	int n = 0;
	n += check(advanced_root_validate("<t:root xmlns:t='urn:mxsd:test' "
					  "tag='ABC'><t:shared>XYZ</t:shared><t:level>9</"
					  "t:level><t:inner><t:right>R</t:right><t:left>L</"
					  "t:left></t:inner></t:root>"),
		   MXSD_OK);
	n += check(advanced_root_validate(
			   "<root xmlns='urn:mxsd:test' "
			   "tag='ABC'><shared>XYZ</shared><level>10</level><inner><left>L</"
			   "left><right>R</right></inner></root>"),
		   MXSD_ERR_INVALID_VALUE);
	n += check(advanced_root_validate("<root xmlns='urn:wrong' tag='ABC'/>"),
		   MXSD_ERR_WRONG_ROOT);
	n += check(advanced_root_validate(
			   "<root xmlns='urn:mxsd:test' "
			   "tag='BAD'><shared>XYZ</shared><level>2</level><inner><left>L</"
			   "left><right>R</right></inner></root>"),
		   MXSD_ERR_INVALID_VALUE);
	n += check(advanced_pick_validate("<pick xmlns='urn:mxsd:test'><two>7</two></pick>"),
		   MXSD_OK);
	n += check(advanced_pick_validate("<pick xmlns='urn:mxsd:test'/>"),
		   MXSD_ERR_MISSING_ELEMENT);
	return n ? 1 : 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_advanced();
}
#endif
