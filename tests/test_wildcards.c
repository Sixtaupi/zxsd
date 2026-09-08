/*
 * Copyright (c) 2026 Romain Pelletant
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Tests for recursive particles and XML Schema wildcards. */
#include "wildcards.mxsd.h"
#include <stdio.h>

/** Check a validation status. */
static int expect(mxsd_result_t result, mxsd_error_t wanted)
{
	if (result.code == wanted) {
		return 0;
	}
	fprintf(stderr, "wanted %d, got %d (%s)\n", (int)wanted, (int)result.code, result.message);
	return 1;
}

/** Exercise nested occurrence models and wildcard processing modes. */
int main(void)
{
	int failures = 0;
	failures += expect(
		wildcards_nested_validate("<nested "
					  "xmlns='urn:mxsd:wildcards'><a>one</a><b>2</b><c>true</"
					  "c><y>Y</y><x>X</x><a>two</a></nested>"),
		MXSD_OK);
	failures += expect(
		wildcards_nested_validate("<nested xmlns='urn:mxsd:wildcards'><x>X</x></nested>"),
		MXSD_ERR_ELEMENT_ORDER);
	failures += expect(wildcards_strict_validate(
				   "<strict xmlns='urn:mxsd:wildcards'><known>3</known></strict>"),
			   MXSD_OK);
	failures += expect(
		wildcards_strict_validate("<strict xmlns='urn:mxsd:wildcards'><unknown/></strict>"),
		MXSD_ERR_UNEXPECTED_ELEMENT);
	failures += expect(
		wildcards_lax_validate("<w:lax xmlns:w='urn:mxsd:wildcards' xmlns:f='urn:foreign' "
				       "f:flag='yes'><f:item><f:nested/></f:item></w:lax>"),
		MXSD_OK);
	failures += expect(
		wildcards_lax_validate("<lax xmlns='urn:mxsd:wildcards'><local xmlns=''/></lax>"),
		MXSD_ERR_ELEMENT_ORDER);
	failures += expect(
		wildcards_skip_validate("<skip xmlns='urn:mxsd:wildcards'><anything "
					"xmlns='urn:any'><deep>text</deep></anything></skip>"),
		MXSD_OK);
	failures += expect(wildcards_attributeStrict_validate(
				   "<attributeStrict xmlns='urn:mxsd:wildcards' "
				   "xmlns:w='urn:mxsd:wildcards' w:externalFlag='4'/>"),
			   MXSD_OK);
	failures += expect(wildcards_attributeStrict_validate(
				   "<attributeStrict xmlns='urn:mxsd:wildcards' "
				   "xmlns:w='urn:mxsd:wildcards' w:externalFlag='bad'/>"),
			   MXSD_ERR_INVALID_VALUE);
	return failures ? 1 : 0;
}
