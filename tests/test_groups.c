/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Runtime tests for model groups, attribute groups, and attribute references.
 */

#include "groups.mxsd.h"

/** Validate one instance and compare its status with the expected result. */
static int expect(const char *xml, mxsd_error_t expected)
{
	mxsd_result_t result = groups_root_validate(xml);

	return result.code == expected ? 0 : 1;
}

/** Run model-group and attribute-group validation cases. */
int mxsd_test_groups(void)
{
	int failures = 0;

	failures += expect("<g:root xmlns:g='urn:mxsd:groups' g:code='A'>"
			   "<g:name>first</g:name><g:value>1</g:value>"
			   "<g:name>second</g:name>"
			   "</g:root>",
			   MXSD_OK);
	failures += expect("<g:root xmlns:g='urn:mxsd:groups'><g:name>x</g:name></g:root>",
			   MXSD_ERR_MISSING_ATTRIBUTE);
	failures += expect("<g:root xmlns:g='urn:mxsd:groups' g:code='B'>"
			   "<g:name>x</g:name></g:root>",
			   MXSD_ERR_INVALID_VALUE);
	failures += expect("<g:root xmlns:g='urn:mxsd:groups' g:code='A'>"
			   "<g:value>1</g:value></g:root>",
			   MXSD_ERR_ELEMENT_ORDER);
	failures += expect("<g:root xmlns:g='urn:mxsd:groups' g:code='A'>"
			   "<g:name>one</g:name><g:name>two</g:name><g:name>three</g:name>"
			   "</g:root>",
			   MXSD_ERR_OCCURRENCE);
	if (groups_treeRoot_validate("<g:treeRoot xmlns:g='urn:mxsd:groups'><g:label>root</g:label>"
				     "<g:branch><g:label>leaf</g:label></g:branch></g:treeRoot>")
		    .code != MXSD_OK) {
		failures++;
	}
	return failures ? 1 : 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_groups();
}
#endif
