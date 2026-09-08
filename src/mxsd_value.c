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

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static int character_class_matches(const char **pattern, char character)
{
	const char *cursor = *pattern;
	int negate = 0;
	int matched = 0;

	if (*cursor == '^') {
		negate = 1;
		cursor++;
	}

	while (*cursor != '\0' && *cursor != ']') {
		char first = *cursor++;

		if (*cursor == '-' && cursor[1] != '\0' && cursor[1] != ']') {
			char last = cursor[1];
			cursor += 2;
			if (character >= first && character <= last) {
				matched = 1;
			}
		} else if (character == first) {
			matched = 1;
		}
	}

	if (*cursor != ']') {
		return -1;
	}

	*pattern = cursor + 1;
	return negate ? !matched : matched;
}

static int pattern_atom_matches(const char *atom, size_t atom_length, char character)
{
	if (atom_length == 1 && atom[0] == '.') {
		return character != '\n' && character != '\r';
	}

	if (atom_length == 2 && atom[0] == '\\') {
		switch (atom[1]) {
		case 'd':
			return isdigit((unsigned char)character) != 0;
		case 's':
			return isspace((unsigned char)character) != 0;
		case 'w':
			return isalnum((unsigned char)character) || character == '_';
		default:
			return character == atom[1];
		}
	}

	if (atom_length >= 2 && atom[0] == '[') {
		const char *class_body = atom + 1;
		return character_class_matches(&class_body, character) > 0;
	}

	return atom_length == 1 && atom[0] == character;
}

static int parse_pattern_quantifier(const char **pattern, size_t *minimum, size_t *maximum)
{
	const char *cursor = *pattern;

	*minimum = 1;
	*maximum = 1;

	if (*cursor == '*' || *cursor == '+' || *cursor == '?') {
		*minimum = (*cursor == '+') ? 1u : 0u;
		*maximum = (*cursor == '?') ? 1u : SIZE_MAX;
		*pattern = cursor + 1;
		return 1;
	}

	if (*cursor != '{') {
		return 1;
	}

	cursor++;
	*minimum = 0;
	while (isdigit((unsigned char)*cursor)) {
		*minimum = *minimum * 10u + (size_t)(*cursor - '0');
		cursor++;
	}

	*maximum = *minimum;
	if (*cursor == ',') {
		cursor++;
		if (*cursor == '}') {
			*maximum = SIZE_MAX;
		} else {
			*maximum = 0;
			while (isdigit((unsigned char)*cursor)) {
				*maximum = *maximum * 10u + (size_t)(*cursor - '0');
				cursor++;
			}
		}
	}

	if (*cursor != '}' || *minimum > *maximum) {
		return 0;
	}

	*pattern = cursor + 1;
	return 1;
}

static int pattern_matches_here(const char *pattern, const char *value, size_t value_length)
{
	const char *next_atom = pattern;
	size_t atom_length;
	size_t minimum;
	size_t maximum;
	size_t matched = 0;

	if (*pattern == '\0') {
		return value_length == 0;
	}

	if (*next_atom == '\\' && next_atom[1] != '\0') {
		next_atom += 2;
	} else if (*next_atom == '[') {
		next_atom++;
		while (*next_atom != '\0' && *next_atom != ']') {
			next_atom++;
		}
		if (*next_atom == '\0') {
			return 0;
		}
		next_atom++;
	} else {
		next_atom++;
	}

	atom_length = (size_t)(next_atom - pattern);
	if (!parse_pattern_quantifier(&next_atom, &minimum, &maximum)) {
		return 0;
	}

	while (matched < value_length && matched < maximum &&
	       pattern_atom_matches(pattern, atom_length, value[matched])) {
		matched++;
	}

	for (;;) {
		if (matched < minimum) {
			return 0;
		}
		if (pattern_matches_here(next_atom, value + matched, value_length - matched)) {
			return 1;
		}
		if (matched == 0) {
			return 0;
		}
		matched--;
	}
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

static int is_name_start(unsigned char c, int allow_colon)
{
	return isalpha(c) || c == '_' || (allow_colon && c == ':') || c >= 0x80u;
}

static int is_name_character(unsigned char c, int allow_colon)
{
	return is_name_start(c, allow_colon) || isdigit(c) || c == '-' || c == '.';
}

static int validate_xml_name(text_span_t value, int allow_colon, int token_only)
{
	size_t index;
	if (value.length == 0 ||
	    (!token_only && !is_name_start((unsigned char)value.data[0], allow_colon))) {
		return 0;
	}
	for (index = token_only ? 0u : 1u; index < value.length; index++) {
		if (!is_name_character((unsigned char)value.data[index], allow_colon)) {
			return 0;
		}
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
		return value.length >= 6u && value.data[0] == '-' && value.data[1] == '-' &&
		       parse_two_digits(value.data + 2u, &number) && number >= 1u &&
		       number <= 12u && value.data[4u] == '-' && value.data[5u] == '-' &&
		       valid_timezone(value.data, value.length, 6u);
	}
	if (type == MXSD_T_GDAY) {
		return value.length >= 5u && memcmp(value.data, "---", 3u) == 0 &&
		       parse_two_digits(value.data + 3u, &number) && number >= 1u &&
		       number <= 31u && valid_timezone(value.data, value.length, 5u);
	}
	if (type == MXSD_T_GMONTH_DAY) {
		unsigned day;
		return value.length >= 7u && value.data[0] == '-' && value.data[1] == '-' &&
		       value.data[4] == '-' && parse_two_digits(value.data + 2u, &number) &&
		       number >= 1u && number <= 12u && parse_two_digits(value.data + 5u, &day) &&
		       day >= 1u && day <= 31u && valid_timezone(value.data, value.length, 7u);
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
	if (i < value.length && value.data[i] == '-') {
		i++;
	}
	if (i >= value.length || value.data[i++] != 'P') {
		return 0;
	}
	while (i < value.length) {
		size_t digits;
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
			i++;
			while (i < value.length && isdigit((unsigned char)value.data[i])) {
				i++;
			}
		}
		if (i == digits || i >= value.length ||
		    strchr(in_time ? "HMS" : "YMD", value.data[i]) == NULL) {
			return 0;
		}
		saw = 1;
		i++;
	}
	return saw;
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
		} else {
			if (!isalnum(c)) {
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
	case MXSD_T_NOTATION:
		split_qualified_name(value, &prefix, &local);
		if (!validate_xml_name(local, 0, 0) ||
		    (prefix.length && !validate_xml_name(prefix, 0, 0))) {
			return 0;
		}
		return prefix.length == 0 || resolve_namespace(parser, prefix, 0).length != 0;
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

static int numeric_value_equal(mxsd_type_t type, text_span_t left, const char *right)
{
	text_span_t right_span = {right, strlen(right)};
	double left_number;
	double right_number;
	if (type == MXSD_T_BOOLEAN) {
		int left_true = span_equals_string(left, "true") || span_equals_string(left, "1");
		int right_true = strcmp(right, "true") == 0 || strcmp(right, "1") == 0;
		return validate_boolean(left) && validate_boolean(right_span) &&
		       left_true == right_true;
	}
	if (type >= MXSD_T_INT && type <= MXSD_T_UNSIGNED_BYTE) {
		return parse_decimal(left, &left_number) &&
		       parse_decimal(right_span, &right_number) && left_number == right_number;
	}
	return 0;
}

static int validate_enumeration(const mxsd_simple_t *simple, text_span_t value)
{
	size_t index;

	if (simple->enumeration_count == 0) {
		return 1;
	}

	for (index = 0; index < simple->enumeration_count; index++) {
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

static int compare_bound(text_span_t value, const char *bound_text, int inclusive, int is_minimum)
{
	text_span_t bound_span = {bound_text, strlen(bound_text)};
	double number;
	double bound;

	if (!parse_decimal(value, &number) || !parse_decimal(bound_span, &bound)) {
		return 0;
	}

	if (is_minimum) {
		return inclusive ? number >= bound : number > bound;
	}
	return inclusive ? number <= bound : number < bound;
}

static int validate_facets(const mxsd_simple_t *simple, text_span_t value, size_t value_length)
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
	if (!validate_enumeration(simple, value)) {
		return 0;
	}
	if ((simple->facets & MXSD_F_PATTERN) &&
	    !pattern_matches_here(simple->pattern, value.data, value.length)) {
		return 0;
	}
	if ((simple->facets & MXSD_F_MIN_INCLUSIVE) &&
	    !compare_bound(value, simple->min_inclusive, 1, 1)) {
		return 0;
	}
	if ((simple->facets & MXSD_F_MAX_INCLUSIVE) &&
	    !compare_bound(value, simple->max_inclusive, 1, 0)) {
		return 0;
	}
	if ((simple->facets & MXSD_F_MIN_EXCLUSIVE) &&
	    !compare_bound(value, simple->min_exclusive, 0, 1)) {
		return 0;
	}
	if ((simple->facets & MXSD_F_MAX_EXCLUSIVE) &&
	    !compare_bound(value, simple->max_exclusive, 0, 0)) {
		return 0;
	}

	return 1;
}

int mxsd_validate_simple_value(const xml_parser_t *parser, mxsd_type_t type,
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
			if (validate_simple_value(parser, member->type, member->simple,
						  normalized)) {
				valid = 1;
				break;
			}
		}
		logical_length = utf8_character_count(normalized);
	} else if (variety == MXSD_SIMPLE_LIST) {
		size_t position = 0;
		size_t count = 0;
		valid = normalized.length > 0;
		while (valid && position < normalized.length) {
			size_t start = position;
			text_span_t item;
			while (position < normalized.length && normalized.data[position] != ' ') {
				position++;
			}
			item.data = normalized.data + start;
			item.length = position - start;
			valid = validate_simple_value(parser, simple->item_type.type,
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
		valid = validate_facets(simple, normalized, logical_length);
	}
	return valid;
}

int mxsd_simple_values_equal(const xml_parser_t *parser, mxsd_type_t type,
			     const mxsd_simple_t *simple, text_span_t left, const char *right_text)
{
	mxsd_whitespace_t whitespace = MXSD_WS_COLLAPSE;
	text_span_t right = {right_text, strlen(right_text)};
	text_span_t normalized_left, normalized_right;
	char left_storage[MXSD_MAX_TEXT_LENGTH + 1u];
	char right_storage[MXSD_MAX_TEXT_LENGTH + 1u];
	mxsd_type_t effective = simple ? simple->base : type;
	int equal;
	(void)parser;
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
