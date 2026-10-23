/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "benchmark.mxsd.h"

#include <mxsd.h>

#include <stdio.h>

#define INPUT_BLOCK_SIZE 4096u

static int validate_file(const char *path)
{
	unsigned char block[INPUT_BLOCK_SIZE];
	mxsd_xml_reader_t reader;
	mxsd_result_t result;
	FILE *input;
	size_t length;

	input = fopen(path, "rb");
	if (input == NULL) {
		perror(path);
		return 2;
	}

	result = mxsd_xml_reader_init(&reader, benchmark_schema());
	while (result.code == MXSD_OK && (length = fread(block, 1u, sizeof(block), input)) != 0u) {
		result = mxsd_xml_reader_feed(&reader, block, length);
	}
	if (result.code == MXSD_OK && ferror(input)) {
		perror(path);
		result.code = MXSD_ERR_ARGUMENT;
	}
	if (result.code == MXSD_OK) {
		result = mxsd_xml_reader_finish(&reader);
	}
	mxsd_xml_reader_dispose(&reader);
	(void)fclose(input);

	if (result.code != MXSD_OK) {
		fprintf(stderr, "%s: validation failed at byte %zu: %s\n", path, result.offset,
			mxsd_error_string(result.code));
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s INPUT.xml\n", argv[0]);
		return 2;
	}
	return validate_file(argv[1]);
}
