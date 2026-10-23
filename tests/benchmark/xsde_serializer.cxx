/*
 * Copyright (c) 2026 mxsd contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "benchmark-sskel.hxx"

#include <xsde/cxx/serializer/genx/document.hxx>

#include <cstdio>
#include <iostream>

#define SAMPLE_COUNT 1024u

struct sample_data {
	unsigned int id;
	char label[16];
	bool enabled;
	int reading;
};

class benchmark_label_writer : public benchmark::Label_sskel {
public:
	benchmark_label_writer() : benchmark::Label_sskel(&base_) {}

	void value(const char *value) { value_ = value; }
	void pre() override { base_.pre(value_); }
	void post() override {}

private:
	xml_schema::string_simpl base_;
	const char *value_ = "";
};

class benchmark_sample_writer : public benchmark::Sample_sskel {
public:
	explicit benchmark_sample_writer(benchmark_label_writer &label) : label_(label) {}

	void value(const sample_data *value) { value_ = value; }
	void pre() override {}
	unsigned int id() override { return value_->id; }
	void label() override { label_.value(value_->label); }
	bool enabled() override { return value_->enabled; }
	int reading() override { return value_->reading; }
	void post() override {}

private:
	benchmark_label_writer &label_;
	const sample_data *value_ = nullptr;
};

class benchmark_samples_writer : public benchmark::samples_sskel {
public:
	explicit benchmark_samples_writer(benchmark_sample_writer &sample) : sample_(sample) {}

	void values(const sample_data *values, size_t count)
	{
		values_ = values;
		count_ = count;
		index_ = 0u;
	}
	void pre() override {}
	bool sample_next() override { return index_ < count_; }
	void sample() override { sample_.value(&values_[index_++]); }
	void post() override {}

private:
	benchmark_sample_writer &sample_;
	const sample_data *values_ = nullptr;
	size_t count_ = 0u;
	size_t index_ = 0u;
};

class counting_writer : public xml_schema::writer {
public:
	void write(const char *, size_t length) override { bytes_ += length; }
	void flush() override {}
	size_t bytes() const { return bytes_; }

private:
	size_t bytes_ = 0u;
};

int main()
{
	static sample_data values[SAMPLE_COUNT];
	benchmark_label_writer label;
	benchmark_sample_writer sample(label);
	benchmark_samples_writer samples(sample);
	xml_schema::unsigned_int_simpl unsigned_int;
	xml_schema::boolean_simpl boolean;
	xml_schema::int_simpl integer;
	counting_writer writer;
	size_t index;

	for (index = 0u; index < SAMPLE_COUNT; ++index) {
		values[index].id = static_cast<unsigned int>(index);
		std::snprintf(values[index].label, sizeof(values[index].label), "sensor_%05zu", index);
		values[index].enabled = (index % 2u) != 0u;
		values[index].reading = static_cast<int>(index % 2001u) - 1000;
	}

	samples.serializers(sample);
	sample.serializers(unsigned_int, label, boolean, integer);
	samples.values(values, SAMPLE_COUNT);
	xml_schema::document_simpl document(samples, "urn:mxsd:benchmark", "samples");
	document.add_prefix("ns1", "urn:mxsd:benchmark");
	try {
		samples.pre();
		document.serialize(writer);
		samples.post();
	} catch (const xml_schema::serializer_exception &error) {
		std::cerr << "serialization failed: " << error.text() << '\n';
		return 1;
	}
	std::cout << writer.bytes() << '\n';
	return 0;
}
