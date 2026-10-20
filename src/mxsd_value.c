/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief XML Schema simple-type and facet validation.
 */

#include "mxsd_internal.h"
#include "mxsd_regex.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int append_utf8(uint32_t codepoint, char *output, size_t *length)
{
	if (codepoint == 0u || codepoint > 0x10ffffu ||
	    (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
		return 0;
	}
	if (codepoint <= 0x7fu) {
		output[(*length)++] = (char)codepoint;
	} else if (codepoint <= 0x7ffu) {
		output[(*length)++] = (char)(0xc0u | (codepoint >> 6));
		output[(*length)++] = (char)(0x80u | (codepoint & 0x3fu));
	} else if (codepoint <= 0xffffu) {
		output[(*length)++] = (char)(0xe0u | (codepoint >> 12));
		output[(*length)++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
		output[(*length)++] = (char)(0x80u | (codepoint & 0x3fu));
	} else {
		output[(*length)++] = (char)(0xf0u | (codepoint >> 18));
		output[(*length)++] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
		output[(*length)++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
		output[(*length)++] = (char)(0x80u | (codepoint & 0x3fu));
	}
	return 1;
}

/** Decode the five predefined XML entities and numeric character references. */
int mxsd_decode_xml_references(text_span_t input, text_span_t *output, char *storage)
{
	size_t source = 0;
	size_t destination = 0;

	if (input.length > MXSD_MAX_TEXT_LENGTH) {
		return 0;
	}
	while (source < input.length) {
		uint32_t codepoint = 0;
		size_t end;
		size_t reference_length;
		int base = 10;

		if (input.data[source] != '&') {
			storage[destination++] = input.data[source++];
			continue;
		}
		end = source + 1u;
		while (end < input.length && input.data[end] != ';') {
			end++;
		}
		if (end == input.length) {
			return 0;
		}
		reference_length = end - source + 1u;
		if (reference_length == 4u && memcmp(input.data + source, "&lt;", 4u) == 0) {
			codepoint = '<';
		} else if (reference_length == 4u && memcmp(input.data + source, "&gt;", 4u) == 0) {
			codepoint = '>';
		} else if (reference_length == 5u &&
			   memcmp(input.data + source, "&amp;", 5u) == 0) {
			codepoint = '&';
		} else if (reference_length == 6u &&
			   memcmp(input.data + source, "&apos;", 6u) == 0) {
			codepoint = '\'';
		} else if (reference_length == 6u &&
			   memcmp(input.data + source, "&quot;", 6u) == 0) {
			codepoint = '"';
		} else if (source + 2u < end && input.data[source + 1u] == '#') {
			size_t digit = source + 2u;
			if (digit < end && (input.data[digit] == 'x' || input.data[digit] == 'X')) {
				base = 16;
				digit++;
			}
			if (digit == end) {
				return 0;
			}
			for (; digit < end; digit++) {
				unsigned value;
				unsigned char character = (unsigned char)input.data[digit];
				if (character >= '0' && character <= '9') {
					value = (unsigned)(character - '0');
				} else if (base == 16 && character >= 'a' && character <= 'f') {
					value = (unsigned)(character - 'a') + 10u;
				} else if (base == 16 && character >= 'A' && character <= 'F') {
					value = (unsigned)(character - 'A') + 10u;
				} else {
					return 0;
				}
				if (codepoint > (0x10ffffu - value) / (unsigned)base) {
					return 0;
				}
				codepoint = codepoint * (unsigned)base + value;
			}
		} else {
			return 0;
		}
		if (!append_utf8(codepoint, storage, &destination) ||
		    destination > MXSD_MAX_TEXT_LENGTH) {
			return 0;
		}
		source = end + 1u;
	}
	storage[destination] = '\0';
	output->data = storage;
	output->length = destination;
	return 1;
}

static int parse_decimal(text_span_t value, double *number)
{
	char buffer[128];
	char *end;

	if (value.length == 0 || value.length >= sizeof(buffer)) {
		return 0;
	}

	memcpy(buffer, value.data, value.length);
	buffer[value.length] = '\0';
	*number = strtod(buffer, &end);
	return end == buffer + value.length;
}

/** Apply the XML Schema whiteSpace facet to a non-owning input span. */
static int normalize_whitespace(text_span_t input, mxsd_whitespace_t mode, text_span_t *output,
				char *storage)
{
	size_t source;
	size_t destination = 0;
	int pending_space = 0;

	*output = input;
	if (mode == MXSD_WS_PRESERVE) {
		return 1;
	}

	if (input.length > MXSD_MAX_TEXT_LENGTH) {
		return 0;
	}
	for (source = 0; source < input.length; source++) {
		char character = input.data[source];
		int xml_space = character == ' ' || character == '\t' || character == '\r' ||
				character == '\n';

		if (mode == MXSD_WS_REPLACE) {
			storage[destination++] = xml_space ? ' ' : character;
		} else if (xml_space) {
			if (destination > 0) {
				pending_space = 1;
			}
		} else {
			if (pending_space) {
				storage[destination++] = ' ';
				pending_space = 0;
			}
			storage[destination++] = character;
		}
	}
	storage[destination] = '\0';
	output->data = storage;
	output->length = destination;
	return 1;
}

int mxsd_validate_boolean(text_span_t value)
{
	return span_equals_string(value, "true") || span_equals_string(value, "false") ||
	       span_equals_string(value, "0") || span_equals_string(value, "1");
}

static int validate_number_lexical_form(mxsd_type_t type, text_span_t value)
{
	size_t index = 0;
	int negative = 0;
	int saw_digit = 0;
	int saw_decimal_point = 0;
	unsigned long accumulated = 0;

	if (index < value.length && (value.data[index] == '+' || value.data[index] == '-')) {
		negative = value.data[index] == '-';
		if (type == MXSD_T_UNSIGNED_INT && negative) {
			return 0;
		}
		index++;
	}

	for (; index < value.length; index++) {
		unsigned char character = (unsigned char)value.data[index];

		if (isdigit(character)) {
			unsigned digit = (unsigned)(character - '0');
			saw_digit = 1;

			if (type == MXSD_T_INT || type == MXSD_T_UNSIGNED_INT) {
				unsigned long limit =
					type == MXSD_T_UNSIGNED_INT
						? 4294967295UL
						: (negative ? 2147483648UL : 2147483647UL);
				if (accumulated > (limit - digit) / 10UL) {
					return 0;
				}
				accumulated = accumulated * 10UL + digit;
			}
			continue;
		}

		if (type == MXSD_T_DECIMAL && character == '.' && !saw_decimal_point) {
			saw_decimal_point = 1;
			continue;
		}

		return 0;
	}

	return saw_digit;
}

static int compare_integer_magnitude(text_span_t value, const char *limit)
{
	size_t start = 0;
	size_t limit_length = strlen(limit);
	while (start < value.length &&
	       (value.data[start] == '+' || value.data[start] == '-' || value.data[start] == '0')) {
		start++;
	}
	while (limit_length > 1u && *limit == '0') {
		limit++;
		limit_length--;
	}
	if (value.length - start != limit_length) {
		return value.length - start < limit_length ? -1 : 1;
	}
	return memcmp(value.data + start, limit, limit_length);
}

static int integer_is_zero(text_span_t value)
{
	size_t i;
	for (i = 0; i < value.length; i++) {
		if (value.data[i] != '+' && value.data[i] != '-' && value.data[i] != '0') {
			return 0;
		}
	}
	return 1;
}

static int validate_derived_integer(mxsd_type_t type, text_span_t value)
{
	int negative = value.length > 0 && value.data[0] == '-';
	int zero;
	const char *limit = NULL;
	if (!validate_number_lexical_form(MXSD_T_INTEGER, value)) {
		return 0;
	}
	zero = integer_is_zero(value);
	switch (type) {
	case MXSD_T_LONG:
		limit = negative ? "9223372036854775808" : "9223372036854775807";
		break;
	case MXSD_T_SHORT:
		limit = negative ? "32768" : "32767";
		break;
	case MXSD_T_BYTE:
		limit = negative ? "128" : "127";
		break;
	case MXSD_T_UNSIGNED_LONG:
		if (negative) {
			return 0;
		}
		limit = "18446744073709551615";
		break;
	case MXSD_T_UNSIGNED_SHORT:
		if (negative) {
			return 0;
		}
		limit = "65535";
		break;
	case MXSD_T_UNSIGNED_BYTE:
		if (negative) {
			return 0;
		}
		limit = "255";
		break;
	case MXSD_T_NON_POSITIVE_INTEGER:
		return negative || zero;
	case MXSD_T_NEGATIVE_INTEGER:
		return negative && !zero;
	case MXSD_T_NON_NEGATIVE_INTEGER:
		return !negative || zero;
	case MXSD_T_POSITIVE_INTEGER:
		return !negative && !zero;
	default:
		return 0;
	}
	return compare_integer_magnitude(value, limit) <= 0;
}

static int validate_floating_point(text_span_t value)
{
	size_t i = 0;
	int digits = 0;
	if (span_equals_string(value, "INF") || span_equals_string(value, "-INF") ||
	    span_equals_string(value, "NaN")) {
		return 1;
	}
	if (i < value.length && (value.data[i] == '+' || value.data[i] == '-')) {
		i++;
	}
	while (i < value.length && isdigit((unsigned char)value.data[i])) {
		i++;
		digits = 1;
	}
	if (i < value.length && value.data[i] == '.') {
		i++;
		while (i < value.length && isdigit((unsigned char)value.data[i])) {
			i++;
			digits = 1;
		}
	}
	if (!digits) {
		return 0;
	}
	if (i < value.length && (value.data[i] == 'e' || value.data[i] == 'E')) {
		size_t exponent_digits;
		i++;
		if (i < value.length && (value.data[i] == '+' || value.data[i] == '-')) {
			i++;
		}
		exponent_digits = i;
		while (i < value.length && isdigit((unsigned char)value.data[i])) {
			i++;
		}
		if (i == exponent_digits) {
			return 0;
		}
	}
	return i == value.length;
}

static int validate_xml_name(text_span_t value, int allow_colon, int token_only)
{
	size_t index = 0u;
	uint32_t codepoint;
	size_t consumed;

	if (value.length == 0u) {
		return 0;
	}
	consumed = mxsd_utf8_decode(value.data, value.length, &codepoint);
	if (consumed == 0u || (!token_only && (!mxsd_xml_name_start(codepoint) ||
					       (!allow_colon && codepoint == ':')))) {
		return 0;
	}
	if (!token_only) {
		index = consumed;
	}
	while (index < value.length) {
		consumed = mxsd_utf8_decode(value.data + index, value.length - index, &codepoint);
		if (consumed == 0u || !mxsd_xml_name_char(codepoint) ||
		    (!allow_colon && codepoint == ':')) {
			return 0;
		}
		index += consumed;
	}
	return 1;
}

static int all_digits(const char *text, size_t count)
{
	size_t index;
	for (index = 0; index < count; index++) {
		if (!isdigit((unsigned char)text[index])) {
			return 0;
		}
	}
	return 1;
}

static int valid_timezone(const char *text, size_t length, size_t position)
{
	unsigned hour;
	unsigned minute;
	if (position == length) {
		return 1;
	}
	if (text[position] == 'Z') {
		return position + 1u == length;
	}
	if (position + 6u != length || (text[position] != '+' && text[position] != '-') ||
	    text[position + 3u] != ':' || !all_digits(text + position + 1u, 2u) ||
	    !all_digits(text + position + 4u, 2u)) {
		return 0;
	}
	hour = (unsigned)(text[position + 1u] - '0') * 10u + (unsigned)(text[position + 2u] - '0');
	minute =
		(unsigned)(text[position + 4u] - '0') * 10u + (unsigned)(text[position + 5u] - '0');
	return hour < 14u || (hour == 14u && minute == 0u);
}

static int parse_two_digits(const char *text, unsigned *number)
{
	if (!all_digits(text, 2u)) {
		return 0;
	}
	*number = (unsigned)(text[0] - '0') * 10u + (unsigned)(text[1] - '0');
	return 1;
}

static int validate_time_value(text_span_t value, size_t start)
{
	unsigned hour, minute, second;
	size_t position = start;
	if (position + 8u > value.length || value.data[position + 2u] != ':' ||
	    value.data[position + 5u] != ':' || !parse_two_digits(value.data + position, &hour) ||
	    !parse_two_digits(value.data + position + 3u, &minute) ||
	    !parse_two_digits(value.data + position + 6u, &second)) {
		return 0;
	}
	if (minute > 59u || second > 59u || hour > 24u ||
	    (hour == 24u && (minute != 0u || second != 0u))) {
		return 0;
	}
	position += 8u;
	if (position < value.length && value.data[position] == '.') {
		size_t fraction = ++position;
		while (position < value.length && isdigit((unsigned char)value.data[position])) {
			position++;
		}
		if (position == fraction) {
			return 0;
		}
	}
	return valid_timezone(value.data, value.length, position);
}

static int leap_year(long year)
{
	return year % 4L == 0L && (year % 100L != 0L || year % 400L == 0L);
}

static int validate_date_value(text_span_t value, size_t *end)
{
	size_t position = 0;
	size_t year_start;
	long year = 0;
	unsigned month, day;
	static const unsigned char month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int negative = 0;
	if (position < value.length && value.data[position] == '-') {
		negative = 1;
		position++;
	}
	year_start = position;
	while (position < value.length && isdigit((unsigned char)value.data[position])) {
		if (year < 1000000L) {
			year = year * 10L + (long)(value.data[position] - '0');
		}
		position++;
	}
	if (position - year_start < 4u || year == 0L || position + 6u > value.length ||
	    value.data[position] != '-' || value.data[position + 3u] != '-' ||
	    !parse_two_digits(value.data + position + 1u, &month) ||
	    !parse_two_digits(value.data + position + 4u, &day) || month < 1u || month > 12u) {
		return 0;
	}
	if (negative) {
		year = -year;
	}
	if (day < 1u || day > month_days[month - 1u] + (month == 2u && leap_year(year))) {
		return 0;
	}
	*end = position + 6u;
	return 1;
}

static int validate_calendar(mxsd_type_t type, text_span_t value)
{
	size_t end;
	unsigned number;
	if (type == MXSD_T_TIME) {
		return validate_time_value(value, 0u);
	}
	if (type == MXSD_T_DATE || type == MXSD_T_DATE_TIME) {
		if (!validate_date_value(value, &end)) {
			return 0;
		}
		if (type == MXSD_T_DATE_TIME) {
			return end < value.length && value.data[end] == 'T' &&
			       validate_time_value(value, end + 1u);
		}
		return valid_timezone(value.data, value.length, end);
	}
	if (type == MXSD_T_GMONTH) {
		size_t timezone_position;
		if (value.length < 4u || value.data[0] != '-' || value.data[1] != '-' ||
		    !parse_two_digits(value.data + 2u, &number) || number < 1u || number > 12u) {
			return 0;
		}
		timezone_position =
			value.length >= 6u && value.data[4u] == '-' && value.data[5u] == '-' ? 6u
											     : 4u;
		return valid_timezone(value.data, value.length, timezone_position);
	}
	if (type == MXSD_T_GDAY) {
		return value.length >= 5u && memcmp(value.data, "---", 3u) == 0 &&
		       parse_two_digits(value.data + 3u, &number) && number >= 1u &&
		       number <= 31u && valid_timezone(value.data, value.length, 5u);
	}
	if (type == MXSD_T_GMONTH_DAY) {
		unsigned day;
		static const unsigned char month_days[] = {31, 29, 31, 30, 31, 30,
							   31, 31, 30, 31, 30, 31};
		return value.length >= 7u && value.data[0] == '-' && value.data[1] == '-' &&
		       value.data[4] == '-' && parse_two_digits(value.data + 2u, &number) &&
		       number >= 1u && number <= 12u && parse_two_digits(value.data + 5u, &day) &&
		       day >= 1u && day <= month_days[number - 1u] &&
		       valid_timezone(value.data, value.length, 7u);
	}
	/* gYear and gYearMonth share the date year lexical prefix. */
	end = value.data[0] == '-' ? 1u : 0u;
	{
		size_t start = end;
		while (end < value.length && isdigit((unsigned char)value.data[end])) {
			end++;
		}
		if (end - start < 4u ||
		    (end - start == 4u && memcmp(value.data + start, "0000", 4u) == 0)) {
			return 0;
		}
	}
	if (type == MXSD_T_GYEAR_MONTH) {
		if (end + 3u > value.length || value.data[end] != '-' ||
		    !parse_two_digits(value.data + end + 1u, &number) || number < 1u ||
		    number > 12u) {
			return 0;
		}
		end += 3u;
	}
	return valid_timezone(value.data, value.length, end);
}

static int validate_duration(text_span_t value)
{
	size_t i = 0;
	int saw = 0;
	int in_time = 0;
	int saw_time = 0;
	int last_rank = 0;
	if (i < value.length && value.data[i] == '-') {
		i++;
	}
	if (i >= value.length || value.data[i++] != 'P') {
		return 0;
	}
	while (i < value.length) {
		size_t digits;
		int fraction = 0;
		int rank;
		if (value.data[i] == 'T' && !in_time) {
			in_time = 1;
			i++;
			continue;
		}
		digits = i;
		while (i < value.length && isdigit((unsigned char)value.data[i])) {
			i++;
		}
		if (i < value.length && value.data[i] == '.') {
			fraction = 1;
			i++;
			while (i < value.length && isdigit((unsigned char)value.data[i])) {
				i++;
			}
		}
		if (i == digits || i >= value.length ||
		    strchr(in_time ? "HMS" : "YMD", value.data[i]) == NULL) {
			return 0;
		}
		if (!in_time) {
			rank = value.data[i] == 'Y' ? 1 : (value.data[i] == 'M' ? 2 : 3);
		} else {
			rank = value.data[i] == 'H' ? 4 : (value.data[i] == 'M' ? 5 : 6);
			saw_time = 1;
		}
		if (rank <= last_rank || (fraction && rank != 6)) {
			return 0;
		}
		last_rank = rank;
		saw = 1;
		i++;
	}
	return saw && (!in_time || saw_time);
}

static int validate_binary(mxsd_type_t type, text_span_t value)
{
	size_t i;
	if (type == MXSD_T_HEX_BINARY) {
		if (value.length % 2u != 0u) {
			return 0;
		}
		for (i = 0; i < value.length; i++) {
			if (!isxdigit((unsigned char)value.data[i])) {
				return 0;
			}
		}
		return 1;
	}
	{
		size_t count = 0;
		size_t padding = 0;
		for (i = 0; i < value.length; i++) {
			unsigned char c = (unsigned char)value.data[i];
			if (c == ' ') {
				continue;
			}
			count++;
			if (c == '=') {
				padding++;
			} else if (padding || !(isalnum(c) || c == '+' || c == '/')) {
				return 0;
			}
		}
		if (count % 4u != 0u || padding > 2u) {
			return 0;
		}
	}
	return 1;
}

static int validate_language(text_span_t value)
{
	size_t i;
	size_t part = 0;
	int primary = 1;
	if (value.length == 0) {
		return 0;
	}
	for (i = 0; i < value.length; i++) {
		unsigned char c = (unsigned char)value.data[i];
		if (c == '-') {
			if (part < 1u || part > 8u) {
				return 0;
			}
			part = 0;
			primary = 0;
		} else {
			if ((primary && !isalpha(c)) || (!primary && !isalnum(c))) {
				return 0;
			}
			part++;
		}
	}
	return part >= 1u && part <= 8u;
}

static int validate_primitive_lexical_form(const xml_parser_t *parser, mxsd_type_t type,
					   text_span_t value)
{
	text_span_t prefix, local;
	size_t i;
	switch (type) {
	case MXSD_T_STRING:
	case MXSD_T_NORMALIZED_STRING:
	case MXSD_T_TOKEN:
	case MXSD_T_ANY_URI:
		return 1;
	case MXSD_T_BOOLEAN:
		return validate_boolean(value);
	case MXSD_T_INT:
	case MXSD_T_UNSIGNED_INT:
	case MXSD_T_INTEGER:
	case MXSD_T_DECIMAL:
		return validate_number_lexical_form(type, value);
	case MXSD_T_FLOAT:
	case MXSD_T_DOUBLE:
		return validate_floating_point(value);
	case MXSD_T_LONG:
	case MXSD_T_SHORT:
	case MXSD_T_BYTE:
	case MXSD_T_NON_POSITIVE_INTEGER:
	case MXSD_T_NEGATIVE_INTEGER:
	case MXSD_T_NON_NEGATIVE_INTEGER:
	case MXSD_T_POSITIVE_INTEGER:
	case MXSD_T_UNSIGNED_LONG:
	case MXSD_T_UNSIGNED_SHORT:
	case MXSD_T_UNSIGNED_BYTE:
		return validate_derived_integer(type, value);
	case MXSD_T_LANGUAGE:
		return validate_language(value);
	case MXSD_T_NAME:
		return validate_xml_name(value, 1, 0);
	case MXSD_T_NCNAME:
	case MXSD_T_ID:
	case MXSD_T_IDREF:
	case MXSD_T_ENTITY:
		return validate_xml_name(value, 0, 0);
	case MXSD_T_NMTOKEN:
		return validate_xml_name(value, 1, 1);
	case MXSD_T_QNAME:
		split_qualified_name(value, &prefix, &local);
		if ((local.data != value.data && prefix.length == 0u) ||
		    !validate_xml_name(local, 0, 0) ||
		    (prefix.length && !validate_xml_name(prefix, 0, 0))) {
			return 0;
		}
		return prefix.length == 0 || resolve_namespace(parser, prefix, 0).length != 0;
	case MXSD_T_NOTATION: {
		text_span_t namespace_uri;
		size_t notation_index;
		split_qualified_name(value, &prefix, &local);
		if ((local.data != value.data && prefix.length == 0u) ||
		    !validate_xml_name(local, 0, 0) ||
		    (prefix.length && !validate_xml_name(prefix, 0, 0))) {
			return 0;
		}
		namespace_uri = resolve_namespace(parser, prefix, 0);
		if (prefix.length != 0u && namespace_uri.length == 0u) {
			return 0;
		}
		for (notation_index = 0u; notation_index < parser->notation_count;
		     ++notation_index) {
			const mxsd_notation_t *notation = &parser->notations[notation_index];
			if (span_equals_string(local, notation->name) &&
			    span_equals_nullable(namespace_uri, notation->namespace_uri)) {
				return 1;
			}
		}
		return 0;
	}
	case MXSD_T_DURATION:
		return validate_duration(value);
	case MXSD_T_DATE:
	case MXSD_T_DATE_TIME:
	case MXSD_T_TIME:
	case MXSD_T_GYEAR:
	case MXSD_T_GYEAR_MONTH:
	case MXSD_T_GMONTH:
	case MXSD_T_GMONTH_DAY:
	case MXSD_T_GDAY:
		return validate_calendar(type, value);
	case MXSD_T_HEX_BINARY:
	case MXSD_T_BASE64_BINARY:
		return validate_binary(type, value);
	default:
		break;
	}
	for (i = 0; i < value.length; i++) {
		(void)value.data[i];
	}
	return 0;
}

static size_t utf8_character_count(text_span_t value)
{
	size_t index;
	size_t count = 0;

	for (index = 0; index < value.length; index++) {
		if (((unsigned char)value.data[index] & 0xc0u) != 0x80u) {
			count++;
		}
	}
	return count;
}

static int compare_ordered(mxsd_type_t type, text_span_t left, text_span_t right, int *comparison);

/** Return whether a primitive shares the exact decimal value space. */
static int is_decimal_type(mxsd_type_t type)
{
	switch (type) {
	case MXSD_T_INT:
	case MXSD_T_UNSIGNED_INT:
	case MXSD_T_INTEGER:
	case MXSD_T_DECIMAL:
	case MXSD_T_LONG:
	case MXSD_T_SHORT:
	case MXSD_T_BYTE:
	case MXSD_T_NON_POSITIVE_INTEGER:
	case MXSD_T_NEGATIVE_INTEGER:
	case MXSD_T_NON_NEGATIVE_INTEGER:
	case MXSD_T_POSITIVE_INTEGER:
	case MXSD_T_UNSIGNED_LONG:
	case MXSD_T_UNSIGNED_SHORT:
	case MXSD_T_UNSIGNED_BYTE:
		return 1;
	default:
		return 0;
	}
}

/** Non-owning canonical view used for exact decimal comparison. */
typedef struct {
	text_span_t value;
	size_t first_digit;
	size_t length;
	long magnitude;
	int sign;
} decimal_view_t;

/** Return the next significant digit while ignoring sign and decimal point. */
static char decimal_next_digit(const decimal_view_t *view, size_t *position, size_t *ordinal)
{
	while (*position < view->value.length) {
		char character = view->value.data[(*position)++];
		if (character >= '0' && character <= '9') {
			size_t current = (*ordinal)++;
			if (current >= view->first_digit) {
				return character;
			}
		}
	}
	return '0';
}

/** Canonicalize a decimal as indexes into its original lexical form. */
static int decimal_parts(text_span_t value, decimal_view_t *view)
{
	size_t position = 0u;
	size_t digit_count = 0u;
	size_t integer_digits = 0u;
	size_t first = SIZE_MAX;
	size_t last = 0u;
	int after_point = 0;

	view->value = value;
	view->sign = 1;
	if (position < value.length &&
	    (value.data[position] == '+' || value.data[position] == '-')) {
		if (value.data[position] == '-') {
			view->sign = -1;
		}
		position++;
	}
	for (; position < value.length; position++) {
		char character = value.data[position];
		if (character == '.' && !after_point) {
			after_point = 1;
			continue;
		}
		if (character < '0' || character > '9') {
			return 0;
		}
		if (character != '0') {
			if (first == SIZE_MAX) {
				first = digit_count;
			}
			last = digit_count;
		}
		digit_count++;
		if (!after_point) {
			integer_digits++;
		}
	}
	if (first == SIZE_MAX) {
		view->sign = 0;
		view->first_digit = 0u;
		view->length = 0u;
		view->magnitude = 0L;
		return 1;
	}
	view->first_digit = first;
	view->length = last - first + 1u;
	view->magnitude = (long)integer_digits - (long)first;
	return 1;
}

/** Compare two decimal values without losing precision through floating point. */
static int compare_decimal_exact(text_span_t left, text_span_t right, int *comparison)
{
	decimal_view_t left_view;
	decimal_view_t right_view;
	size_t index, maximum;
	size_t left_position = 0u, right_position = 0u;
	size_t left_ordinal = 0u, right_ordinal = 0u;

	if (!decimal_parts(left, &left_view) || !decimal_parts(right, &right_view)) {
		return 0;
	}
	if (left_view.sign != right_view.sign) {
		*comparison =
			(left_view.sign > right_view.sign) - (left_view.sign < right_view.sign);
		return 1;
	}
	if (left_view.sign == 0) {
		*comparison = 0;
		return 1;
	}
	if (left_view.magnitude != right_view.magnitude) {
		*comparison = (left_view.magnitude > right_view.magnitude) -
			      (left_view.magnitude < right_view.magnitude);
		*comparison *= left_view.sign;
		return 1;
	}
	maximum = left_view.length > right_view.length ? left_view.length : right_view.length;
	for (index = 0u; index < maximum; index++) {
		char left_digit =
			index < left_view.length
				? decimal_next_digit(&left_view, &left_position, &left_ordinal)
				: '0';
		char right_digit =
			index < right_view.length
				? decimal_next_digit(&right_view, &right_position, &right_ordinal)
				: '0';
		if (left_digit != right_digit) {
			*comparison = (left_digit > right_digit ? 1 : -1) * left_view.sign;
			return 1;
		}
	}
	*comparison = 0;
	return 1;
}

static int numeric_value_equal(mxsd_type_t type, text_span_t left, const char *right)
{
	text_span_t right_span = {right, strlen(right)};
	if (type == MXSD_T_BOOLEAN) {
		int left_true = span_equals_string(left, "true") || span_equals_string(left, "1");
		int right_true = strcmp(right, "true") == 0 || strcmp(right, "1") == 0;
		return validate_boolean(left) && validate_boolean(right_span) &&
		       left_true == right_true;
	}
	if (is_decimal_type(type)) {
		int comparison;
		return compare_decimal_exact(left, right_span, &comparison) && comparison == 0;
	}
	if (type == MXSD_T_FLOAT || type == MXSD_T_DOUBLE) {
		double left_number;
		double right_number;
		return parse_decimal(left, &left_number) &&
		       parse_decimal(right_span, &right_number) && left_number == right_number;
	}
	if (type == MXSD_T_DURATION || type == MXSD_T_DATE || type == MXSD_T_DATE_TIME ||
	    type == MXSD_T_TIME || type == MXSD_T_GYEAR || type == MXSD_T_GYEAR_MONTH ||
	    type == MXSD_T_GMONTH || type == MXSD_T_GMONTH_DAY || type == MXSD_T_GDAY) {
		int comparison;
		return compare_ordered(type, left, right_span, &comparison) && comparison == 0;
	}
	return 0;
}

static int validate_enumeration(const xml_parser_t *parser, const mxsd_simple_t *simple,
				text_span_t value)
{
	size_t index;

	if (simple->enumeration_count == 0) {
		return 1;
	}

	for (index = 0; index < simple->enumeration_count; index++) {
		if ((simple->base == MXSD_T_QNAME || simple->base == MXSD_T_NOTATION) &&
		    simple->enumeration_namespaces != NULL) {
			text_span_t prefix, local, namespace_uri;
			split_qualified_name(value, &prefix, &local);
			namespace_uri = resolve_namespace(parser, prefix, 0);
			if (span_equals_string(local, simple->enumeration[index]) &&
			    span_equals_nullable(namespace_uri,
						 simple->enumeration_namespaces[index])) {
				return 1;
			}
			continue;
		}
		if (span_equals_string(value, simple->enumeration[index]) ||
		    numeric_value_equal(simple->base, value, simple->enumeration[index])) {
			return 1;
		}
	}
	return 0;
}

static size_t binary_value_length(mxsd_type_t type, text_span_t value)
{
	if (type == MXSD_T_HEX_BINARY) {
		return value.length / 2u;
	}
	if (type == MXSD_T_BASE64_BINARY) {
		size_t i, characters = 0, padding = 0;
		for (i = 0; i < value.length; i++) {
			if (value.data[i] != ' ') {
				characters++;
				if (value.data[i] == '=') {
					padding++;
				}
			}
		}
		return characters / 4u * 3u - padding;
	}
	return utf8_character_count(value);
}

static int parse_duration_order(text_span_t value, double *months, double *seconds)
{
	size_t position = 0u;
	double sign = 1.0;
	int in_time = 0;
	int saw = 0;
	*months = 0.0;
	*seconds = 0.0;
	if (position < value.length && value.data[position] == '-') {
		sign = -1.0;
		position++;
	}
	if (position >= value.length || value.data[position++] != 'P') {
		return 0;
	}
	while (position < value.length) {
		double number = 0.0;
		double scale = 0.0;
		char designator;
		if (value.data[position] == 'T' && !in_time) {
			in_time = 1;
			position++;
			continue;
		}
		if (position >= value.length || !isdigit((unsigned char)value.data[position])) {
			return 0;
		}
		while (position < value.length && isdigit((unsigned char)value.data[position])) {
			number = number * 10.0 + (double)(value.data[position++] - '0');
		}
		if (position < value.length && value.data[position] == '.') {
			position++;
			scale = 0.1;
			while (position < value.length &&
			       isdigit((unsigned char)value.data[position])) {
				number += (double)(value.data[position++] - '0') * scale;
				scale *= 0.1;
			}
		}
		if (position >= value.length) {
			return 0;
		}
		designator = value.data[position++];
		if (!in_time && designator == 'Y') {
			*months += number * 12.0;
		} else if (!in_time && designator == 'M') {
			*months += number;
		} else if (!in_time && designator == 'D') {
			*seconds += number * 86400.0;
		} else if (in_time && designator == 'H') {
			*seconds += number * 3600.0;
		} else if (in_time && designator == 'M') {
			*seconds += number * 60.0;
		} else if (in_time && designator == 'S') {
			*seconds += number;
		} else {
			return 0;
		}
		saw = 1;
	}
	*months *= sign;
	*seconds *= sign;
	return saw;
}

static int parse_timezone_order(text_span_t value, size_t position, double *offset, int *specified)
{
	unsigned hour;
	unsigned minute;
	*offset = 0.0;
	*specified = 0;
	if (position == value.length) {
		return 1;
	}
	*specified = 1;
	if (value.data[position] == 'Z') {
		return position + 1u == value.length;
	}
	if (position + 6u != value.length ||
	    (value.data[position] != '+' && value.data[position] != '-') ||
	    value.data[position + 3u] != ':' ||
	    !parse_two_digits(value.data + position + 1u, &hour) ||
	    !parse_two_digits(value.data + position + 4u, &minute)) {
		return 0;
	}
	*offset = (double)(hour * 60u + minute) * 60.0;
	if (value.data[position] == '-') {
		*offset = -*offset;
	}
	return 1;
}

static int parse_time_order(text_span_t value, size_t start, double *number, double *uncertainty)
{
	unsigned hour;
	unsigned minute;
	unsigned second;
	size_t position = start;
	double fraction = 0.0;
	double scale = 0.1;
	double offset;
	int specified;
	if (position + 8u > value.length || !parse_two_digits(value.data + position, &hour) ||
	    !parse_two_digits(value.data + position + 3u, &minute) ||
	    !parse_two_digits(value.data + position + 6u, &second)) {
		return 0;
	}
	position += 8u;
	if (position < value.length && value.data[position] == '.') {
		position++;
		while (position < value.length && isdigit((unsigned char)value.data[position])) {
			fraction += (double)(value.data[position++] - '0') * scale;
			scale *= 0.1;
		}
	}
	if (!parse_timezone_order(value, position, &offset, &specified)) {
		return 0;
	}
	*number = (double)(hour * 3600u + minute * 60u + second) + fraction - offset;
	*uncertainty = specified ? 0.0 : 14.0 * 3600.0;
	return 1;
}

static long calendar_day_number(long year, unsigned month, unsigned day)
{
	long adjusted_year = year - (month <= 2u ? 1L : 0L);
	long era = adjusted_year >= 0L ? adjusted_year / 400L : (adjusted_year - 399L) / 400L;
	unsigned year_of_era = (unsigned)(adjusted_year - era * 400L);
	unsigned shifted_month = month > 2u ? month - 3u : month + 9u;
	unsigned day_of_year = (153u * shifted_month + 2u) / 5u + day - 1u;
	return era * 146097L +
	       (long)(year_of_era * 365u + year_of_era / 4u - year_of_era / 100u + day_of_year);
}

static unsigned calendar_month_days(long year, unsigned month)
{
	static const unsigned char days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	return days[month - 1u] + (month == 2u && leap_year(year));
}

static double duration_reference_value(long year, unsigned month, unsigned day, double months,
				       double seconds)
{
	long month_index = year * 12L + (long)month - 1L + (long)months;
	long result_year;
	unsigned result_month;
	unsigned maximum_day;

	if (month_index >= 0L) {
		result_year = month_index / 12L;
	} else {
		result_year = (month_index - 11L) / 12L;
	}
	result_month = (unsigned)(month_index - result_year * 12L) + 1u;
	maximum_day = calendar_month_days(result_year, result_month);
	if (day > maximum_day) {
		day = maximum_day;
	}
	return (double)calendar_day_number(result_year, result_month, day) * 86400.0 + seconds;
}

static int compare_duration(text_span_t left, text_span_t right, int *comparison)
{
	static const struct {
		long year;
		unsigned month;
		unsigned day;
	} references[] = {{1696L, 9u, 1u}, {1697L, 2u, 1u}, {1903L, 3u, 1u}, {1903L, 7u, 1u}};
	double left_months;
	double left_seconds;
	double right_months;
	double right_seconds;
	int result = 0;
	size_t index;

	if (!parse_duration_order(left, &left_months, &left_seconds) ||
	    !parse_duration_order(right, &right_months, &right_seconds)) {
		return 0;
	}
	for (index = 0u; index < sizeof(references) / sizeof(references[0]); ++index) {
		double left_value =
			duration_reference_value(references[index].year, references[index].month,
						 references[index].day, left_months, left_seconds);
		double right_value = duration_reference_value(
			references[index].year, references[index].month, references[index].day,
			right_months, right_seconds);
		int current = (left_value > right_value) - (left_value < right_value);

		if (current != 0) {
			if (result != 0 && result != current) {
				return 0;
			}
			result = current;
		}
	}
	*comparison = result;
	return 1;
}

static int parse_calendar_order(mxsd_type_t type, text_span_t value, double *number,
				int *timezone_specified)
{
	if (type == MXSD_T_TIME) {
		double uncertainty;
		int result = parse_time_order(value, 0u, number, &uncertainty);
		*timezone_specified = uncertainty == 0.0;
		return result;
	}
	if (type == MXSD_T_DATE || type == MXSD_T_DATE_TIME) {
		size_t position = 0u;
		size_t year_start;
		long year = 0L;
		unsigned month;
		unsigned day;
		int negative = 0;
		double offset;
		if (position < value.length && value.data[position] == '-') {
			negative = 1;
			position++;
		}
		year_start = position;
		while (position < value.length && isdigit((unsigned char)value.data[position])) {
			year = year * 10L + (long)(value.data[position++] - '0');
		}
		if (position == year_start || position + 6u > value.length ||
		    !parse_two_digits(value.data + position + 1u, &month) ||
		    !parse_two_digits(value.data + position + 4u, &day)) {
			return 0;
		}
		if (negative) {
			year = -year;
		}
		position += 6u;
		*number = (double)calendar_day_number(year, month, day) * 86400.0;
		if (type == MXSD_T_DATE_TIME) {
			double time_number;
			double uncertainty;
			if (position >= value.length || value.data[position] != 'T' ||
			    !parse_time_order(value, position + 1u, &time_number, &uncertainty)) {
				return 0;
			}
			*number += time_number;
			*timezone_specified = uncertainty == 0.0;
			return 1;
		}
		if (!parse_timezone_order(value, position, &offset, timezone_specified)) {
			return 0;
		}
		*number -= offset;
		return 1;
	}
	if (type == MXSD_T_GYEAR || type == MXSD_T_GYEAR_MONTH) {
		size_t position = 0u;
		long year = 0L;
		int negative = 0;
		unsigned month = 1u;
		double offset;
		if (position < value.length && value.data[position] == '-') {
			negative = 1;
			position++;
		}
		while (position < value.length && isdigit((unsigned char)value.data[position])) {
			year = year * 10L + (long)(value.data[position++] - '0');
		}
		if (negative) {
			year = -year;
		}
		if (type == MXSD_T_GYEAR_MONTH) {
			if (position + 3u > value.length || value.data[position] != '-' ||
			    !parse_two_digits(value.data + position + 1u, &month)) {
				return 0;
			}
			position += 3u;
		}
		if (!parse_timezone_order(value, position, &offset, timezone_specified)) {
			return 0;
		}
		*number = (double)calendar_day_number(year, month, 1u) * 86400.0 - offset;
		return 1;
	}
	if (type == MXSD_T_GMONTH_DAY || type == MXSD_T_GMONTH || type == MXSD_T_GDAY) {
		size_t position;
		unsigned month = 1u;
		unsigned day = 1u;
		double offset;
		if (type == MXSD_T_GDAY) {
			if (value.length < 5u || !parse_two_digits(value.data + 3u, &day)) {
				return 0;
			}
			position = 5u;
		} else {
			if (value.length < 4u || !parse_two_digits(value.data + 2u, &month)) {
				return 0;
			}
			if (type == MXSD_T_GMONTH_DAY) {
				if (value.length < 7u || !parse_two_digits(value.data + 5u, &day)) {
					return 0;
				}
				position = 7u;
			} else {
				position = value.length >= 6u && value.data[4u] == '-' &&
							   value.data[5u] == '-'
						   ? 6u
						   : 4u;
			}
		}
		if (!parse_timezone_order(value, position, &offset, timezone_specified)) {
			return 0;
		}
		*number = (double)calendar_day_number(2000L, month, day) * 86400.0 - offset;
		return 1;
	}
	return 0;
}

static int compare_ordered(mxsd_type_t type, text_span_t left, text_span_t right, int *comparison)
{
	double left_value;
	double right_value;
	if (type == MXSD_T_DURATION) {
		return compare_duration(left, right, comparison);
	}
	if (type == MXSD_T_DATE || type == MXSD_T_DATE_TIME || type == MXSD_T_TIME ||
	    type == MXSD_T_GYEAR || type == MXSD_T_GYEAR_MONTH || type == MXSD_T_GMONTH ||
	    type == MXSD_T_GMONTH_DAY || type == MXSD_T_GDAY) {
		int left_timezone;
		int right_timezone;
		double left_uncertainty;
		double right_uncertainty;
		if (!parse_calendar_order(type, left, &left_value, &left_timezone) ||
		    !parse_calendar_order(type, right, &right_value, &right_timezone)) {
			return 0;
		}
		if (left_timezone == right_timezone) {
			*comparison = (left_value > right_value) - (left_value < right_value);
			return 1;
		}
		left_uncertainty = left_timezone ? 0.0 : 14.0 * 3600.0;
		right_uncertainty = right_timezone ? 0.0 : 14.0 * 3600.0;
		if (left_value + left_uncertainty < right_value - right_uncertainty) {
			*comparison = -1;
			return 1;
		}
		if (left_value - left_uncertainty > right_value + right_uncertainty) {
			*comparison = 1;
			return 1;
		}
		return 0;
	}
	if (is_decimal_type(type)) {
		return compare_decimal_exact(left, right, comparison);
	}
	if (!parse_decimal(left, &left_value) || !parse_decimal(right, &right_value)) {
		return 0;
	}
	*comparison = (left_value > right_value) - (left_value < right_value);
	return 1;
}

static int compare_bound(mxsd_type_t type, text_span_t value, const char *bound_text, int inclusive,
			 int is_minimum)
{
	text_span_t bound_span = {bound_text, strlen(bound_text)};
	int comparison;

	if (!compare_ordered(type, value, bound_span, &comparison)) {
		return 0;
	}

	if (is_minimum) {
		return inclusive ? comparison >= 0 : comparison > 0;
	}
	return inclusive ? comparison <= 0 : comparison < 0;
}

static int validate_digit_facets(const mxsd_simple_t *simple, text_span_t value)
{
	size_t start = 0u;
	size_t point = value.length;
	size_t end = value.length;
	size_t first;
	size_t index;
	size_t total;
	size_t fraction;

	if (start < value.length && (value.data[start] == '+' || value.data[start] == '-')) {
		start++;
	}
	for (index = start; index < value.length; index++) {
		if (value.data[index] == '.') {
			point = index;
			break;
		}
	}
	while (end > point + (point < value.length ? 1u : 0u) && value.data[end - 1u] == '0') {
		end--;
	}
	first = start;
	while (first < point && value.data[first] == '0') {
		first++;
	}
	if (first == point && point < end) {
		first++;
		while (first < end && value.data[first] == '0') {
			first++;
		}
	}
	total = first == end ? 1u : end - first - (first < point && point < end ? 1u : 0u);
	fraction = point < end ? end - point - 1u : 0u;
	if ((simple->facets & MXSD_F_TOTAL_DIGITS) && total > simple->total_digits) {
		return 0;
	}
	if ((simple->facets & MXSD_F_FRACTION_DIGITS) && fraction > simple->fraction_digits) {
		return 0;
	}
	return 1;
}

static int validate_facets(const xml_parser_t *parser, const mxsd_simple_t *simple,
			   text_span_t value, size_t value_length)
{

	if ((simple->facets & MXSD_F_LENGTH) && value_length != simple->length) {
		return 0;
	}
	if ((simple->facets & MXSD_F_MIN_LENGTH) && value_length < simple->min_length) {
		return 0;
	}
	if ((simple->facets & MXSD_F_MAX_LENGTH) && value_length > simple->max_length) {
		return 0;
	}
	if (!validate_enumeration(parser, simple, value)) {
		return 0;
	}
	if (simple->facets & MXSD_F_PATTERN) {
		size_t index;
		if (simple->pattern_count > 0u) {
			for (index = 0u; index < simple->pattern_count; ++index) {
				if (!mxsd_pattern_matches(simple->patterns[index], value)) {
					return 0;
				}
			}
		} else if (!mxsd_pattern_matches(simple->pattern, value)) {
			return 0;
		}
	}
	if ((simple->facets & MXSD_F_MIN_INCLUSIVE) &&
	    !compare_bound(simple->base, value, simple->min_inclusive, 1, 1)) {
		return 0;
	}
	if ((simple->facets & MXSD_F_MAX_INCLUSIVE) &&
	    !compare_bound(simple->base, value, simple->max_inclusive, 1, 0)) {
		return 0;
	}
	if ((simple->facets & MXSD_F_MIN_EXCLUSIVE) &&
	    !compare_bound(simple->base, value, simple->min_exclusive, 0, 1)) {
		return 0;
	}
	if ((simple->facets & MXSD_F_MAX_EXCLUSIVE) &&
	    !compare_bound(simple->base, value, simple->max_exclusive, 0, 0)) {
		return 0;
	}
	if (!validate_digit_facets(simple, value)) {
		return 0;
	}

	return 1;
}

static int validate_simple_value_decoded(const xml_parser_t *parser, mxsd_type_t type,
					 const mxsd_simple_t *simple, text_span_t value)
{
	mxsd_type_t effective_type = simple != NULL ? simple->base : type;
	mxsd_whitespace_t whitespace = MXSD_WS_COLLAPSE;
	mxsd_simple_variety_t variety = MXSD_SIMPLE_ATOMIC;
	text_span_t normalized;
	char storage[MXSD_MAX_TEXT_LENGTH + 1u];
	size_t logical_length;
	int valid = 0;

	if (simple != NULL) {
		whitespace = simple->whitespace;
		variety = simple->variety;
	} else if (type == MXSD_T_STRING) {
		whitespace = MXSD_WS_PRESERVE;
	} else if (type == MXSD_T_NORMALIZED_STRING) {
		whitespace = MXSD_WS_REPLACE;
	}
	if (value.length > MXSD_MAX_TEXT_LENGTH ||
	    !normalize_whitespace(value, whitespace, &normalized, storage)) {
		return 0;
	}

	if (variety == MXSD_SIMPLE_UNION) {
		size_t index;
		for (index = 0; index < simple->member_type_count; index++) {
			const mxsd_simple_ref_t *member = &simple->member_types[index];
			if (validate_simple_value_decoded(parser, member->type, member->simple,
							  normalized)) {
				valid = 1;
				break;
			}
		}
		logical_length = utf8_character_count(normalized);
	} else if (variety == MXSD_SIMPLE_LIST) {
		size_t position = 0;
		size_t count = 0;
		valid = 1;
		while (valid && position < normalized.length) {
			size_t start = position;
			text_span_t item;
			while (position < normalized.length && normalized.data[position] != ' ') {
				position++;
			}
			item.data = normalized.data + start;
			item.length = position - start;
			valid = validate_simple_value_decoded(parser, simple->item_type.type,
							      simple->item_type.simple, item);
			count++;
			if (position < normalized.length) {
				position++;
			}
		}
		logical_length = count;
	} else {
		valid = validate_primitive_lexical_form(parser, effective_type, normalized);
		logical_length = binary_value_length(effective_type, normalized);
	}

	if (valid && simple != NULL) {
		valid = validate_facets(parser, simple, normalized, logical_length);
	}
	return valid;
}

int mxsd_validate_simple_value(const xml_parser_t *parser, mxsd_type_t type,
			       const mxsd_simple_t *simple, text_span_t value)
{
	text_span_t decoded = value;
	char storage[MXSD_MAX_TEXT_LENGTH + 1u];

	if (!parser->values_are_decoded && !mxsd_decode_xml_references(value, &decoded, storage)) {
		return 0;
	}
	return validate_simple_value_decoded(parser, type, simple, decoded);
}

int mxsd_simple_values_equal(const xml_parser_t *parser, mxsd_type_t type,
			     const mxsd_simple_t *simple, text_span_t left, const char *right_text)
{
	mxsd_whitespace_t whitespace = MXSD_WS_COLLAPSE;
	text_span_t right = {right_text, strlen(right_text)};
	text_span_t normalized_left, normalized_right;
	char left_storage[MXSD_MAX_TEXT_LENGTH + 1u];
	char right_storage[MXSD_MAX_TEXT_LENGTH + 1u];
	char decoded_storage[MXSD_MAX_TEXT_LENGTH + 1u];
	mxsd_type_t effective = simple ? simple->base : type;
	int equal;
	if (simple != NULL && simple->variety == MXSD_SIMPLE_UNION) {
		const mxsd_simple_ref_t *left_member = NULL;
		size_t index;
		int right_valid = 0;

		for (index = 0u; index < simple->member_type_count; index++) {
			const mxsd_simple_ref_t *member = &simple->member_types[index];
			text_span_t right = {right_text, strlen(right_text)};

			if (left_member == NULL &&
			    mxsd_validate_simple_value(parser, member->type, member->simple,
						       left)) {
				left_member = member;
			}
			if (!right_valid && validate_simple_value_decoded(parser, member->type,
									  member->simple, right)) {
				right_valid = 1;
			}
		}
		if (left_member == NULL || !right_valid) {
			return 0;
		}
		return mxsd_simple_values_equal(parser, left_member->type, left_member->simple,
						left, right_text);
	}
	if (!parser->values_are_decoded &&
	    !mxsd_decode_xml_references(left, &left, decoded_storage)) {
		return 0;
	}
	if (simple) {
		whitespace = simple->whitespace;
	} else if (type == MXSD_T_STRING) {
		whitespace = MXSD_WS_PRESERVE;
	} else if (type == MXSD_T_NORMALIZED_STRING) {
		whitespace = MXSD_WS_REPLACE;
	}
	if (!normalize_whitespace(left, whitespace, &normalized_left, left_storage)) {
		return 0;
	}
	if (!normalize_whitespace(right, whitespace, &normalized_right, right_storage)) {
		return 0;
	}
	equal = (normalized_left.length == normalized_right.length &&
		 memcmp(normalized_left.data, normalized_right.data, normalized_left.length) ==
			 0) ||
		numeric_value_equal(effective, normalized_left, normalized_right.data);
	return equal;
}
