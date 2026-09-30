#pragma once

#include "infrastructure/service_status.h"

struct progress_observation
{
	service_health sensor_health;
#ifndef NO_STORAGE
	service_health storage_health;
#endif
	TickType_t sensor_tick;
#ifndef NO_STORAGE
	TickType_t storage_tick;
#endif
};

inline bool should_refresh_watchdog(TickType_t now, TickType_t started,
                                    const progress_observation &observed)
{
	constexpr TickType_t progress_timeout = pdMS_TO_TICKS(10000);
#ifndef NO_STORAGE
	constexpr TickType_t storage_start_timeout = pdMS_TO_TICKS(15000);
#endif
	const TickType_t sensor_reference = observed.sensor_tick == 0 ? started : observed.sensor_tick;

	const bool sensor_live = observed.sensor_health != service_health::failed &&
	                         (observed.sensor_health == service_health::starting
	                              ? now - started < progress_timeout
	                              : now - sensor_reference <= progress_timeout);
#ifndef NO_STORAGE
	const bool storage_live = observed.storage_health != service_health::failed &&
	                          (observed.storage_health == service_health::starting
	                               ? now - started < storage_start_timeout
	                               : now - observed.storage_tick <= progress_timeout);
	return sensor_live && storage_live;
#else
	return sensor_live;
#endif
}
