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

	auto record = format_sample_csv(sample, std::nullopt, sample.timestamp);
	assert(record);
	assert(std::string_view(record->text.data(), record->length) ==
	       "1234,23.50,100000.25,,,0,,,0,,0,\r\n");
	assert(std::string_view(sample_csv_header) ==
	       "timestamp_ms,temperature_c,pressure_pa,gnss_sequence,gnss_age_ms,"
	       "position_valid,latitude_deg,longitude_deg,speed_valid,speed_mps,"
	       "time_valid,utc_unix_s\r\n");

	gnss_sample gnss{};
	gnss.sequence = 42;
	gnss.received_at = 1200;
	gnss.position = geo_position{46.4542, 30.7575};
	gnss.speed_mps = 1.25f;
	gnss.utc_time = 1234567890;
	record = format_sample_csv(sample, gnss, sample.timestamp);
	assert(record);
	assert(std::string_view(record->text.data(), record->length) ==
	       "1234,23.50,100000.25,42,34,1,46.454200,30.757500,1,1.25,1,1234567890\r\n");

	gnss.position.reset();
	gnss.speed_mps.reset();
	record = format_sample_csv(sample, gnss, sample.timestamp);
	assert(record);
	assert(std::string_view(record->text.data(), record->length) ==
	       "1234,23.50,100000.25,42,34,0,,,0,,1,1234567890\r\n");

	gnss.position = geo_position{46.4542, 30.7575};
	gnss.speed_mps = 1.25f;
	record = format_sample_csv(sample, gnss, sample.timestamp + 6000);
	assert(record);
	assert(std::string_view(record->text.data(), record->length) ==
	       "1234,23.50,100000.25,42,6034,0,,,0,,0,\r\n");
	gnss.received_at = std::numeric_limits<TickType_t>::max() - 10;
	record = format_sample_csv(sample, gnss, 20);
	assert(record);
	assert(std::string_view(record->text.data(), record->length) ==
	       "1234,23.50,100000.25,42,31,1,46.454200,30.757500,1,1.25,1,1234567890\r\n");

	sample.valid = false;
	assert(!format_sample_csv(sample, gnss, sample.timestamp));
	sample.valid = true;
	sample.temperature_c = std::numeric_limits<float>::max();
	sample.pressure_pa = std::numeric_limits<float>::max();
	assert(format_sample_csv(sample, std::nullopt, sample.timestamp));
}
