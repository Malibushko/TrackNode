#include "application/log_record.h"

#include <cassert>
#include <limits>
#include <string_view>

int main()
{
	sensor_sample sample{};
	sample.timestamp = 1234;
	sample.sequence = 7;
	sample.temperature_c = 23.5f;
	sample.pressure_pa = 100000.25f;
	sample.valid = true;

	auto record = format_sample_csv(sample);
	assert(record);
	assert(std::string_view(record->text.data(), record->length) ==
	       "1234,23.50,100000.25\r\n");
	assert(std::string_view(sample_csv_header) ==
	       "timestamp_ms,temperature_c,pressure_pa\r\n");

	sample.valid = false;
	assert(!format_sample_csv(sample));
	sample.valid = true;
	sample.temperature_c = std::numeric_limits<float>::max();
	sample.pressure_pa = std::numeric_limits<float>::max();
	assert(!format_sample_csv(sample));
}
