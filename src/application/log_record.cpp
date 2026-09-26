#include "application/log_record.h"

#include <charconv>
#include <cstdio>

result<log_record> format_sample_csv(const sensor_sample &sample,
                                     const std::optional<gnss_sample> &gnss,
                                     TickType_t recorded_at)
{
	if (!sample.valid)
	{
		return fail(component::storage, error_code::invalid_argument);
	}

	char sequence[21]{};
	char age[11]{};
	char latitude[24]{};
	char longitude[24]{};
	char speed[24]{};
	char utc_time[21]{};
	bool fresh = false;
	if (gnss)
	{
		const auto age_ms = pdTICKS_TO_MS(recorded_at - gnss->received_at);
		fresh = age_ms <= gnss_freshness_limit_ms;
		const auto sequence_result = std::to_chars(sequence, sequence + sizeof(sequence) - 1,
		                                           gnss->sequence);
		const auto age_result = std::to_chars(age, age + sizeof(age) - 1, age_ms);
		if (sequence_result.ec != std::errc{} || age_result.ec != std::errc{})
		{
			return fail(component::storage, error_code::invalid_argument);
		}
		*sequence_result.ptr = '\0';
		*age_result.ptr = '\0';
	}

	const bool position_valid = fresh && gnss->position.has_value();
	const bool speed_valid = fresh && gnss->speed_mps.has_value();
	const bool time_valid = fresh && gnss->utc_time.has_value();
	if (position_valid)
	{
		const int latitude_length = std::snprintf(latitude, sizeof(latitude), "%.6f", gnss->position->latitude);
		const int longitude_length = std::snprintf(longitude, sizeof(longitude), "%.6f", gnss->position->longitude);
		if (latitude_length < 0 || latitude_length >= static_cast<int>(sizeof(latitude)) ||
		    longitude_length < 0 || longitude_length >= static_cast<int>(sizeof(longitude)))
		{
			return fail(component::storage, error_code::invalid_argument);
		}
	}
	if (speed_valid)
	{
		const int speed_length = std::snprintf(speed, sizeof(speed), "%.2f",
		                                       static_cast<double>(*gnss->speed_mps));
		if (speed_length < 0 || speed_length >= static_cast<int>(sizeof(speed)))
		{
			return fail(component::storage, error_code::invalid_argument);
		}
	}
	if (time_valid)
	{
		const auto converted = std::to_chars(utc_time, utc_time + sizeof(utc_time) - 1,
		                                     static_cast<std::int64_t>(*gnss->utc_time));
		if (converted.ec != std::errc{})
		{
			return fail(component::storage, error_code::invalid_argument);
		}
		*converted.ptr = '\0';
	}

	log_record record{};
	const int length = std::snprintf(record.text.data(), record.text.size(),
	                            "%lu,%.2f,%.2f,%s,%s,%u,%s,%s,%u,%s,%u,%s\r\n",
	                            static_cast<unsigned long>(pdTICKS_TO_MS(sample.timestamp)),
	                            static_cast<double>(sample.temperature_c),
	                            static_cast<double>(sample.pressure_pa), sequence, age,
	                            static_cast<unsigned>(position_valid), latitude, longitude,
	                            static_cast<unsigned>(speed_valid), speed,
	                            static_cast<unsigned>(time_valid), utc_time);
	if (length < 0 || static_cast<std::size_t>(length) >= record.text.size())
	{
		return fail(component::storage, error_code::invalid_argument);
	}
	record.length = static_cast<std::uint8_t>(length);
	return record;
}

//////////////////////////////////////////////////////////////////////////
