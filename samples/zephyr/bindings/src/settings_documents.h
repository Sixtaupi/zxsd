/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief XML settings documents used by the typed bindings sample.
 */

#ifndef ZXSD_BINDINGS_SETTINGS_DOCUMENTS_H
#define ZXSD_BINDINGS_SETTINGS_DOCUMENTS_H

/** Settings document as received, for instance from a network or a file. */
static const char received_document[] = "<?xml version='1.0' encoding='UTF-8'?>\n"
					"<settings xmlns='urn:zxsd:sample:settings' version='3'>\n"
					"  <mode>performance</mode>\n"
					"  <sensor id='1'>\n"
					"    <label>Boiler temperature</label>\n"
					"    <period>500</period>\n"
					"    <threshold>-20</threshold>\n"
					"  </sensor>\n"
					"  <sensor id='2' enabled='false'>\n"
					"    <label>Room &amp; hallway humidity</label>\n"
					"    <period>2000</period>\n"
					"  </sensor>\n"
					"</settings>\n";

/** Settings document with a sensor placed before the required mode element. */
static const char invalid_document[] = "<settings xmlns='urn:zxsd:sample:settings' version='4'>"
				       "<sensor id='1'><label>x</label><period>1</period></sensor>"
				       "</settings>";

#endif
