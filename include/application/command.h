#pragma once

#include <cstdint>
#include <type_traits>

#include "common/power_mode.h"

enum class command_type : std::uint8_t
{
	ping,
	read_temperature,
	read_pressure,
	read_gnss,
	read_log,
	set_sample_period,
	set_power_mode,
	reset_storage_prepare,
	reset_storage_confirm,
	read_voltage,
	error
};

struct command
{
	command_type type = command_type::ping;
	std::uint32_t sample_period_ms = 0;
	power_mode power_mode_ = power_mode::high;
};

static_assert(std::is_trivially_copyable_v<command>);
