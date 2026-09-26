#pragma once

#include "application/sample.h"

#include <array>
#include <cstdint>
#include <optional>

inline constexpr char sample_csv_header[] =
    "timestamp_ms,temperature_c,pressure_pa,gnss_sequence,gnss_age_ms,"
    "position_valid,latitude_deg,longitude_deg,speed_valid,speed_mps,"
    "time_valid,utc_unix_s\r\n";

struct log_record
{
	std::array<char, 160> text{};
	std::uint8_t length = 0;
};

result<log_record> format_sample_csv(const sensor_sample &sample,
                                     const std::optional<gnss_sample> &gnss,
                                     TickType_t recorded_at);
