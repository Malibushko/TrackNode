#pragma once

#include "application/sample.h"

#include <array>
#include <cstdint>

inline constexpr char sample_csv_header[] = "timestamp_ms,temperature_c,pressure_pa\r\n";

struct log_record
{
	std::array<char, 64> text{};
	std::uint8_t length = 0;
};

result<log_record> format_sample_csv(const sensor_sample &sample);
