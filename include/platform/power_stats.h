#pragma once

#include "common/power_profile.h"

#include <cstdint>

#ifndef TRACK_NODE_POWER_STATS
#define TRACK_NODE_POWER_STATS 0
#endif

struct power_report
{
	power_profile::window window{};
	std::uint32_t core_clock_hz = 0;
	bool tickless = false;
	bool debug_clocks_in_low_power = false;
};

namespace power_stats
{
inline constexpr bool enabled = TRACK_NODE_POWER_STATS;

void start();
void take_report(power_report &out);
const char *exception_name(std::uint16_t exception);
}
