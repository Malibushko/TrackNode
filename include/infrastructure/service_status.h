#pragma once

#include "core/result.h"

#include <array>
#include <cstdint>

enum class program_state
{
	ok,
	initialization_error,
	runtime_error,
	hal_error
};

enum class service_id : std::uint8_t
{
	console,
	sensor,
	storage,
	logger,
	indicator,
	watchdog,
	count
};

enum class service_health : std::uint8_t
{
	starting,
	ready,
	degraded,
	failed
};

struct storage_snapshot
{
	service_health health = service_health::starting;
	std::array<char, 13> file_name{};
	std::uint64_t log_size = 0;
	TickType_t observed_at = 0;
	std::uint32_t dropped_samples = 0;
};
