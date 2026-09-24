#pragma once

#include "infrastructure/service_status.h"

struct progress_observation
{
	service_health sensor_health;
	service_health storage_health;
	TickType_t sensor_tick;
	TickType_t storage_tick;
};

inline bool should_refresh_watchdog(TickType_t now, TickType_t started,
                                    const progress_observation &observed)
{
	constexpr TickType_t progress_timeout = pdMS_TO_TICKS(10000);
	constexpr TickType_t storage_start_timeout = pdMS_TO_TICKS(15000);

	const bool sensor_live = observed.sensor_health != service_health::failed &&
	                         (observed.sensor_health == service_health::starting
	                              ? now - started < progress_timeout
	                              : now - observed.sensor_tick <= progress_timeout);
	const bool storage_live = observed.storage_health != service_health::failed &&
	                          (observed.storage_health == service_health::starting
	                               ? now - started < storage_start_timeout
	                               : now - observed.storage_tick <= progress_timeout);
	return sensor_live && storage_live;
}
