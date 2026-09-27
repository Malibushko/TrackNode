#pragma once

#include "application/sample.h"

#include <array>
#include <optional>

// Wire format v1: "TN", version, flags, sequence, uptime_ms, temperature_centi_c,
// pressure_pa, latitude_e6, longitude_e6, speed_cm_s, utc_unix_s, gnss_age_ms.
// All multibyte integers are little endian. Missing GNSS fields are zero.
struct radio_packet
{
	std::array<std::uint8_t, 64> bytes{};
	std::uint8_t length = 0;
};

result<radio_packet> format_radio_packet(const sensor_sample &sample, const std::optional<gnss_sample> &gnss, TickType_t recorded_at);
