/*
 * Copyright (c) 2026 Romain Pelletant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Host tests for generated typed bindings: reading, writing and limits.
 */

#include "bindings.bind.h"
#include "bindings.mxsd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                                           \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,           \
				#condition);                                                       \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

#define CHECK_CODE(result, wanted)                                                                 \
	do {                                                                                       \
		mxsd_result_t check_result_ = (result);                                            \
		if (check_result_.code != (wanted)) {                                              \
			fprintf(stderr, "%s:%d: wanted %s, got %s (path %s, offset %lu)\n",        \
				__FILE__, __LINE__, mxsd_error_string(wanted),                     \
				check_result_.message,                                             \
				check_result_.path != NULL ? check_result_.path : "-",             \
				(unsigned long)check_result_.offset);                              \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

#define CHECK_STRING(actual, expected)                                                             \
	do {                                                                                       \
		const char *check_actual_ = (actual);                                              \
		if (check_actual_ == NULL || strcmp(check_actual_, (expected)) != 0) {             \
			fprintf(stderr, "%s:%d: wanted \"%s\", got \"%s\"\n", __FILE__, __LINE__,  \
				(expected), check_actual_ != NULL ? check_actual_ : "(null)");     \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

/*
 * The inventory schema has an identity constraint, which needs validation
 * scratch memory. Use an explicit allocator so that the tests also run in
 * zero-heap builds of the library; the test program itself may use malloc().
 */
static void *test_allocate(void *context, size_t size)
{
	(void)context;
	return malloc(size);
}

static void test_deallocate(void *context, void *pointer)
{
	(void)context;
	free(pointer);
}

static const mxsd_allocator_t test_allocator = {test_allocate, test_deallocate, NULL};

/** Read an inventory document, validating with @ref test_allocator. */
static mxsd_result_t read_inventory(const char *xml, bindings_inventory_t *inventory,
				    mxsd_bind_arena_t *arena)
{
	return mxsd_bind_read_with_allocator(bindings_inventory_binding(), xml, inventory, arena,
					     &test_allocator);
}

/** Write an inventory document, validating with @ref test_allocator. */
static mxsd_result_t write_inventory(const bindings_inventory_t *inventory, char *buffer,
				     size_t capacity, size_t *length)
{
	return mxsd_bind_write_buffer_with_allocator(bindings_inventory_binding(), inventory,
						     buffer, capacity, length, &test_allocator);
}

/** A complete document exercising every binding layout. */
static const char full_document[] =
	"<?xml version='1.0'?>\n"
	"<!-- inventory -->\n"
	"<inv:inventory xmlns:inv='urn:mxsd:inventory' version='  2.1  '>\n"
	"  <inv:owner name='A &amp; B&#9;tab' int='-7'/>\n"
	"  <inv:item id='1' rank='-3' note='line&#10;break'>\n"
	"    <inv:code> AB123 </inv:code>\n"
	"    <inv:status>  on-hold </inv:status>\n"
	"    <inv:quantity>+42</inv:quantity>\n"
	"    <inv:weight inv:unit='kg'>-9223372036854775808</inv:weight>\n"
	"    <inv:tag>red</inv:tag><inv:tag>  big   one </inv:tag>\n"
	"    <inv:price>12.50</inv:price>\n"
	"    <inv:location site=' north '><inv:shelf>65535</inv:shelf><inv:site>&lt;a&amp;b&gt; "
	"&lt;x&gt;</inv:site></inv:location>\n"
	"    <inv:part ref='p1'><inv:label>root\tlabel</inv:label>\n"
	"      <inv:part ref='p2'><inv:label>child</inv:label>\n"
	"        <inv:part ref='p3'><inv:label>grandchild</inv:label></inv:part>\n"
	"      </inv:part>\n"
	"      <inv:part ref='p4'><inv:label>second</inv:label></inv:part>\n"
	"    </inv:part>\n"
	"  </inv:item>\n"
	"  <inv:item id='4294967295' kind='stock'>\n"
	"    <inv:code>ZZ999</inv:code><inv:status/><inv:quantity>-2147483648</inv:quantity>\n"
	"    <inv:free>1</inv:free><inv:location><inv:site/></inv:location>\n"
	"  </inv:item>\n"
	"  <inv:perishable id='9'><inv:code>PE001</inv:code><inv:status>retired</inv:status>"
	"<inv:quantity>0</inv:quantity><inv:free>false</inv:free>"
	"<inv:location><inv:site>cold</inv:site></inv:location>"
	"<inv:expires>2026-10-20</inv:expires></inv:perishable>\n"
	"  <inv:tags> a  b c </inv:tags>\n"
	"  <inv:limit>none</inv:limit>\n"
	"  <inv:fill>100</inv:fill>\n"
	"  <inv:serial>18446744073709551615</inv:serial>\n"
	"  <inv:offset>9223372036854775807</inv:offset>\n"
	"  <inv:delta>-128</inv:delta>\n"
	"  <inv:audited>2026-10-20T12:00:00Z</inv:audited>\n"
	"</inv:inventory>\n";

/** Canonical serialization of @ref full_document after reading it. */
static const char full_canonical[] =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<ns1:inventory xmlns:ns1=\"urn:mxsd:inventory\" version=\"2.1\">"
	"<ns1:owner name=\"A &amp; B&#9;tab\" int=\"-7\"/>"
	"<ns1:item id=\"1\" rank=\"-3\" note=\"line&#10;break\" kind=\"stock\">"
	"<ns1:code>AB123</ns1:code><ns1:status>on-hold</ns1:status>"
	"<ns1:quantity>42</ns1:quantity>"
	"<ns1:weight ns1:unit=\"kg\" precision=\"2\">-9223372036854775808</ns1:weight>"
	"<ns1:tag>red</ns1:tag><ns1:tag>big one</ns1:tag><ns1:price>12.50</ns1:price>"
	"<ns1:location site=\"north\"><ns1:site>&lt;a&amp;b&gt; &lt;x&gt;</ns1:site>"
	"<ns1:shelf>65535</ns1:shelf></ns1:location>"
	"<ns1:part ref=\"p1\"><ns1:label>root label</ns1:label>"
	"<ns1:part ref=\"p2\"><ns1:label>child</ns1:label>"
	"<ns1:part ref=\"p3\"><ns1:label>grandchild</ns1:label></ns1:part></ns1:part>"
	"<ns1:part ref=\"p4\"><ns1:label>second</ns1:label></ns1:part></ns1:part>"
	"</ns1:item>"
	"<ns1:item id=\"4294967295\" kind=\"stock\"><ns1:code>ZZ999</ns1:code>"
	"<ns1:status>active</ns1:status><ns1:quantity>-2147483648</ns1:quantity>"
	"<ns1:free>true</ns1:free><ns1:location><ns1:site/></ns1:location></ns1:item>"
	"<ns1:perishable id=\"9\" kind=\"stock\"><ns1:code>PE001</ns1:code>"
	"<ns1:status>retired</ns1:status><ns1:quantity>0</ns1:quantity>"
	"<ns1:free>false</ns1:free><ns1:location><ns1:site>cold</ns1:site></ns1:location>"
	"<ns1:expires>2026-10-20</ns1:expires></ns1:perishable>"
	"<ns1:tags>a b c</ns1:tags><ns1:limit>none</ns1:limit><ns1:fill>100</ns1:fill>"
	"<ns1:serial>18446744073709551615</ns1:serial>"
	"<ns1:offset>9223372036854775807</ns1:offset><ns1:delta>-128</ns1:delta>"
	"<ns1:audited>2026-10-20T12:00:00Z</ns1:audited></ns1:inventory>";

/** Arena storage with the alignment of any bound structure. */
static union {
	long double alignment_long_double;
	void *alignment_pointer;
	uint64_t alignment_integer;
	unsigned char bytes[16384];
} arena_storage;

/** Read @ref full_document into @p inventory using the shared arena. */
static mxsd_result_t read_full(bindings_inventory_t *inventory, mxsd_bind_arena_t *arena)
{
	mxsd_bind_arena_init(arena, arena_storage.bytes, sizeof(arena_storage.bytes));
	return read_inventory(full_document, inventory, arena);
}

/** Check every value decoded from @ref full_document. */
static void test_read_values(void)
{
	bindings_inventory_t inventory;
	mxsd_bind_arena_t arena;
	const bindings_Item_t *item;
	const bindings_Part_t *part;

	CHECK_CODE(read_full(&inventory, &arena), MXSD_OK);
	if (failures != 0) {
		return;
	}
	CHECK_STRING(inventory.version, "2.1");
	CHECK_STRING(inventory.owner.name, "A & B\ttab");
	CHECK(inventory.owner.has_int_ && inventory.owner.int_ == -7);
	CHECK(inventory.item_count == 2u);

	item = &inventory.item[0];
	CHECK(item->id == 1u);
	CHECK(item->has_rank && item->rank == -3);
	CHECK_STRING(item->note, "line\nbreak");
	CHECK(item->has_kind && item->kind == BINDINGS_ITEM_KIND_STOCK);
	CHECK_STRING(item->code, "AB123");
	CHECK(item->status == BINDINGS_STATUS_ON_HOLD);
	CHECK(item->quantity == 42);
	CHECK(item->weight != NULL && item->weight->value == INT64_MIN);
	CHECK(item->weight != NULL && strcmp(item->weight->unit, "kg") == 0);
	CHECK(item->weight != NULL && item->weight->has_precision && item->weight->precision == 2);
	CHECK(item->tag_count == 2u);
	CHECK_STRING(item->tag[0], "red");
	CHECK_STRING(item->tag[1], "big one");
	CHECK_STRING(item->price, "12.50");
	CHECK(!item->has_free);
	CHECK_STRING(item->location.site, "north");
	CHECK_STRING(item->location.site_elem, "<a&b> <x>");
	CHECK(item->location.has_shelf && item->location.shelf == 65535u);

	part = item->part;
	CHECK(part != NULL);
	if (part != NULL) {
		CHECK_STRING(part->ref, "p1");
		CHECK_STRING(part->label, "root label");
		CHECK(part->part_count == 2u);
		CHECK_STRING(part->part[0].label, "child");
		CHECK(part->part[0].part_count == 1u);
		CHECK_STRING(part->part[0].part[0].label, "grandchild");
		CHECK(part->part[0].part[0].part_count == 0u && part->part[0].part[0].part == NULL);
		CHECK_STRING(part->part[1].ref, "p4");
	}

	item = &inventory.item[1];
	CHECK(item->id == UINT32_MAX);
	CHECK(!item->has_rank && item->note == NULL && item->weight == NULL);
	CHECK(item->status == BINDINGS_STATUS_ACTIVE); /* Element default on empty content. */
	CHECK(item->quantity == INT32_MIN);
	CHECK(item->price == NULL && item->has_free && item->free);
	CHECK_STRING(item->location.site_elem, "");
	CHECK(!item->location.has_shelf && item->part == NULL && item->tag_count == 0u);

	CHECK(inventory.perishable_count == 1u);
	CHECK_STRING(inventory.perishable[0].expires, "2026-10-20");
	CHECK(inventory.perishable[0].has_free && !inventory.perishable[0].free);
	CHECK(inventory.perishable[0].has_kind); /* Fixed attribute applied when absent. */
	CHECK_STRING(inventory.tags, "a b c");
	CHECK_STRING(inventory.limit, "none");
	CHECK(inventory.has_fill && inventory.fill == 100u);
	CHECK(inventory.has_serial && inventory.serial == UINT64_MAX);
	CHECK(inventory.has_offset && inventory.offset == INT64_MAX);
	CHECK(inventory.has_delta && inventory.delta == INT8_MIN);
	CHECK_STRING(inventory.audited, "2026-10-20T12:00:00Z");
	CHECK(arena.used <= arena.capacity);
}

/** Writing a read structure yields the canonical form, which reads back identically. */
static void test_round_trip(void)
{
	static char output[4096];
	static char second[4096];
	bindings_inventory_t inventory;
	bindings_inventory_t again;
	mxsd_bind_arena_t arena;
	mxsd_bind_arena_t again_arena;
	static unsigned char again_storage[16384];
	size_t length = 0u;
	size_t second_length = 0u;

	CHECK_CODE(read_full(&inventory, &arena), MXSD_OK);
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), &length), MXSD_OK);
	CHECK(length == strlen(output));
	CHECK_STRING(output, full_canonical);

	mxsd_bind_arena_init(&again_arena, again_storage, sizeof(again_storage));
	CHECK_CODE(read_inventory(output, &again, &again_arena), MXSD_OK);
	CHECK_CODE(write_inventory(&again, second, sizeof(second), &second_length), MXSD_OK);
	CHECK(second_length == length);
	CHECK_STRING(second, output);
}

/** Bounded sink collecting output and optionally failing after a byte budget. */
typedef struct {
	char data[4096];
	size_t length;
	size_t budget;
} collector_t;

static int collect(void *context, const char *data, size_t length)
{
	collector_t *collector = (collector_t *)context;

	if (length > collector->budget - collector->length ||
	    length >= sizeof(collector->data) - collector->length) {
		return -1;
	}
	memcpy(collector->data + collector->length, data, length);
	collector->length += length;
	collector->data[collector->length] = '\0';
	return 0;
}

/** Streaming validation while writing matches buffered output and validation. */
static void test_stream_write(void)
{
	static mxsd_stream_t validator;
	static collector_t collector;
	bindings_inventory_t inventory;
	mxsd_bind_arena_t arena;

	CHECK_CODE(read_full(&inventory, &arena), MXSD_OK);
	memset(&collector, 0, sizeof(collector));
	collector.budget = sizeof(collector.data);
	CHECK_CODE(mxsd_stream_init_with_allocator(&validator, bindings_inventory_schema(),
						   &test_allocator),
		   MXSD_OK);
	CHECK_CODE(bindings_inventory_write(&inventory, collect, &collector, &validator), MXSD_OK);
	CHECK_STRING(collector.data, full_canonical);
	mxsd_stream_dispose(&validator);

	/* Without a validator, the same bytes are produced. */
	memset(&collector, 0, sizeof(collector));
	collector.budget = sizeof(collector.data);
	CHECK_CODE(bindings_inventory_write(&inventory, collect, &collector, NULL), MXSD_OK);
	CHECK_STRING(collector.data, full_canonical);

	/* A validator for another schema is rejected before writing anything. */
	memset(&collector, 0, sizeof(collector));
	collector.budget = sizeof(collector.data);
	CHECK_CODE(mxsd_stream_init_with_allocator(&validator, bindings_inventory_schema(),
						   &test_allocator),
		   MXSD_OK);
	validator.finished = 1;
	CHECK_CODE(bindings_inventory_write(&inventory, collect, &collector, &validator),
		   MXSD_ERR_ARGUMENT);
	CHECK(collector.length == 0u);
	mxsd_stream_dispose(&validator);

	/* A validator that already failed is rejected as well. */
	memset(&collector, 0, sizeof(collector));
	collector.budget = sizeof(collector.data);
	CHECK_CODE(mxsd_stream_init_with_allocator(&validator, bindings_inventory_schema(),
						   &test_allocator),
		   MXSD_OK);
	CHECK_CODE(mxsd_start_element(&validator, NULL, "wrong"), MXSD_ERR_WRONG_ROOT);
	validator.depth = 0u;
	CHECK_CODE(bindings_inventory_write(&inventory, collect, &collector, &validator),
		   MXSD_ERR_ARGUMENT);
	CHECK(collector.length == 0u);
	mxsd_stream_dispose(&validator);

	/* A sink failure stops writing with MXSD_ERR_OUTPUT. */
	memset(&collector, 0, sizeof(collector));
	collector.budget = 100u;
	CHECK_CODE(bindings_inventory_write(&inventory, collect, &collector, NULL),
		   MXSD_ERR_OUTPUT);
	CHECK(collector.length <= 100u);
}

/** Build a minimal valid inventory in caller-owned, read-only storage. */
static void minimal_inventory(bindings_inventory_t *inventory, bindings_Item_t *item)
{
	memset(inventory, 0, sizeof(*inventory));
	memset(item, 0, sizeof(*item));
	inventory->version = "1";
	inventory->owner.name = "owner";
	item->id = 7u;
	item->code = "AB001";
	item->status = BINDINGS_STATUS_ACTIVE;
	item->quantity = 1;
	item->has_free = true;
	item->free = false;
	item->location.site_elem = "here";
	inventory->item = item;
	inventory->item_count = 1u;
}

/** Writer structural checks and validation of the produced document. */
static void test_write_errors(void)
{
	static char output[2048];
	static const char *const too_many_tags[] = {"a", "b", "c", "d"};
	static const char *const null_tag[] = {"a", NULL};
	bindings_inventory_t inventory;
	bindings_Item_t item;
	bindings_Item_t items[2];
	bindings_Part_t cycle;
	size_t length;

	minimal_inventory(&inventory, &item);
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), &length), MXSD_OK);
	CHECK_STRING(output,
		     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		     "<ns1:inventory xmlns:ns1=\"urn:mxsd:inventory\" version=\"1\">"
		     "<ns1:owner name=\"owner\"/><ns1:item id=\"7\"><ns1:code>AB001</ns1:code>"
		     "<ns1:status>active</ns1:status><ns1:quantity>1</ns1:quantity>"
		     "<ns1:free>false</ns1:free><ns1:location><ns1:site>here</ns1:site>"
		     "</ns1:location></ns1:item></ns1:inventory>");

	/* Exact buffer boundary: the document and its NUL fit, one byte less does not. */
	CHECK_CODE(write_inventory(&inventory, output, length + 1u, NULL), MXSD_OK);
	CHECK_CODE(write_inventory(&inventory, output, length, NULL), MXSD_ERR_OUTPUT);
	CHECK(strlen(output) < length);

	item.code = NULL;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL),
		   MXSD_ERR_MISSING_ELEMENT);
	item.code = "AB001";

	inventory.owner.name = NULL;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL),
		   MXSD_ERR_MISSING_ATTRIBUTE);
	inventory.owner.name = "owner";

	inventory.item_count = 0u;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL), MXSD_ERR_OCCURRENCE);
	inventory.item_count = 1u;

	item.tag = too_many_tags;
	item.tag_count = 4u;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL), MXSD_ERR_OCCURRENCE);
	item.tag_count = 3u;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL), MXSD_OK);
	item.tag = null_tag;
	item.tag_count = 2u;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL),
		   MXSD_ERR_MISSING_ELEMENT);
	item.tag = NULL;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL), MXSD_ERR_ARGUMENT);
	item.tag_count = 0u;

	item.status = (bindings_Status_t)3;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL),
		   MXSD_ERR_INVALID_VALUE);
	item.status = BINDINGS_STATUS_RETIRED;

	/* Invalid UTF-8 and characters outside XML 1.0 are never written. */
	item.location.site_elem = "bad\xff";
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL),
		   MXSD_ERR_INVALID_VALUE);
	item.location.site_elem = "bell\a";
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL),
		   MXSD_ERR_INVALID_VALUE);
	item.location.site_elem = "caf\xc3\xa9 \xf0\x9f\x93\xa6";
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL), MXSD_OK);

	/* Facets are enforced by validating the produced document. */
	item.code = "lowercase";
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL),
		   MXSD_ERR_INVALID_VALUE);
	item.code = "AB001";

	/* A choice with both alternatives is rejected by validation. */
	item.price = "1.0";
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL),
		   MXSD_ERR_ELEMENT_ORDER);
	item.price = NULL;

	/* Identity constraints apply to written documents. */
	memset(items, 0, sizeof(items));
	items[0] = item;
	items[1] = item;
	inventory.item = items;
	inventory.item_count = 2u;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL),
		   MXSD_ERR_IDENTITY_CONSTRAINT);
	items[1].id = 8u;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL), MXSD_OK);
	inventory.item = &item;
	inventory.item_count = 1u;

	/* A cyclic object graph is bounded by MXSD_MAX_DEPTH instead of recursing forever. */
	memset(&cycle, 0, sizeof(cycle));
	cycle.ref = "loop";
	cycle.label = "loop";
	cycle.part = &cycle;
	cycle.part_count = 1u;
	item.part = &cycle;
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL),
		   MXSD_ERR_EXCESSIVE_NESTING);
	item.part = NULL;

	CHECK_CODE(write_inventory(NULL, output, sizeof(output), NULL), MXSD_ERR_ARGUMENT);
	CHECK_CODE(write_inventory(&inventory, NULL, sizeof(output), NULL), MXSD_ERR_ARGUMENT);
	CHECK_CODE(write_inventory(&inventory, output, 0u, NULL), MXSD_ERR_ARGUMENT);
	CHECK_CODE(bindings_inventory_write(&inventory, NULL, NULL, NULL), MXSD_ERR_ARGUMENT);
}

/** Escaping preserves every significant character through a round trip. */
static void test_escaping(void)
{
	static char output[2048];
	static unsigned char storage[2048];
	bindings_inventory_t inventory;
	bindings_inventory_t decoded;
	bindings_Item_t item;
	mxsd_bind_arena_t arena;

	minimal_inventory(&inventory, &item);
	item.note = "quote\" apos' amp& lt< gt> tab\t lf\n cr\r end";
	item.location.site_elem = "text & <markup> ]]> cr\r lf\n tab\t";
	CHECK_CODE(write_inventory(&inventory, output, sizeof(output), NULL), MXSD_OK);
	CHECK(strstr(output, "note=\"quote&quot; apos' amp&amp; lt&lt; gt&gt; tab&#9; lf&#10; "
			     "cr&#13; end\"") != NULL);
	CHECK(strstr(output, "text &amp; &lt;markup&gt; ]]&gt; cr&#13; lf\n tab\t") != NULL);
	mxsd_bind_arena_init(&arena, storage, sizeof(storage));
	CHECK_CODE(read_inventory(output, &decoded, &arena), MXSD_OK);
	CHECK_STRING(decoded.item[0].note, item.note);
	CHECK_STRING(decoded.item[0].location.site_elem, item.location.site_elem);
}

/** Reading failures leave no partial state behind. */
static void test_read_errors(void)
{
	static unsigned char storage[16384];
	bindings_inventory_t inventory;
	mxsd_bind_arena_t arena;
	size_t needed;
	size_t capacity;

	/* Invalid documents report the validation error and zero the structure. */
	mxsd_bind_arena_init(&arena, storage, sizeof(storage));
	memset(&inventory, 0xa5, sizeof(inventory));
	CHECK_CODE(read_inventory("<inv:inventory xmlns:inv='urn:mxsd:inventory'/>", &inventory,
				  &arena),
		   MXSD_ERR_MISSING_ATTRIBUTE);
	CHECK(inventory.version == NULL && inventory.item == NULL && arena.used == 0u);
	CHECK_CODE(read_inventory("<inventory/>", &inventory, &arena), MXSD_ERR_WRONG_ROOT);
	CHECK_CODE(read_inventory("<inv:inventory", &inventory, &arena), MXSD_ERR_MALFORMED_XML);

	/*
	 * Find the smallest sufficient arena by bisection: temporary numeric text can
	 * make the peak usage exceed the final usage. Then check the first rejected size.
	 */
	{
		size_t failing = 0u;
		size_t working = sizeof(storage);

		while (working - failing > 1u) {
			capacity = failing + (working - failing) / 2u;
			mxsd_bind_arena_init(&arena, storage, capacity);
			if (read_inventory(full_document, &inventory, &arena).code == MXSD_OK) {
				working = capacity;
			} else {
				failing = capacity;
			}
		}
		needed = working;
	}
	mxsd_bind_arena_init(&arena, storage, needed);
	CHECK_CODE(read_inventory(full_document, &inventory, &arena), MXSD_OK);
	CHECK(arena.used <= needed);
	mxsd_bind_arena_init(&arena, storage, needed - 1u);
	CHECK_CODE(read_inventory(full_document, &inventory, &arena), MXSD_ERR_RESOURCE_LIMIT);
	CHECK(arena.used == 0u && inventory.item == NULL && inventory.version == NULL);

	/* The arena keeps earlier allocations and only grows on success. */
	mxsd_bind_arena_init(&arena, storage, sizeof(storage));
	arena.used = 3u;
	CHECK_CODE(read_inventory(full_document, &inventory, &arena), MXSD_OK);
	CHECK(arena.used > 3u);
	CHECK(((size_t)((const unsigned char *)inventory.item - storage)) % sizeof(void *) == 0u);

	mxsd_bind_arena_init(&arena, NULL, 0u);
	CHECK_CODE(read_inventory(full_document, &inventory, &arena), MXSD_ERR_RESOURCE_LIMIT);
	CHECK_CODE(read_inventory(NULL, &inventory, &arena), MXSD_ERR_ARGUMENT);
	CHECK_CODE(read_inventory(full_document, NULL, &arena), MXSD_ERR_ARGUMENT);
	CHECK_CODE(read_inventory(full_document, &inventory, NULL), MXSD_ERR_ARGUMENT);
}

/** Valid documents using xsi:type substitution cannot be represented. */
static void test_unsupported_substitution(void)
{
	static unsigned char storage[4096];
	static const char substituted[] =
		"<inv:inventory xmlns:inv='urn:mxsd:inventory' "
		"xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' version='1'>"
		"<inv:owner name='o'/>"
		"<inv:item id='1' xsi:type='inv:PerishableItem'><inv:code>AB001</inv:code>"
		"<inv:status>active</inv:status><inv:quantity>1</inv:quantity>"
		"<inv:free>true</inv:free><inv:location><inv:site>s</inv:site></inv:location>"
		"<inv:expires>2026-01-01</inv:expires></inv:item></inv:inventory>";
	static const char redundant[] =
		"<inv:inventory xmlns:inv='urn:mxsd:inventory' "
		"xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' version='1'>"
		"<inv:owner name='o'/>"
		"<inv:item id='1' xsi:type='inv:Item'><inv:code>AB001</inv:code>"
		"<inv:status>active</inv:status><inv:quantity>1</inv:quantity>"
		"<inv:free>true</inv:free><inv:location><inv:site>s</inv:site></inv:location>"
		"</inv:item></inv:inventory>";
	bindings_inventory_t inventory;
	mxsd_bind_arena_t arena;

	CHECK_CODE(mxsd_validate_with_allocator(bindings_inventory_schema(), substituted,
						&test_allocator),
		   MXSD_OK);
	mxsd_bind_arena_init(&arena, storage, sizeof(storage));
	CHECK_CODE(read_inventory(substituted, &inventory, &arena), MXSD_ERR_UNSUPPORTED);
	CHECK(arena.used == 0u && inventory.item == NULL);
	CHECK_CODE(read_inventory(redundant, &inventory, &arena), MXSD_OK);
	CHECK(inventory.item_count == 1u && inventory.item[0].id == 1u);
}

/** A second root has its own entry points; CDATA and comments are decoded as text. */
static void test_second_root(void)
{
	static unsigned char storage[1024];
	static char output[512];
	static const char document[] =
		"<p:part xmlns:p='urn:mxsd:inventory' ref='r1'>"
		"<p:label>a<!-- comment --><![CDATA[<b&c>]]>&#x1F4E6;<?pi data?>\td</p:label>"
		"<p:part ref='r2'><p:label/></p:part></p:part>";
	bindings_Part_t part;
	mxsd_bind_arena_t arena;

	mxsd_bind_arena_init(&arena, storage, sizeof(storage));
	CHECK_CODE(bindings_part_read(document, &part, &arena), MXSD_OK);
	CHECK_STRING(part.ref, "r1");
	CHECK_STRING(part.label, "a<b&c>\xf0\x9f\x93\xa6 d"); /* normalizedString replaces tab. */
	CHECK(part.part_count == 1u);
	if (part.part_count != 1u) {
		return;
	}
	CHECK_STRING(part.part[0].label, "");
	CHECK_CODE(bindings_part_write_buffer(&part, output, sizeof(output), NULL), MXSD_OK);
	CHECK_STRING(output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			     "<ns1:part xmlns:ns1=\"urn:mxsd:inventory\" ref=\"r1\">"
			     "<ns1:label>a&lt;b&amp;c&gt;\xf0\x9f\x93\xa6 d</ns1:label>"
			     "<ns1:part ref=\"r2\"><ns1:label/></ns1:part></ns1:part>");
	/* Each root reads only its own document element. */
	CHECK_CODE(bindings_part_read(full_document, &part, &arena), MXSD_ERR_WRONG_ROOT);
}

/** Generated entry points use the library's default validation policy. */
static void test_generated_wrappers(void)
{
	static char output[4096];
	static unsigned char storage[16384];
	bindings_inventory_t inventory;
	mxsd_bind_arena_t arena;

	mxsd_bind_arena_init(&arena, storage, sizeof(storage));
#ifdef MXSD_NO_HEAP
	/* Zero-heap builds cannot validate identity constraints without an allocator. */
	CHECK_CODE(bindings_inventory_read(full_document, &inventory, &arena),
		   MXSD_ERR_RESOURCE_LIMIT);
	CHECK(arena.used == 0u);
	(void)output;
#else
	CHECK_CODE(bindings_inventory_read(full_document, &inventory, &arena), MXSD_OK);
	CHECK_CODE(bindings_inventory_write_buffer(&inventory, output, sizeof(output), NULL),
		   MXSD_OK);
	CHECK_STRING(output, full_canonical);
#endif
	/* A NULL allocator selects zero-heap validation explicitly. */
	mxsd_bind_arena_init(&arena, storage, sizeof(storage));
	CHECK_CODE(mxsd_bind_read_with_allocator(bindings_inventory_binding(), full_document,
						 &inventory, &arena, NULL),
		   MXSD_ERR_RESOURCE_LIMIT);
}

/** The binding descriptor exposes the generated schema. */
static void test_descriptor(void)
{
	const mxsd_bind_root_t *root = bindings_inventory_binding();

	CHECK(root != NULL && root->schema() == bindings_inventory_schema());
	CHECK_STRING(root->name, "inventory");
	CHECK_STRING(root->namespace_uri, "urn:mxsd:inventory");
	CHECK(root->type->size == sizeof(bindings_inventory_t));
	CHECK_STRING(mxsd_error_string(MXSD_ERR_OUTPUT), "output failure");
	CHECK_STRING(mxsd_error_string(MXSD_ERR_UNSUPPORTED), "unsupported by binding");
}

int mxsd_test_bindings(void)
{
	failures = 0;
	test_read_values();
	test_round_trip();
	test_stream_write();
	test_write_errors();
	test_escaping();
	test_read_errors();
	test_unsupported_substitution();
	test_second_root();
	test_generated_wrappers();
	test_descriptor();
	if (failures != 0) {
		fprintf(stderr, "%d binding check(s) failed\n", failures);
		return 1;
	}
	return 0;
}

#ifndef MXSD_TEST_NO_MAIN
int main(void)
{
	return mxsd_test_bindings();
}
#endif
