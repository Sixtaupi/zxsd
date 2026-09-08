/* Copyright (c) 2026 Romain Pelletant
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file @brief Runtime tests for composed XML Schema documents. */
#include "composition.mxsd.h"
#include <stdio.h>

/** Compare one composed-schema validation result. */
static int expect(const char *xml, mxsd_error_t wanted)
{
	mxsd_result_t result = composition_composed_validate(xml);
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Exercise include, import and redefine in one registry. */
int main(void)
{
	int failures = 0;
	failures += expect("<c:composed xmlns:c='urn:mxsd:composition' "
			   "xmlns:f='urn:mxsd:foreign'><c:included><c:includedValue>4</"
			   "c:includedValue></c:included><f:foreignItem><f:foreignValue>ok</"
			   "f:foreignValue></f:foreignItem><c:record><c:baseValue>base</"
			   "c:baseValue><c:extraValue>8</c:extraValue></c:record></c:composed>",
			   MXSD_OK);
	failures += expect("<c:composed xmlns:c='urn:mxsd:composition' "
			   "xmlns:f='urn:mxsd:foreign'><c:included><c:includedValue>bad</"
			   "c:includedValue></c:included><f:foreignItem><f:foreignValue>ok</"
			   "f:foreignValue></f:foreignItem><c:record><c:baseValue>base</"
			   "c:baseValue><c:extraValue>8</c:extraValue></c:record></c:composed>",
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(
		"<c:composed xmlns:c='urn:mxsd:composition' "
		"xmlns:f='urn:mxsd:foreign'><c:included><c:includedValue>4</c:includedValue></"
		"c:included><f:foreignItem><f:foreignValue>ok</f:foreignValue></"
		"f:foreignItem><c:record><c:extraValue>8</c:extraValue></c:record></c:composed>",
		MXSD_ERR_ELEMENT_ORDER);
	return failures ? 1 : 0;
}
