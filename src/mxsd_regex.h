/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MXSD_REGEX_H
#define MXSD_REGEX_H

#include "mxsd_internal.h"

#include <stdint.h>

/** Match one complete XML Schema regular expression against a value. */
int mxsd_pattern_matches(const char *pattern, text_span_t value);

/** Test a code point against an XML Schema Unicode category or block name. */
int mxsd_unicode_property_matches(uint32_t codepoint, const char *property, size_t length);

/** Decode one strict UTF-8 scalar value, returning its byte length or zero. */
size_t mxsd_utf8_decode(const char *text, size_t length, uint32_t *codepoint);

/** Test whether a code point is an XML 1.0 NameStartChar. */
int mxsd_xml_name_start(uint32_t codepoint);

/** Test whether a code point is an XML 1.0 NameChar. */
int mxsd_xml_name_char(uint32_t codepoint);

#endif
