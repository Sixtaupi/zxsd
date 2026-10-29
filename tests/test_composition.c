/* Copyright (c) 2026 Sixtaupi
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file @brief Runtime tests for composed XML Schema documents. */
#include "composition.zxsd.h"
#include <stdio.h>

/** Compare one composed-schema validation result. */
static int expect(const char *xml, zxsd_error_t wanted)
{
	zxsd_result_t result = composition_composed_validate(xml);
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Exercise include, import and redefine in one registry. */
int zxsd_test_composition(void)
{
	int failures = 0;
	failures +=
		expect("<c:composed xmlns:c='urn:zxsd:composition' "
		       "xmlns:f='urn:zxsd:foreign' baseCode='base' extraCode='extra'>"
		       "<c:included><c:includedValue>4</"
		       "c:includedValue></c:included><f:foreignItem><f:foreignValue>ok</"
		       "f:foreignValue></f:foreignItem><c:record><c:baseValue>base</"
		       "c:baseValue><c:extraValue>8</c:extraValue></c:record>"
		       "<c:groupBase>x</c:groupBase><c:groupExtra>9</c:groupExtra></c:composed>",
		       ZXSD_OK);
	failures +=
		expect("<c:composed xmlns:c='urn:zxsd:composition' "
		       "xmlns:f='urn:zxsd:foreign' baseCode='base' extraCode='extra'>"
		       "<c:included><c:includedValue>bad</"
		       "c:includedValue></c:included><f:foreignItem><f:foreignValue>ok</"
		       "f:foreignValue></f:foreignItem><c:record><c:baseValue>base</"
		       "c:baseValue><c:extraValue>8</c:extraValue></c:record>"
		       "<c:groupBase>x</c:groupBase><c:groupExtra>9</c:groupExtra></c:composed>",
		       ZXSD_ERR_INVALID_VALUE);
	failures +=
		expect("<c:composed xmlns:c='urn:zxsd:composition' "
		       "xmlns:f='urn:zxsd:foreign' baseCode='base' extraCode='extra'>"
		       "<c:included><c:includedValue>4</c:includedValue></"
		       "c:included><f:foreignItem><f:foreignValue>ok</f:foreignValue></"
		       "f:foreignItem><c:record><c:extraValue>8</c:extraValue></c:record>"
		       "<c:groupBase>x</c:groupBase><c:groupExtra>9</c:groupExtra></c:composed>",
		       ZXSD_ERR_ELEMENT_ORDER);
	failures += expect(
		"<c:composed xmlns:c='urn:zxsd:composition' xmlns:f='urn:zxsd:foreign' "
		"baseCode='base'><c:included><c:includedValue>4</c:includedValue></c:included>"
		"<f:foreignItem><f:foreignValue>ok</f:foreignValue></f:foreignItem>"
		"<c:record><c:baseValue>base</c:baseValue><c:extraValue>8</c:extraValue>"
		"</c:record><c:groupBase>x</c:groupBase><c:groupExtra>9</c:groupExtra>"
		"</c:composed>",
		ZXSD_ERR_MISSING_ATTRIBUTE);
	return failures ? 1 : 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_composition();
}
#endif
