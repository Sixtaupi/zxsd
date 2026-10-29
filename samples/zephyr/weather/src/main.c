/*
 * Copyright (c) 2026 Sixtaupi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Interactive HTTPS weather lookup with validated XML responses.
 */

#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ca_certificate.h"
#include "nominatim.bind.h"
#include "weather_summary.h"

#define NOMINATIM_HOST   "nominatim.openstreetmap.org"
#define MET_HOST         "api.met.no"
#define HTTPS_PORT       443
#define HTTP_TIMEOUT     (30 * MSEC_PER_SEC)
#define NOMINATIM_CA_TAG 42
#define MET_CA_TAG       43

#define CITY_LENGTH          64
#define URL_LENGTH           384
#define PLACE_XML_LENGTH     4096
#define PLACE_ARENA_LENGTH   2048
#define WEATHER_ARENA_LENGTH 4096
#define PROGRESS_STEP        (16u * 1024u)

/* Both services require an identifying User-Agent. Adapt the project URL when forking. */
#define USER_AGENT "User-Agent: zxsd-zephyr-weather/0.1 (https://github.com/Sixtaupi/zxsd)\r\n"

/** Streaming XML response state used by the HTTP callback. */
struct response_stream {
	met_weather_weatherdata_product_time_reader_t *binding;
	char *data;
	size_t capacity;
	size_t length;
	size_t next_progress;
	int status;
	bool overflow;
	bool show_progress;
};

/** Caller-owned state for one asynchronous DNS query. */
struct resolve_state {
	struct k_sem done;
	struct sockaddr_in address;
	int status;
	bool found;
};

static char http_receive_buffer[4096];
static char place_xml[PLACE_XML_LENGTH];
static unsigned char place_arena_storage[PLACE_ARENA_LENGTH];
static unsigned char weather_arena_storage[WEATHER_ARENA_LENGTH];
static K_SEM_DEFINE(network_connected, 0, 1);
static struct net_mgmt_event_callback network_callback;

BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE == 0,
	     "the weather sample keeps the Zephyr system heap disabled");

/** Signal when Zephyr reports usable layer-4 connectivity. */
static void network_event(struct net_mgmt_event_callback *callback, uint64_t event,
			  struct net_if *iface)
{
	ARG_UNUSED(callback);
	ARG_UNUSED(iface);
	if (event == NET_EVENT_L4_CONNECTED) {
		k_sem_give(&network_connected);
	}
}

/** Wait until an interface has an address before accepting a city. */
static void wait_for_network(void)
{
	net_mgmt_init_event_callback(&network_callback, network_event, NET_EVENT_L4_CONNECTED);
	net_mgmt_add_event_callback(&network_callback);
	conn_mgr_mon_resend_status();
	printk("Waiting for network...\n");
	k_sem_take(&network_connected, K_FOREVER);
	printk("Network connected.\n");
}

/** Feed response fragments directly to the bounded incremental XML reader. */
static int response_cb(struct http_response *response, enum http_final_call final_data,
		       void *user_data)
{
	struct response_stream *stream = user_data;

	stream->status = response->http_status_code;
	if (response->body_frag_start == NULL || response->body_frag_len == 0u) {
		return 0;
	}
	if (stream->data != NULL) {
		size_t available = stream->capacity - stream->length - 1u;
		size_t copied = MIN(response->body_frag_len, available);

		memcpy(stream->data + stream->length, response->body_frag_start, copied);
		stream->length += copied;
		stream->data[stream->length] = '\0';
		stream->overflow |= copied != response->body_frag_len;
	} else {
		(void)met_weather_weatherdata_product_time_reader_feed(
			stream->binding, response->body_frag_start, response->body_frag_len);
	}
	if (stream->show_progress &&
	    (response->processed >= stream->next_progress || final_data == HTTP_DATA_FINAL)) {
		if (response->content_length != 0u) {
			printk("\rTelechargement: %u/%u Kio (%u %%)",
			       (unsigned int)(response->processed / 1024u),
			       (unsigned int)(response->content_length / 1024u),
			       (unsigned int)(response->processed * 100u /
					      response->content_length));
		} else {
			printk("\rTelechargement: %u Kio",
			       (unsigned int)(response->processed / 1024u));
		}
		while (stream->next_progress <= response->processed) {
			stream->next_progress += PROGRESS_STEP;
		}
		if (final_data == HTTP_DATA_FINAL) {
			printk("\n");
		}
	}
	return 0;
}

/** Retain the first IPv4 address and signal when resolution is complete. */
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

/** Resolve one HTTPS service without allocating from the system heap. */
static int resolve_service(const char *host, struct sockaddr_in *address)
{
	struct resolve_state state = {0};
	uint16_t dns_id;
	int rc;

	k_sem_init(&state.done, 0, 1);
	state.status = DNS_EAI_FAIL;
	rc = dns_get_addr_info(host, DNS_QUERY_TYPE_A, &dns_id, resolve_cb, &state, HTTP_TIMEOUT);
	if (rc != 0) {
		return rc;
	}
	k_sem_take(&state.done, K_FOREVER);
	if (state.status != 0) {
		return state.status;
	}
	state.address.sin_port = htons(HTTPS_PORT);
	*address = state.address;
	return 0;
}

/** Open a certificate-verified TLS connection with SNI enabled. */
static int connect_service(const char *host)
{
	sec_tag_t tags[] = {strcmp(host, NOMINATIM_HOST) == 0 ? NOMINATIM_CA_TAG : MET_CA_TAG};
	struct sockaddr_in address;
	int verify = TLS_PEER_VERIFY_REQUIRED;
	int client;
	int rc;

	rc = resolve_service(host, &address);
	if (rc != 0) {
		return -EHOSTUNREACH;
	}
	client = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
	if (client < 0) {
		return -errno;
	}
	if (zsock_setsockopt(client, SOL_TLS, TLS_SEC_TAG_LIST, tags, sizeof(tags)) < 0 ||
	    zsock_setsockopt(client, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify)) < 0 ||
	    zsock_setsockopt(client, SOL_TLS, TLS_HOSTNAME, host, strlen(host) + 1u) < 0) {
		rc = -errno;
		zsock_close(client);
		return rc;
	}
	if (zsock_connect(client, (const struct sockaddr *)&address, sizeof(address)) < 0) {
		rc = -errno;
		zsock_close(client);
		return rc;
	}
	return client;
}

/** Execute one HTTPS GET and collect the complete XML response. */
static int https_get(const char *host, const char *url, struct response_stream *response)
{
	static const char *headers[] = {USER_AGENT, "Accept: application/xml\r\n",
					"Accept-Encoding: identity\r\n", "Connection: close\r\n",
					NULL};
	struct http_request request = {
		.method = HTTP_GET,
		.url = url,
		.host = host,
		.protocol = "HTTP/1.1",
		.header_fields = headers,
		.response = response_cb,
		.recv_buf = http_receive_buffer,
		.recv_buf_len = sizeof(http_receive_buffer),
	};
	int client;
	int rc;

	client = connect_service(host);
	if (client < 0) {
		return client;
	}
	rc = http_client_req(client, &request, HTTP_TIMEOUT, response);
	zsock_close(client);
	return rc;
}

/** Percent-encode arbitrary UTF-8 city bytes for an HTTP query component. */
static int encode_query(const char *input, char *output, size_t capacity)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t used = 0u;

	while (*input != '\0') {
		unsigned char value = (unsigned char)*input++;

		if (isalnum(value) || value == '-' || value == '_' || value == '.' ||
		    value == '~') {
			if (used + 1u >= capacity) {
				return -ENOSPC;
			}
			output[used++] = (char)value;
		} else {
			if (used + 3u >= capacity) {
				return -ENOSPC;
			}
			output[used++] = '%';
			output[used++] = hex[value >> 4];
			output[used++] = hex[value & 0x0fu];
		}
	}
	output[used] = '\0';
	return 0;
}

/** Copy a coordinate while limiting its fractional part to four digits. */
static int copy_coordinate(const char *input, char *output, size_t capacity)
{
	size_t length = strlen(input);
	const char *decimal = strchr(input, '.');

	if (decimal != NULL && length > (size_t)(decimal - input) + 5u) {
		length = (size_t)(decimal - input) + 5u;
	}
	if (length + 1u > capacity) {
		return -ENOSPC;
	}
	memcpy(output, input, length);
	output[length] = '\0';
	return 0;
}

/** Fetch, validate, and display weather for one city. */
static void lookup_city(const char *city)
{
	static struct response_stream response;
	static struct weather_summary weather;
	static met_weather_weatherdata_product_time_reader_t forecast_reader;
	static met_weather_timeType_t forecast;
	nominatim_searchresults_t search;
	zxsd_bind_arena_t arena;
	zxsd_bind_arena_t weather_arena;
	char encoded[CITY_LENGTH * 3u];
	char latitude[24];
	char longitude[24];
	char url[URL_LENGTH];
	zxsd_result_t result;
	int rc;

	response = (struct response_stream){
		.data = place_xml,
		.capacity = sizeof(place_xml),
	};

	if (encode_query(city, encoded, sizeof(encoded)) != 0) {
		printk("City name too long.\n");
		return;
	}
	snprintf(url, sizeof(url), "/search?q=%s&format=xml&limit=1", encoded);

	printk("Searching for %s...\n", city);
	rc = https_get(NOMINATIM_HOST, url, &response);
	if (rc < 0) {
		printk("Nominatim: transport error %d (%s)\n", rc, strerror(-rc));
		return;
	}
	if (response.status != 200 || response.overflow) {
		printk("Location to lat/lon request: HTTP %d, overflow %d\n", response.status,
		       response.overflow);
		return;
	}
	zxsd_bind_arena_init(&arena, place_arena_storage, sizeof(place_arena_storage));
	result = nominatim_searchresults_read(place_xml, &search, &arena);
	if (result.code != ZXSD_OK) {
		printk("XML coordonates response invalid: %s (byte %u)\n", result.message,
		       (unsigned int)result.offset);
		return;
	}
	if (search.place == NULL) {
		printk("No results for '%s'.\n", city);
		return;
	}
	if (copy_coordinate(search.place->lat, latitude, sizeof(latitude)) != 0 ||
	    copy_coordinate(search.place->lon, longitude, sizeof(longitude)) != 0) {
		printk("Coordonnees Nominatim trop longues.\n");
		return;
	}
	printk("Location: %s\nCoordinates: %s, %s\n", search.place->display_name, latitude,
	       longitude);

	snprintf(url, sizeof(url),
		 "/weatherapi/locationforecast/2.0/classic?lat=%s&lon=%s&altitude=90", latitude,
		 longitude);
	memset(&weather, 0, sizeof(weather));
	response = (struct response_stream){
		.binding = &forecast_reader,
		.next_progress = PROGRESS_STEP,
		.show_progress = true,
	};
	zxsd_bind_arena_init(&weather_arena, weather_arena_storage, sizeof(weather_arena_storage));
	result = met_weather_weatherdata_product_time_reader_init(
		&forecast_reader, &forecast, &weather_arena, weather_summary_capture, &weather,
		NULL);
	if (result.code != ZXSD_OK) {
		printk("Initialisation XML impossible: %s\n", result.message);
		return;
	}
	response.status = 0;
	printk("Downloading weather data...\n");
	rc = https_get(MET_HOST, url, &response);
	if (rc < 0) {
		met_weather_weatherdata_product_time_reader_dispose(&forecast_reader);
		printk("MET Norway: transport error %d (%s)\n", rc, strerror(-rc));
		return;
	}
	result = met_weather_weatherdata_product_time_reader_finish(&forecast_reader);
	met_weather_weatherdata_product_time_reader_dispose(&forecast_reader);
	if (response.status != 200 || result.code != ZXSD_OK) {
		printk("MET Norway: transport %d, HTTP %d, XML %s\n", rc, response.status,
		       result.message);
		return;
	}
	if (!weather.complete) {
		printk("The valid response does not contain the expected summary.\n");
		return;
	}

	printk("\nWeather in %s\n", search.place->display_name);
	printk("Forecast: %s\n", weather.time);
	printk("Temperature: %s %s\n", weather.temperature, weather.temperature_unit);
	printk("Humidity: %s %s\n", weather.humidity, weather.humidity_unit);
	printk("Wind: %s m/s (%s)\n", weather.wind_speed,
	       weather.wind_name[0] != '\0' ? weather.wind_name : "-");
	printk("Pressure: %s %s\n", weather.pressure, weather.pressure_unit);
	printk("Cloudiness: %s %%\n\n", weather.cloudiness);
}

int main(void)
{
	char city[CITY_LENGTH];
	int rc;

	rc = tls_credential_add(NOMINATIM_CA_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
				nominatim_ca_certificate, sizeof(nominatim_ca_certificate));
	if (rc < 0 && rc != -EEXIST) {
		printk("Unable to install Nominatim certificate: %d\n", rc);
		return 0;
	}
	rc = tls_credential_add(MET_CA_TAG, TLS_CREDENTIAL_CA_CERTIFICATE, met_ca_certificate,
				sizeof(met_ca_certificate));
	if (rc < 0 && rc != -EEXIST) {
		printk("Unable to install MET Norway certificate: %d\n", rc);
		return 0;
	}
	console_getline_init();
	printk("zxsd weather - (c) OpenStreetMap contributors, meteo MET Norway\n");
	wait_for_network();

	for (;;) {
		char *line;
		size_t length;

		printk("City (Press Enter to validate): ");
		line = console_getline();
		if (line == NULL) {
			continue;
		}
		length = strlen(line);
		while (length > 0u && (line[length - 1u] == '\r' || line[length - 1u] == '\n')) {
			line[--length] = '\0';
		}
		if (length == 0u) {
			continue;
		}
		if (length >= sizeof(city)) {
			printk("Name limited to %u bytes.\n", (unsigned int)(sizeof(city) - 1u));
			continue;
		}
		memcpy(city, line, length + 1u);
		lookup_city(city);
		/* Nominatim's public service permits at most one request per second. */
		// k_sleep(K_SECONDS(1));
	}
	return 0;
}
