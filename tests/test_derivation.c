/*
 * Copyright (c) 2026 Sixtaupi
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Positive and negative tests for XSD type derivation. */
#include "derivation.zxsd.h"
#include <stdio.h>

/** Check one result and report a mismatch. */
static int expect(zxsd_result_t result, zxsd_error_t wanted)
{
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d, got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Exercise restriction, extension, inheritance, abstract and anonymous types. */
int zxsd_test_derivation(void)
{
	int failures = 0;
	failures +=
		expect(derivation_extended_validate("<extended id='7' tag='ok'><name> abc "
						    "</name><enabled>true</enabled></extended>"),
		       ZXSD_OK);
	failures += expect(
		derivation_extended_validate(
			"<extended tag='ok'><name>abc</name><enabled>true</enabled></extended>"),
		ZXSD_ERR_MISSING_ATTRIBUTE);
	failures +=
		expect(derivation_extended_validate(
			       "<extended id='7' "
			       "tag='ok'><name>toolong</name><enabled>true</enabled></extended>"),
		       ZXSD_ERR_INVALID_VALUE);
	failures += expect(
		derivation_restricted_validate("<restricted id='1'><name>abc</name></restricted>"),
		ZXSD_OK);
	failures +=
		expect(derivation_restricted_validate("<restricted><name>abc</name></restricted>"),
		       ZXSD_ERR_MISSING_ATTRIBUTE);
	failures +=
		expect(derivation_label_validate("<label lang='fr-FR'> hello </label>"), ZXSD_OK);
	failures += expect(derivation_label_validate("<label>hello</label>"),
			   ZXSD_ERR_MISSING_ATTRIBUTE);
	failures +=
		expect(derivation_shortLabel_validate("<shortLabel lang='fr'> abc </shortLabel>"),
		       ZXSD_OK);
	failures +=
		expect(derivation_shortLabel_validate("<shortLabel lang='fr'>abcd</shortLabel>"),
		       ZXSD_ERR_INVALID_VALUE);
	failures += expect(derivation_abstractRecord_validate(
				   "<abstractRecord><value>x</value></abstractRecord>"),
			   ZXSD_ERR_INVALID_VALUE);
	failures += expect(derivation_anonymous_validate("<anonymous>2</anonymous>"), ZXSD_OK);
	failures += expect(derivation_anonymous_validate("<anonymous>0</anonymous>"),
			   ZXSD_ERR_INVALID_VALUE);
	return failures ? 1 : 0;
}

#ifndef ZXSD_TEST_NO_MAIN
int main(void)
{
	return zxsd_test_derivation();
}
#endif
