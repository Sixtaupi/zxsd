/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief REST client exchanging validated XML through generated bindings.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include <errno.h>
#include <string.h>

#include "slideshow.bind.h"

#define REST_HOST    "httpbin.org"
#define REST_PORT    80
#define HTTP_TIMEOUT (15 * MSEC_PER_SEC)
#define XML_RESOURCE_URL                                                                           \
	"/base64/PHNsaWRlc2hvdyB0aXRsZT0iWmVwaHlyIFJFU1QgZGVtbyIgZGF0ZT0iMjAyNi0xMC0yMSIg"         \
	"YXV0aG9yPSJodHRwYmluIj48c2xpZGUgdHlwZT0iYWxsIj48dGl0bGU+VmFsaWRhdGVkIFhNTDwvdGl0"         \
	"bGU+PGl0ZW0+UmVhZDwvaXRlbT48aXRlbT5VcGRhdGU8L2l0ZW0+PGl0ZW0+V3JpdGU8L2l0ZW0+PC9z"         \
	"bGlkZT48L3NsaWRlc2hvdz4="

/** Complete XML response body and the HTTP status collected by the callback. */
struct response_body {
	char *data;
	size_t capacity;
	size_t length;
	int status;
	bool overflow;
};

static char http_receive_buffer[2048];
static char xml[2048];
static char response_storage[2048];
static unsigned char arena_storage[1024];

BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE == 0,
	     "the REST sample must keep the Zephyr system heap disabled");

/** Caller-owned result of one asynchronous, allocation-free DNS query. */
struct resolve_state {
	struct k_sem done;
	struct sockaddr_in address;
	int status;
	bool found;
};

/** Collect body fragments because Zephyr may invoke the callback more than once. */
static int response_cb(struct http_response *response, enum http_final_call final_data,
		       void *user_data)
{
	struct response_body *body = user_data;
	size_t available;
	size_t copied;

	body->status = response->http_status_code;
	if (response->body_frag_start == NULL || response->body_frag_len == 0u) {
		return 0;
	}
	available = body->capacity - body->length - 1u;
	copied = MIN(response->body_frag_len, available);
	memcpy(body->data + body->length, response->body_frag_start, copied);
	body->length += copied;
	body->data[body->length] = '\0';
	body->overflow |= copied != response->body_frag_len;
	ARG_UNUSED(final_data);
	return 0;
}

/** Retain the first IPv4 result and signal when the DNS query is complete. */
static void resolve_cb(enum dns_resolve_status status, struct dns_addrinfo *info, void *user_data)
{
	struct resolve_state *state = user_data;

	if (status == DNS_EAI_INPROGRESS && info != NULL && info->ai_family == AF_INET &&
	    info->ai_addrlen == sizeof(state->address) && !state->found) {
		memcpy(&state->address, &info->ai_addr_storage, sizeof(state->address));
		state->found = true;
		return;
	}
	if (status == DNS_EAI_INPROGRESS) {
		return;
	}
	state->status = status == DNS_EAI_ALLDONE && state->found ? 0 : status;
	k_sem_give(&state->done);
}

/** Resolve the REST host through Zephyr's fixed DNS query and buffer pools. */
static int resolve_service(struct sockaddr_in *address)
{
	struct resolve_state state = {0};
	uint16_t dns_id;
	int rc;

	k_sem_init(&state.done, 0, 1);
	state.status = DNS_EAI_FAIL;
	rc = dns_get_addr_info(REST_HOST, DNS_QUERY_TYPE_A, &dns_id, resolve_cb, &state,
			       HTTP_TIMEOUT);
	if (rc != 0) {
		return rc;
	}
	k_sem_take(&state.done, K_FOREVER);
	if (state.status != 0) {
		return state.status;
	}
	state.address.sin_port = htons(REST_PORT);
	*address = state.address;
	return 0;
}

/** Open one TCP connection to the configured REST service. */
static int connect_service(void)
{
	struct sockaddr_in address;
	int client;
	int rc;

	rc = resolve_service(&address);
	if (rc != 0) {
		printk("DNS failed: %d\n", rc);
		return -EHOSTUNREACH;
	}
	client = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client >= 0 &&
	    zsock_connect(client, (const struct sockaddr *)&address, sizeof(address)) < 0) {
		rc = -errno;
		zsock_close(client);
		client = rc;
	} else if (client < 0) {
		client = -errno;
	}
	return client;
}

/** Execute an HTTP request and collect its response body. */
static int request(enum http_method method, const char *url, const char *payload,
		   size_t payload_length, struct response_body *response)
{
	static const char *headers[] = {"Accept: application/xml\r\n", NULL};
	struct http_request request = {
		.method = method,
		.url = url,
		.host = REST_HOST,
		.protocol = "HTTP/1.1",
		.header_fields = headers,
		.content_type_value = "application/xml",
		.payload = payload,
		.payload_len = payload_length,
		.response = response_cb,
		.recv_buf = http_receive_buffer,
		.recv_buf_len = sizeof(http_receive_buffer),
	};
	int client;
	int rc;

	client = connect_service();
	if (client < 0) {
		return client;
	}
	rc = http_client_req(client, &request, HTTP_TIMEOUT, response);
	zsock_close(client);
	return rc;
}

int main(void)
{
	struct response_body response = {.data = xml, .capacity = sizeof(xml)};
	slideshow_slideshow_t slideshow;
	mxsd_bind_arena_t arena;
	mxsd_result_t result;
	size_t xml_length;
	int rc;

	/* GET a REST resource whose response body is XML. */
	rc = request(HTTP_GET, XML_RESOURCE_URL, NULL, 0u, &response);
	if (rc < 0 || response.status != 200 || response.overflow) {
		printk("GET failed: transport %d, HTTP %d, overflow %d\n", rc, response.status,
		       response.overflow);
		return 0;
	}

	printk("GET XML response body: %.*s", (int)response.length, response.data);

	/* read() validates against slideshow.xsd before exposing typed values. */
	mxsd_bind_arena_init(&arena, arena_storage, sizeof(arena_storage));
	result = slideshow_slideshow_read(xml, &slideshow, &arena);
	if (result.code != MXSD_OK) {
		printk("GET XML rejected: %s at %u\n", result.message, (unsigned int)result.offset);
		return 0;
	}
	printk("GET XML valid: \"%s\", %u slide(s)\n", slideshow.title,
	       (unsigned int)slideshow.slide_count);

	/* Modify the typed resource, then serialize and validate it before sending. */
	slideshow.author = "mxsd on Zephyr";
	result = slideshow_slideshow_write_buffer(&slideshow, xml, sizeof(xml), &xml_length);
	if (result.code != MXSD_OK) {
		printk("serialization refused: %s\n", result.message);
		return 0;
	}
	printk("POST XML valid: %u bytes\n", (unsigned int)xml_length);

	/* httpbin accepts the XML entity and returns a JSON diagnostic envelope. */
	response = (struct response_body){.data = response_storage,
					  .capacity = sizeof(response_storage)};
	rc = request(HTTP_POST, "/anything", xml, xml_length, &response);
	if (rc < 0 || response.status != 200) {
		printk("POST failed: transport %d, HTTP %d\n", rc, response.status);
		return 0;
	}
	printk("POST accepted: HTTP %d\n", response.status);
	printk("mxsd REST sample done\n");
	return 0;
}
