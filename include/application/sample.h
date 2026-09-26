#pragma once

#include "core/result.h"

#include <cstdint>
#include <ctime>
#include <optional>

inline constexpr std::uint32_t gnss_freshness_limit_ms = 5000;

struct sensor_sample
{
	TickType_t timestamp = 0;
	std::uint32_t sequence = 0;
	float temperature_c = 0;
	float pressure_pa = 0;
	bool valid = false;
};

struct geo_position
{
	double latitude = 0;
	double longitude = 0;
};

struct gnss_sample
{
	std::uint64_t sequence = 0;
	TickType_t received_at = 0;
	std::optional<geo_position> position;
	std::optional<float> speed_mps;
	std::optional<std::time_t> utc_time;
};
