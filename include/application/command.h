#pragma once

#include <cstdint>
#include <type_traits>

enum class command_type : std::uint8_t
{
	ping,
	read_temperature,
	read_pressure,
	read_log,
	set_sample_period
};

struct command
{
	command_type type = command_type::ping;
	std::uint32_t sample_period_ms = 0;
};

static_assert(std::is_trivially_copyable_v<command>);
