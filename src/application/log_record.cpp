#include "application/log_record.h"

#include <cstdio>

result<log_record> format_sample_csv(const sensor_sample &sample)
{
	if (!sample.valid)
	{
		return fail(component::storage, error_code::invalid_argument);
	}

	log_record record{};
	const int length = snprintf(record.text.data(), record.text.size(), "%lu,%.2f,%.2f\r\n",
	                            static_cast<unsigned long>(pdTICKS_TO_MS(sample.timestamp)),
	                            static_cast<double>(sample.temperature_c),
	                            static_cast<double>(sample.pressure_pa));
	if (length < 0 || static_cast<std::size_t>(length) >= record.text.size())
	{
		return fail(component::storage, error_code::invalid_argument);
	}
	record.length = static_cast<std::uint8_t>(length);
	return record;
}

//////////////////////////////////////////////////////////////////////////
