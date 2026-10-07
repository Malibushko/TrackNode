#pragma once
#include <zephyr/kernel.h>
inline k_timepoint_t sys_timepoint_calc(k_timeout_t timeout)
{
	return simulated_time + timeout;
}
inline k_timeout_t sys_timepoint_timeout(k_timepoint_t point)
{
	return std::max<std::int64_t>(0, point - simulated_time);
}
inline bool sys_timepoint_expired(k_timepoint_t point)
{
	return simulated_time >= point;
}
