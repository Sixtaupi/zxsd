/* Copyright (c) 2026 Romain Pelletant
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief Allocation-free XML Schema 1.0 regular-expression engine. */

#include "mxsd_regex.h"

#include <limits.h>
#include <string.h>

#define INVALID_NODE   UINT16_MAX
#define POSITION_WORDS ((MXSD_MAX_TEXT_LENGTH + 64u) / 64u)
#define BRANCH_LEVELS  10u

typedef enum {
	RX_EMPTY,
	RX_ATOM,
	RX_CONCAT,
	RX_ALT,
	RX_REPEAT
} node_kind_t;
typedef struct {
	node_kind_t kind;
	uint16_t left, right, offset, length;
	uint32_t minimum, maximum;
} node_t;
typedef struct {
	const char *text;
	size_t length, position, depth, count;
	node_t nodes[MXSD_MAX_REGEX_NODES];
	int valid;
} parser_t;
typedef struct {
	uint64_t words[POSITION_WORDS];
} position_set_t;
typedef struct {
	const char *pattern;
	const node_t *nodes;
	text_span_t value;
	size_t word_count;
} matcher_t;

/** Decode one shortest-form UTF-8 scalar and return its byte length. */
size_t mxsd_utf8_decode(const char *text, size_t length, uint32_t *codepoint)
{
	const unsigned char *bytes = (const unsigned char *)text;
	uint32_t result;
	size_t count, index;

	if (length == 0u) {
		return 0u;
	}
	if (bytes[0] < 0x80u) {
		*codepoint = bytes[0];
		return 1u;
	}
	if (bytes[0] >= 0xC2u && bytes[0] <= 0xDFu) {
		result = bytes[0] & 0x1Fu;
		count = 2u;
	} else if (bytes[0] >= 0xE0u && bytes[0] <= 0xEFu) {
		result = bytes[0] & 0x0Fu;
		count = 3u;
	} else if (bytes[0] >= 0xF0u && bytes[0] <= 0xF4u) {
		result = bytes[0] & 0x07u;
		count = 4u;
	} else {
		return 0u;
	}
	if (count > length || (count == 3u && bytes[0] == 0xE0u && bytes[1] < 0xA0u) ||
	    (count == 3u && bytes[0] == 0xEDu && bytes[1] >= 0xA0u) ||
	    (count == 4u && bytes[0] == 0xF0u && bytes[1] < 0x90u) ||
	    (count == 4u && bytes[0] == 0xF4u && bytes[1] >= 0x90u)) {
		return 0u;
	}
	for (index = 1u; index < count; ++index) {
		if ((bytes[index] & 0xC0u) != 0x80u) {
			return 0u;
		}
		result = (result << 6u) | (bytes[index] & 0x3Fu);
	}
	*codepoint = result;
	return count;
}

static int is_xml_character(uint32_t cp)
{
	return cp == 0x9u || cp == 0xAu || cp == 0xDu || (cp >= 0x20u && cp <= 0xD7FFu) ||
	       (cp >= 0xE000u && cp <= 0xFFFDu) || (cp >= 0x10000u && cp <= 0x10FFFFu);
}

int mxsd_xml_name_start(uint32_t cp)
{
	return cp == ':' || cp == '_' || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
	       (cp >= 0xC0u && cp <= 0xD6u) || (cp >= 0xD8u && cp <= 0xF6u) ||
	       (cp >= 0xF8u && cp <= 0x2FFu) || (cp >= 0x370u && cp <= 0x37Du) ||
	       (cp >= 0x37Fu && cp <= 0x1FFFu) || (cp >= 0x200Cu && cp <= 0x200Du) ||
	       (cp >= 0x2070u && cp <= 0x218Fu) || (cp >= 0x2C00u && cp <= 0x2FEFu) ||
	       (cp >= 0x3001u && cp <= 0xD7FFu) || (cp >= 0xF900u && cp <= 0xFDCFu) ||
	       (cp >= 0xFDF0u && cp <= 0xFFFDu) || (cp >= 0x10000u && cp <= 0xEFFFFu);
}

int mxsd_xml_name_char(uint32_t cp)
{
	return mxsd_xml_name_start(cp) || cp == '-' || cp == '.' || (cp >= '0' && cp <= '9') ||
	       cp == 0xB7u || (cp >= 0x300u && cp <= 0x36Fu) || (cp >= 0x203Fu && cp <= 0x2040u);
}

/* XSD 1.0 defines the \i and \c escapes by Unicode categories rather than by
 * the edition-dependent XML NameStartChar production used by the parser. */
static int xsd_name_start(uint32_t cp)
{
	return cp == ':' || cp == '_' || cp == 0x212Eu || (cp >= 0x2180u && cp <= 0x2182u) ||
	       cp == 0x3007u || (cp >= 0x3021u && cp <= 0x3029u) ||
	       mxsd_unicode_property_matches(cp, "L", 1u);
}

static int xsd_name_char(uint32_t cp)
{
	if (cp == 0x346u) {
		/* The XSD 1.0 escape is tied to the XML 1.0 Second Edition range,
		 * which ends this combining-character interval at U+0345. */
		return 0;
	}
	return xsd_name_start(cp) || cp == '-' || cp == '.' || cp == 0xB7u || cp == 0x387u ||
	       cp == 0x6DDu || cp == 0x6DEu || mxsd_unicode_property_matches(cp, "N", 1u) ||
	       mxsd_unicode_property_matches(cp, "M", 1u) || (cp >= 0x203Fu && cp <= 0x2040u);
}

static int escaped_character(char escape, uint32_t *cp)
{
	if (escape == 'n') {
		*cp = '\n';
	} else if (escape == 'r') {
		*cp = '\r';
	} else if (escape == 't') {
		*cp = '\t';
	} else if (strchr("\\|.-^?*+{}()[]", escape) != NULL) {
		*cp = (unsigned char)escape;
	} else {
		return 0;
	}
	return 1;
}

static int escape_matches(const char *escape, size_t length, uint32_t cp)
{
	int matched;
	uint32_t literal;

	if (length < 2u || escape[0] != '\\') {
		return 0;
	}
	if (length == 2u && escaped_character(escape[1], &literal)) {
		return cp == literal;
	}
	switch (escape[1]) {
	case 's':
	case 'S':
		matched = cp == 0x20u || cp == 0x9u || cp == 0xAu || cp == 0xDu;
		break;
	case 'i':
	case 'I':
		matched = xsd_name_start(cp);
		break;
	case 'c':
	case 'C':
		matched = xsd_name_char(cp);
		break;
	case 'd':
	case 'D':
		matched = mxsd_unicode_property_matches(cp, "Nd", 2u);
		break;
	case 'w':
	case 'W':
		matched = mxsd_unicode_property_matches(cp, "L", 1u) ||
			  mxsd_unicode_property_matches(cp, "M", 1u) ||
			  mxsd_unicode_property_matches(cp, "N", 1u) ||
			  mxsd_unicode_property_matches(cp, "S", 1u);
		break;
	case 'p':
	case 'P':
		if (length < 5u || escape[2] != '{' || escape[length - 1u] != '}') {
			return 0;
		}
		matched = mxsd_unicode_property_matches(cp, escape + 3, length - 4u);
		break;
	default:
		return 0;
	}
	if (strchr("SICDWP", escape[1]) != NULL) {
		matched = !matched;
	}
	return matched && is_xml_character(cp);
}

/** Return the byte extent of a class, including nested subtraction. */
static size_t class_extent(const char *text, size_t length, size_t depth)
{
	size_t position = 1u;
	if (length < 2u || text[0] != '[' || depth > MXSD_MAX_REGEX_DEPTH) {
		return 0u;
	}
	while (position < length) {
		if (text[position] == '\\') {
			if (++position >= length) {
				return 0u;
			}
			if ((text[position] == 'p' || text[position] == 'P') &&
			    position + 1u < length && text[position + 1u] == '{') {
				const char *end =
					memchr(text + position + 2u, '}', length - position - 2u);
				if (end == NULL) {
					return 0u;
				}
				position = (size_t)(end - text) + 1u;
			} else {
				position++;
			}
		} else if (text[position] == '-' && position + 1u < length &&
			   text[position + 1u] == '[') {
			size_t nested = class_extent(text + position + 1u, length - position - 1u,
						     depth + 1u);
			if (nested == 0u) {
				return 0u;
			}
			position += nested + 1u;
		} else if (text[position] == ']') {
			return position + 1u;
		} else {
			position++;
		}
	}
	return 0u;
}

static size_t class_item(const char *text, size_t length, uint32_t cp, int *matched,
			 uint32_t *single, int *is_single)
{
	uint32_t decoded;
	size_t consumed;
	*matched = 0;
	*is_single = 0;
	if (length == 0u) {
		return 0u;
	}
	if (text[0] != '\\') {
		consumed = mxsd_utf8_decode(text, length, &decoded);
		if (consumed == 0u) {
			return 0u;
		}
		*single = decoded;
		*is_single = 1;
		*matched = decoded == cp;
		return consumed;
	}
	if (length < 2u) {
		return 0u;
	}
	if (escaped_character(text[1], &decoded)) {
		*single = decoded;
		*is_single = 1;
		*matched = decoded == cp;
		return 2u;
	}
	if (text[1] == 'p' || text[1] == 'P') {
		const char *end = memchr(text + 2u, '}', length - 2u);
		if (end == NULL || text[2] != '{') {
			return 0u;
		}
		consumed = (size_t)(end - text) + 1u;
	} else {
		consumed = 2u;
	}
	*matched = escape_matches(text, consumed, cp);
	return consumed;
}

static int class_matches(const char *text, size_t length, uint32_t cp, size_t depth)
{
	size_t position = 1u, subtraction_length = 0u;
	size_t item_count = 0u;
	const char *subtraction = NULL;
	int negate = 0, matched = 0;

	if (length < 2u || text[0] != '[' || text[length - 1u] != ']' ||
	    depth > MXSD_MAX_REGEX_DEPTH) {
		return 0;
	}
	if (position < length - 1u && text[position] == '^') {
		negate = 1;
		position++;
	}
	while (position < length - 1u) {
		uint32_t first = 0u, last = 0u;
		int item_match, first_single, ignored_match, last_single;
		size_t consumed;
		if (text[position] == '-' && position + 1u < length - 1u &&
		    text[position + 1u] == '[') {
			subtraction = text + position + 1u;
			subtraction_length =
				class_extent(subtraction, length - position - 1u, depth + 1u);
			if (subtraction_length == 0u ||
			    position + 1u + subtraction_length != length - 1u) {
				return 0;
			}
			break;
		}
		if (item_count != 0u && text[position] == '-' && position + 1u < length - 1u) {
			/* A dash between adjacent ranges is also a literal class member. */
			matched = matched || cp == '-';
			position++;
			continue;
		}
		consumed = class_item(text + position, length - 1u - position, cp, &item_match,
				      &first, &first_single);
		if (consumed == 0u) {
			return 0;
		}
		position += consumed;
		if (first_single && !(first == '-' && item_count == 0u) &&
		    position + 1u < length - 1u && text[position] == '-' &&
		    text[position + 1u] != '[') {
			position++;
			consumed = class_item(text + position, length - 1u - position, cp,
					      &ignored_match, &last, &last_single);
			if (consumed == 0u || !last_single || first > last) {
				return 0;
			}
			position += consumed;
			item_match = cp >= first && cp <= last;
		}
		matched = matched || item_match;
		item_count++;
	}
	matched = negate ? !matched : matched;
	if (subtraction != NULL && class_matches(subtraction, subtraction_length, cp, depth + 1u)) {
		matched = 0;
	}
	return matched && is_xml_character(cp);
}

static int atom_matches(const char *atom, size_t length, uint32_t cp)
{
	uint32_t literal;
	size_t consumed;
	if (length == 1u && atom[0] == '.') {
		return cp != '\n' && cp != '\r' && is_xml_character(cp);
	}
	if (length > 0u && atom[0] == '\\') {
		return escape_matches(atom, length, cp);
	}
	if (length > 0u && atom[0] == '[') {
		return class_matches(atom, length, cp, 0u);
	}
	consumed = mxsd_utf8_decode(atom, length, &literal);
	return consumed == length && literal == cp;
}

static uint16_t add_node(parser_t *parser, node_kind_t kind)
{
	node_t *node;
	if (parser->count >= MXSD_MAX_REGEX_NODES || parser->count >= UINT16_MAX) {
		parser->valid = 0;
		return INVALID_NODE;
	}
	node = &parser->nodes[parser->count];
	memset(node, 0, sizeof(*node));
	node->kind = kind;
	return (uint16_t)parser->count++;
}

static uint16_t parse_regexp(parser_t *parser);

static uint16_t parse_atom(parser_t *parser)
{
	size_t start = parser->position, extent;
	uint16_t node;
	char character;
	uint32_t ignored;

	if (start >= parser->length) {
		parser->valid = 0;
		return INVALID_NODE;
	}
	character = parser->text[start];
	if (character == '(') {
		if (++parser->depth > MXSD_MAX_REGEX_DEPTH) {
			parser->valid = 0;
			return INVALID_NODE;
		}
		parser->position++;
		node = parse_regexp(parser);
		if (parser->position >= parser->length || parser->text[parser->position] != ')') {
			parser->valid = 0;
			return INVALID_NODE;
		}
		parser->position++;
		parser->depth--;
		return node;
	}
	if (character == '[') {
		extent = class_extent(parser->text + start, parser->length - start, 0u);
		if (extent == 0u) {
			parser->valid = 0;
			return INVALID_NODE;
		}
		parser->position += extent;
	} else if (character == '\\') {
		parser->position++;
		if (parser->position >= parser->length) {
			parser->valid = 0;
			return INVALID_NODE;
		}
		if ((parser->text[parser->position] == 'p' ||
		     parser->text[parser->position] == 'P') &&
		    parser->position + 1u < parser->length &&
		    parser->text[parser->position + 1u] == '{') {
			const char *end = memchr(parser->text + parser->position + 2u, '}',
						 parser->length - parser->position - 2u);
			if (end == NULL) {
				parser->valid = 0;
				return INVALID_NODE;
			}
			parser->position = (size_t)(end - parser->text) + 1u;
		} else {
			parser->position++;
		}
	} else if (character == '.') {
		parser->position++;
	} else {
		if (strchr("?*+{}|)]", character) != NULL) {
			parser->valid = 0;
			return INVALID_NODE;
		}
		extent = mxsd_utf8_decode(parser->text + start, parser->length - start, &ignored);
		if (extent == 0u) {
			parser->valid = 0;
			return INVALID_NODE;
		}
		parser->position += extent;
	}
	node = add_node(parser, RX_ATOM);
	if (node != INVALID_NODE) {
		parser->nodes[node].offset = (uint16_t)start;
		parser->nodes[node].length = (uint16_t)(parser->position - start);
	}
	return node;
}

static uint32_t parse_count(parser_t *parser)
{
	uint32_t value = 0u;
	size_t digits = 0u;
	while (parser->position < parser->length && parser->text[parser->position] >= '0' &&
	       parser->text[parser->position] <= '9') {
		unsigned digit = (unsigned)(parser->text[parser->position++] - '0');
		if (value > (UINT32_MAX - digit) / 10u) {
			parser->valid = 0;
			return 0u;
		}
		value = value * 10u + digit;
		digits++;
	}
	if (digits == 0u) {
		parser->valid = 0;
	}
	return value;
}

static uint16_t parse_piece(parser_t *parser)
{
	uint16_t atom = parse_atom(parser), repeat;
	uint32_t minimum = 1u, maximum = 1u;
	char character;
	if (!parser->valid || parser->position >= parser->length) {
		return atom;
	}
	character = parser->text[parser->position];
	if (character == '?' || character == '*' || character == '+') {
		minimum = character == '+' ? 1u : 0u;
		maximum = character == '?' ? 1u : UINT32_MAX;
		parser->position++;
	} else if (character == '{') {
		parser->position++;
		minimum = parse_count(parser);
		maximum = minimum;
		if (parser->position < parser->length && parser->text[parser->position] == ',') {
			parser->position++;
			maximum = parser->position < parser->length &&
						  parser->text[parser->position] == '}'
					  ? UINT32_MAX
					  : parse_count(parser);
		}
		if (parser->position >= parser->length || parser->text[parser->position] != '}' ||
		    minimum > maximum) {
			parser->valid = 0;
			return INVALID_NODE;
		}
		parser->position++;
	} else {
		return atom;
	}
	repeat = add_node(parser, RX_REPEAT);
	if (repeat != INVALID_NODE) {
		parser->nodes[repeat].left = atom;
		parser->nodes[repeat].minimum = minimum;
		parser->nodes[repeat].maximum = maximum;
	}
	return repeat;
}

static uint16_t parse_branch(parser_t *parser)
{
	uint16_t branches[BRANCH_LEVELS];
	uint16_t result = INVALID_NODE;
	size_t level;

	for (level = 0u; level < BRANCH_LEVELS; ++level) {
		branches[level] = INVALID_NODE;
	}
	while (parser->position < parser->length && parser->text[parser->position] != '|' &&
	       parser->text[parser->position] != ')') {
		uint16_t piece = parse_piece(parser);
		if (!parser->valid) {
			return INVALID_NODE;
		}
		for (level = 0u; level < BRANCH_LEVELS && branches[level] != INVALID_NODE;
		     ++level) {
			uint16_t concat = add_node(parser, RX_CONCAT);
			if (concat == INVALID_NODE) {
				return concat;
			}
			parser->nodes[concat].left = branches[level];
			parser->nodes[concat].right = piece;
			branches[level] = INVALID_NODE;
			piece = concat;
		}
		if (level == BRANCH_LEVELS) {
			parser->valid = 0;
			return INVALID_NODE;
		}
		branches[level] = piece;
	}
	for (level = BRANCH_LEVELS; level-- > 0u;) {
		if (branches[level] == INVALID_NODE) {
			continue;
		}
		if (result == INVALID_NODE) {
			result = branches[level];
		} else {
			uint16_t concat = add_node(parser, RX_CONCAT);
			if (concat == INVALID_NODE) {
				return concat;
			}
			parser->nodes[concat].left = result;
			parser->nodes[concat].right = branches[level];
			result = concat;
		}
	}
	return result == INVALID_NODE ? add_node(parser, RX_EMPTY) : result;
}

static uint16_t parse_regexp(parser_t *parser)
{
	uint16_t result = parse_branch(parser);
	while (parser->valid && parser->position < parser->length &&
	       parser->text[parser->position] == '|') {
		uint16_t right, alternative;
		parser->position++;
		right = parse_branch(parser);
		/* Repeated empty branches still denote only the empty string. Collapsing
		 * them keeps every legal expression within the node bound implied by
		 * MXSD_MAX_PATTERN_LENGTH. */
		if (result != INVALID_NODE && right != INVALID_NODE &&
		    parser->nodes[result].kind == RX_EMPTY &&
		    parser->nodes[right].kind == RX_EMPTY) {
			continue;
		}
		alternative = add_node(parser, RX_ALT);
		if (alternative == INVALID_NODE) {
			return alternative;
		}
		parser->nodes[alternative].left = result;
		parser->nodes[alternative].right = right;
		result = alternative;
	}
	return result;
}

static void set_clear(position_set_t *set, size_t word_count)
{
	memset(set->words, 0, word_count * sizeof(set->words[0]));
}
static void set_add(position_set_t *set, size_t position)
{
	set->words[position / 64u] |= UINT64_C(1) << (position % 64u);
}
static int set_contains(const position_set_t *set, size_t position)
{
	return (set->words[position / 64u] & (UINT64_C(1) << (position % 64u))) != 0u;
}
static void set_union(position_set_t *destination, const position_set_t *source, size_t word_count)
{
	size_t index;
	for (index = 0u; index < word_count; ++index) {
		destination->words[index] |= source->words[index];
	}
}
static int set_equal(const position_set_t *left, const position_set_t *right, size_t word_count)
{
	return memcmp(left->words, right->words, word_count * sizeof(left->words[0])) == 0;
}
static int set_empty(const position_set_t *set, size_t word_count)
{
	size_t index;
	for (index = 0u; index < word_count; ++index) {
		if (set->words[index] != 0u) {
			return 0;
		}
	}
	return 1;
}

static void evaluate(const matcher_t *matcher, uint16_t node_index, const position_set_t *input,
		     position_set_t *output, size_t depth);

static void evaluate_repeat(const matcher_t *matcher, const node_t *node,
			    const position_set_t *input, position_set_t *output, size_t depth)
{
	position_set_t current;
	uint32_t count = 0u;
	memcpy(current.words, input->words, matcher->word_count * sizeof(current.words[0]));
	set_clear(output, matcher->word_count);
	if (node->minimum == 0u) {
		set_union(output, &current, matcher->word_count);
	}
	while (count < node->maximum && !set_empty(&current, matcher->word_count)) {
		position_set_t next;
		evaluate(matcher, node->left, &current, &next, depth + 1u);
		count++;
		if (count >= node->minimum) {
			set_union(output, &next, matcher->word_count);
		}
		if (set_equal(&current, &next, matcher->word_count)) {
			if (count < node->minimum) {
				set_union(output, &next, matcher->word_count);
			}
			break;
		}
		current = next;
	}
}

static void evaluate(const matcher_t *matcher, uint16_t node_index, const position_set_t *input,
		     position_set_t *output, size_t depth)
{
	const node_t *node;
	set_clear(output, matcher->word_count);
	if (depth > MXSD_MAX_REGEX_NODES || node_index == INVALID_NODE) {
		return;
	}
	node = &matcher->nodes[node_index];
	switch (node->kind) {
	case RX_EMPTY:
		*output = *input;
		break;
	case RX_ATOM: {
		size_t position;
		for (position = 0u; position < matcher->value.length; ++position) {
			uint32_t cp;
			size_t consumed;
			if (!set_contains(input, position)) {
				continue;
			}
			consumed = mxsd_utf8_decode(matcher->value.data + position,
						    matcher->value.length - position, &cp);
			if (consumed != 0u &&
			    atom_matches(matcher->pattern + node->offset, node->length, cp)) {
				set_add(output, position + consumed);
			}
		}
		break;
	}
	case RX_CONCAT: {
		position_set_t intermediate;
		evaluate(matcher, node->left, input, &intermediate, depth + 1u);
		evaluate(matcher, node->right, &intermediate, output, depth + 1u);
		break;
	}
	case RX_ALT: {
		position_set_t alternative;
		evaluate(matcher, node->left, input, output, depth + 1u);
		evaluate(matcher, node->right, input, &alternative, depth + 1u);
		set_union(output, &alternative, matcher->word_count);
		break;
	}
	case RX_REPEAT:
		evaluate_repeat(matcher, node, input, output, depth);
		break;
	}
}

int mxsd_pattern_matches(const char *pattern, text_span_t value)
{
	parser_t parser;
	matcher_t matcher;
	position_set_t input, output;
	uint16_t root;

	if (pattern == NULL || value.length > MXSD_MAX_TEXT_LENGTH) {
		return 0;
	}
	parser.text = pattern;
	parser.length = strlen(pattern);
	parser.position = 0u;
	parser.depth = 0u;
	parser.count = 0u;
	parser.valid = parser.length <= MXSD_MAX_PATTERN_LENGTH;
	root = parse_regexp(&parser);
	if (!parser.valid || parser.position != parser.length || root == INVALID_NODE) {
		return 0;
	}
	matcher.pattern = pattern;
	matcher.nodes = parser.nodes;
	matcher.value = value;
	matcher.word_count = (value.length + 64u) / 64u;
	set_clear(&input, matcher.word_count);
	set_add(&input, 0u);
	evaluate(&matcher, root, &input, &output, 0u);
	return set_contains(&output, value.length);
}
