#pragma once

#include "core/result.h"

#include <array>
#include <cstdint>
#include <optional>

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
#ifndef NO_STORAGE
	storage,
#endif
	logger,
	indicator,
	watchdog,
	gnss,
	radio,
	bluetooth,
	control_button,
	count
};

enum class service_health : std::uint8_t
{
	disabled,
	starting,
	ready,
	degraded,
	failed
};

const char *service_name(service_id id);
const char *health_name(service_health health);

struct service_diagnostic
{
	service_health health;
	std::optional<error> failure;
};

#ifndef NO_STORAGE
struct storage_snapshot
{
	service_health health = service_health::starting;
	std::array<char, 13> file_name{};
	std::uint64_t log_size = 0;
	TickType_t observed_at = 0;
	std::uint32_t dropped_samples = 0;
};
#endif
