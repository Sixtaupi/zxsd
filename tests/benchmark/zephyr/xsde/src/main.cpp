/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "benchmark-pimpl.hxx"
#include "benchmark-sskel.hxx"

#include <xsde/cxx/parser/expat/document.hxx>
#include <xsde/cxx/serializer/genx/document.hxx>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stdio.h>
#include <new>

#define BENCHMARK_RUNS   20u
#define INPUT_BLOCK_SIZE 4096u
#define SAMPLE_COUNT     1024u

extern "C" const unsigned char benchmark_xml[];
extern "C" const size_t benchmark_xml_size;

/* XSD/e is compiled by build2 with the toolchain's default C++ dialect, which
 * can emit the C++14 sized-delete entry points. Zephyr's minimal C++ runtime
 * intentionally only supplies the unsized forms. */
void operator delete(void *pointer, size_t) throw()
{
	::operator delete(pointer);
}

void operator delete[](void *pointer, size_t) throw()
{
	::operator delete[](pointer);
}

struct sample_data {
	unsigned int id;
	char label[16];
	bool enabled;
	int reading;
};

static sample_data values[SAMPLE_COUNT];

class benchmark_label_writer: public benchmark::Label_sskel
{
      public:
	benchmark_label_writer() : benchmark::Label_sskel(&base_)
	{
	}

	void value(const char *value)
	{
		value_ = value;
	}
	virtual void pre()
	{
		base_.pre(value_);
	}
	virtual void post()
	{
	}

      private:
	xml_schema::string_simpl base_;
	const char *value_;
};

class benchmark_sample_writer: public benchmark::Sample_sskel
{
      public:
	explicit benchmark_sample_writer(benchmark_label_writer &label) : label_(label)
	{
	}

	void value(const sample_data *value)
	{
		value_ = value;
	}
	virtual void pre()
	{
	}
	virtual unsigned int id()
	{
		return value_->id;
	}
	virtual void label()
	{
		label_.value(value_->label);
	}
	virtual bool enabled()
	{
		return value_->enabled;
	}
	virtual int reading()
	{
		return value_->reading;
	}
	virtual void post()
	{
	}

      private:
	benchmark_label_writer &label_;
	const sample_data *value_;
};

class benchmark_samples_writer: public benchmark::samples_sskel
{
      public:
	explicit benchmark_samples_writer(benchmark_sample_writer &sample) : sample_(sample)
	{
	}

	void values(const sample_data *items, size_t count)
	{
		values_ = items;
		count_ = count;
		index_ = 0u;
	}
	virtual void pre()
	{
	}
	virtual bool sample_next()
	{
		return index_ < count_;
	}
	virtual void sample()
	{
		sample_.value(&values_[index_++]);
	}
	virtual void post()
	{
	}

      private:
	benchmark_sample_writer &sample_;
	const sample_data *values_;
	size_t count_;
	size_t index_;
};

struct counting_sink {
	size_t bytes;
};

static bool count_output(void *context, const char *, size_t length)
{
	static_cast<counting_sink *>(context)->bytes += length;
	return true;
}

static bool flush_output(void *)
{
	return true;
}

static int validate_once()
{
	benchmark::samples_pimpl samples;
	benchmark::Sample_pimpl sample;
	xml_schema::unsigned_int_pimpl unsigned_int;
	benchmark::Label_pimpl label;
	xml_schema::boolean_pimpl boolean;
	xml_schema::int_pimpl integer;
	xml_schema::parser_error error;
	size_t offset = 0u;

	samples.parsers(sample);
	sample.parsers(unsigned_int, label, boolean, integer);
	xml_schema::document_pimpl document(samples, "urn:mxsd:benchmark", "samples");
	if ((error = document._error())) {
		return -1;
	}
	samples.pre();
	if ((error = samples._error())) {
		return -1;
	}
	while (!error && offset < benchmark_xml_size) {
		size_t length = benchmark_xml_size - offset;

		if (length > INPUT_BLOCK_SIZE) {
			length = INPUT_BLOCK_SIZE;
		}
		offset += length;
		document.parse(benchmark_xml + offset - length, length,
			       offset == benchmark_xml_size);
		error = document._error();
	}
	if (error) {
		return -1;
	}
	samples.post_samples();
	return samples._error() ? -1 : 0;
}

static int serialize_once(size_t *bytes)
{
	benchmark_label_writer label;
	benchmark_sample_writer sample(label);
	benchmark_samples_writer samples(sample);
	xml_schema::unsigned_int_simpl unsigned_int;
	xml_schema::boolean_simpl boolean;
	xml_schema::int_simpl integer;
	counting_sink sink = {0u};

	samples.serializers(sample);
	sample.serializers(unsigned_int, label, boolean, integer);
	samples.values(values, SAMPLE_COUNT);
	xml_schema::document_simpl document(samples, "urn:mxsd:benchmark", "samples");
	if (document._error()) {
		return -1;
	}
	document.add_prefix("ns1", "urn:mxsd:benchmark");
	if (document._error()) {
		return -1;
	}
	samples.pre();
	document.serialize(count_output, flush_output, &sink);
	if (document._error()) {
		return -1;
	}
	samples.post();
	if (samples._error()) {
		return -1;
	}
	*bytes = sink.bytes;
	return 0;
}

static void report(const char *name, uint64_t total_cycles)
{
	printk("%s: %llu cycles/run\n", name, total_cycles / BENCHMARK_RUNS);
}

int main()
{
	uint64_t validation_cycles = 0u;
	uint64_t serialization_cycles = 0u;
	size_t output_bytes = 0u;

	for (size_t index = 0u; index < SAMPLE_COUNT; ++index) {
		(void)snprintf(values[index].label, sizeof(values[index].label), "sensor_%05lu",
			       static_cast<unsigned long>(index));
		values[index].id = static_cast<unsigned int>(index);
		values[index].enabled = (index % 2u) != 0u;
		values[index].reading = static_cast<int>(index % 2001u) - 1000;
	}

	if (validate_once() != 0 || serialize_once(&output_bytes) != 0) {
		printk("benchmark warm-up failed\n");
		return 1;
	}
	for (size_t index = 0u; index < BENCHMARK_RUNS; ++index) {
		uint64_t started = k_cycle_get_64();

		if (validate_once() != 0) {
			printk("validation failed\n");
			return 1;
		}
		validation_cycles += k_cycle_get_64() - started;
		started = k_cycle_get_64();
		if (serialize_once(&output_bytes) != 0) {
			printk("serialization failed\n");
			return 1;
		}
		serialization_cycles += k_cycle_get_64() - started;
	}

	printk("XSD/e qemu_cortex_a53, input %zu, output %zu\n", benchmark_xml_size, output_bytes);
	report("validation", validation_cycles);
	report("serialization", serialization_cycles);
	return 0;
}
