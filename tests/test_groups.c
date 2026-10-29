/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Runtime tests for model groups, attribute groups, and attribute references.
 */

#include "groups.zxsd.h"

/** Validate one instance and compare its status with the expected result. */
static int expect(const char *xml, zxsd_error_t expected)
{
	zxsd_result_t result = groups_root_validate(xml);

	return result.code == expected ? 0 : 1;
}

/** Run model-group and attribute-group validation cases. */
int zxsd_test_groups(void)
{
	int failures = 0;

	failures += expect("<g:root xmlns:g='urn:zxsd:groups' g:code='A'>"
			   "<g:name>first</g:name><g:value>1</g:value>"
			   "<g:name>second</g:name>"
			   "</g:root>",
			   ZXSD_OK);
	failures += expect("<g:root xmlns:g='urn:zxsd:groups'><g:name>x</g:name></g:root>",
			   ZXSD_ERR_MISSING_ATTRIBUTE);
	failures += expect("<g:root xmlns:g='urn:zxsd:groups' g:code='B'>"
			   "<g:name>x</g:name></g:root>",
			   ZXSD_ERR_INVALID_VALUE);
	failures += expect("<g:root xmlns:g='urn:zxsd:groups' g:code='A'>"
			   "<g:value>1</g:value></g:root>",
			   ZXSD_ERR_ELEMENT_ORDER);
	failures += expect("<g:root xmlns:g='urn:zxsd:groups' g:code='A'>"
			   "<g:name>one</g:name><g:name>two</g:name><g:name>three</g:name>"
			   "</g:root>",
			   ZXSD_ERR_OCCURRENCE);
	if (groups_treeRoot_validate("<g:treeRoot xmlns:g='urn:zxsd:groups'><g:label>root</g:label>"
				     "<g:branch><g:label>leaf</g:label></g:branch></g:treeRoot>")
		    .code != ZXSD_OK) {
		failures++;
	}
	return failures ? 1 : 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_groups();
}
#endif
