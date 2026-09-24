#pragma once

#include "core/result.h"

#include <cstdint>

struct sensor_sample
{
	TickType_t timestamp = 0;
	std::uint32_t sequence = 0;
	float temperature_c = 0;
	float pressure_pa = 0;
	bool valid = false;
};
