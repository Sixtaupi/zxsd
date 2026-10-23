/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Standalone installed-package consumer test. */

#include "message.mxsd.h"
#include "message.bind.h"

#include <string.h>

/** Exercise the installed typed streaming API and generated callback adapter. */
static mxsd_bind_action_t message_received(void *context, const message_message_t *message)
{
	int *received = context;

	if (message->sequence != 7u || strcmp(message->value, "installed package") != 0) {
		return MXSD_BIND_CALLBACK_ERROR;
	}
	(*received)++;
	return MXSD_BIND_CONTINUE;
}

int main(void)
{
	const mxsd_bind_runtime_config_t config = MXSD_BIND_RUNTIME_CONFIG_INIT;
	mxsd_result_t result;
	static message_message_reader_t reader;
	message_message_t message;
	mxsd_bind_arena_t arena;
	unsigned char storage[128];
	const char *xml = "<message sequence='7'>installed package</message>";
	int received = 0;

	if (mxsd_bind_check_runtime(&config).code != MXSD_OK) {
		return 1;
	}
	result = message_validate("<message sequence='7'>installed package</message>");
	if (result.code != MXSD_OK) {
		return 1;
	}
	result = message_validate("<message sequence='invalid'>bad</message>");
	if (result.code != MXSD_ERR_INVALID_VALUE) {
		return 1;
	}
	mxsd_bind_arena_init(&arena, storage, sizeof(storage));
	result = message_message_reader_init(&reader, &message, &arena, message_received, &received,
					     NULL);
	if (result.code == MXSD_OK) {
		result = message_message_reader_feed(&reader, xml, strlen(xml));
	}
	if (result.code == MXSD_OK) {
		result = message_message_reader_finish(&reader);
	}
	message_message_reader_dispose(&reader);
	return result.code == MXSD_OK && received == 1 && arena.used == 0u ? 0 : 1;
}
