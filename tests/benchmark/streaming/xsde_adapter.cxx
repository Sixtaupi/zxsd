/*
 * Copyright (c) 2026 mxsd contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file @brief XSD/e C++/Parser callbacks materializing the same native sample values. */

#include "bench.h"
#include "benchmark-pskel.hxx"

#include <limits.h>
#include <new>

#if defined(XSDE_STL) || defined(XSDE_EXCEPTIONS) || defined(XSDE_IOSTREAM)
#error "Use the documented embedded XSD/e runtime configuration"
#endif
#if !defined(XSDE_PARSER_VALIDATION) || !defined(XSDE_REGEXP)
#error "Parser validation and regular expression facets must be enabled"
#endif
#if UINT_MAX != 4294967295U || INT_MAX != 2147483647
#error "The benchmark requires XSD/e native int/unsigned int to be 32-bit"
#endif

#ifdef __ZEPHYR__
/* Zephyr's minimal C++ runtime only provides the unsized forms. */
void operator delete(void *pointer, size_t) throw()
{
	::operator delete(pointer);
}

void operator delete[](void *pointer, size_t) throw()
{
	::operator delete[](pointer);
}
#endif

/** Retain the decoded, facet-validated string until its enclosing sample closes. */
class label_reader: public benchmark::Label_pskel
{
      public:
	label_reader() : benchmark::Label_pskel(&base_), value_(NULL)
	{
	}
	virtual void post_Label()
	{
		value_ = post_string();
	}
	~label_reader()
	{
		delete[] value_;
	}
	void release()
	{
		delete[] value_;
		value_ = NULL;
	}
	const char *value() const
	{
		return value_;
	}

      private:
	xml_schema::string_pimpl base_;
	char *value_;
};

/** One native record, reused for every occurrence, with the common checksum sink. */
class sample_reader: public benchmark::Sample_pskel
{
      public:
	sample_reader(label_reader &label, bench_observer_t &observer)
		: label_(label), observer_(observer)
	{
	}
	virtual void id(unsigned int value)
	{
		id_ = value;
	}
	virtual void enabled(bool value)
	{
		enabled_ = value;
	}
	virtual void reading(int value)
	{
		reading_ = value;
	}
	virtual void post_Sample()
	{
		bench_consume(&observer_, id_, label_.value(), enabled_, reading_);
		label_.release();
	}

      private:
	label_reader &label_;
	bench_observer_t &observer_;
	uint32_t id_;
	bool enabled_;
	int32_t reading_;
};

extern "C" int bench_parse(const unsigned char *xml, size_t length, size_t block_size,
			   bench_observer_t *observer)
{
	benchmark::samples_pskel samples;
	label_reader label;
	sample_reader sample(label, *observer);
	xml_schema::unsigned_int_pimpl unsigned_int;
	xml_schema::boolean_pimpl boolean;
	xml_schema::int_pimpl integer;

	if (block_size == 0u) {
		return -1;
	}
	samples.parsers(sample);
	sample.parsers(unsigned_int, label, boolean, integer);
	xml_schema::document_pimpl document(samples, "urn:mxsd:benchmark", "samples");
	if (document._error()) {
		return -1;
	}
	samples.pre();
	if (samples._error()) {
		return -1;
	}
	for (size_t offset = 0u; offset < length;) {
		size_t count = length - offset;

		if (count > block_size) {
			count = block_size;
		}
		observer->supplied = offset + count;
		document.parse(xml + offset, count, offset + count == length);
		if (document._error()) {
			return -1;
		}
		offset += count;
	}
	if (length == 0u) {
		document.parse("", 0u, true);
	}
	if (document._error()) {
		return -1;
	}
	samples.post_samples();
	return samples._error() ? -1 : 0;
}

extern "C" size_t bench_static_context_bytes(void)
{
	/* These objects live on the measured worker stack; Expat uses the malloc arena. */
	return 0u;
}
