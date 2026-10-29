/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Standalone installed-package consumer test. */

#include "message.zxsd.h"
#include "message.bind.h"

#include <string.h>

/** Exercise the installed typed streaming API and generated callback adapter. */
static zxsd_bind_action_t message_received(void *context, const message_message_t *message)
{
	int *received = context;

	if (message->sequence != 7u || strcmp(message->value, "installed package") != 0) {
		return ZXSD_BIND_CALLBACK_ERROR;
	}
	(*received)++;
	return ZXSD_BIND_CONTINUE;
}

int main(void)
{
	const zxsd_bind_runtime_config_t config = ZXSD_BIND_RUNTIME_CONFIG_INIT;
	zxsd_result_t result;
	static message_message_reader_t reader;
	message_message_t message;
	zxsd_bind_arena_t arena;
	unsigned char storage[128];
	const char *xml = "<message sequence='7'>installed package</message>";
	int received = 0;

	if (zxsd_bind_check_runtime(&config).code != ZXSD_OK) {
		return 1;
	}
	result = message_validate("<message sequence='7'>installed package</message>");
	if (result.code != ZXSD_OK) {
		return 1;
	}
	result = message_validate("<message sequence='invalid'>bad</message>");
	if (result.code != ZXSD_ERR_INVALID_VALUE) {
		return 1;
	}
	zxsd_bind_arena_init(&arena, storage, sizeof(storage));
	result = message_message_reader_init(&reader, &message, &arena, message_received, &received,
					     NULL);
	if (result.code == ZXSD_OK) {
		result = message_message_reader_feed(&reader, xml, strlen(xml));
	}
	if (result.code == ZXSD_OK) {
		result = message_message_reader_finish(&reader);
	}
	message_message_reader_dispose(&reader);
	return result.code == ZXSD_OK && received == 1 && arena.used == 0u ? 0 : 1;
}
