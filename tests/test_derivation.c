/*
 * Copyright (c) 2026 Romain Pelletant
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Positive and negative tests for XSD type derivation. */
#include "derivation.mxsd.h"
#include <stdio.h>

/** Check one result and report a mismatch. */
static int expect(mxsd_result_t result, mxsd_error_t wanted)
{
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d, got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Exercise restriction, extension, inheritance, abstract and anonymous types. */
int main(void)
{
	int failures = 0;
	failures +=
		expect(derivation_extended_validate("<extended id='7' tag='ok'><name> abc "
						    "</name><enabled>true</enabled></extended>"),
		       MXSD_OK);
	failures += expect(
		derivation_extended_validate(
			"<extended tag='ok'><name>abc</name><enabled>true</enabled></extended>"),
		MXSD_ERR_MISSING_ATTRIBUTE);
	failures +=
		expect(derivation_extended_validate(
			       "<extended id='7' "
			       "tag='ok'><name>toolong</name><enabled>true</enabled></extended>"),
		       MXSD_ERR_INVALID_VALUE);
	failures +=
		expect(derivation_restricted_validate("<restricted><name>abc</name></restricted>"),
		       MXSD_OK);
	failures += expect(
		derivation_restricted_validate("<restricted id='1'><name>abc</name></restricted>"),
		MXSD_ERR_UNEXPECTED_ATTRIBUTE);
	failures +=
		expect(derivation_label_validate("<label lang='fr-FR'> hello </label>"), MXSD_OK);
	failures += expect(derivation_label_validate("<label>hello</label>"),
			   MXSD_ERR_MISSING_ATTRIBUTE);
	failures +=
		expect(derivation_shortLabel_validate("<shortLabel lang='fr'> abc </shortLabel>"),
		       MXSD_OK);
	failures +=
		expect(derivation_shortLabel_validate("<shortLabel lang='fr'>abcd</shortLabel>"),
		       MXSD_ERR_INVALID_VALUE);
	failures += expect(derivation_abstractRecord_validate(
				   "<abstractRecord><value>x</value></abstractRecord>"),
			   MXSD_ERR_INVALID_VALUE);
	failures += expect(derivation_anonymous_validate("<anonymous>2</anonymous>"), MXSD_OK);
	failures += expect(derivation_anonymous_validate("<anonymous>0</anonymous>"),
			   MXSD_ERR_INVALID_VALUE);
	return failures ? 1 : 0;
}
