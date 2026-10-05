#pragma once

#include "application/sample.h"
#include "common/power_mode.h"
#include "core/result.h"

struct telemetry_sample
{
	sensor_sample sensor;
	std::optional<gnss_sample> gnss;
	TickType_t recorded_at;
};

class telemetry_sink
{
public:
	virtual ~telemetry_sink() = default;
	// Nonblocking acceptance into a transport queue, not delivery confirmation.
	virtual status submit(const telemetry_sample &sample) = 0;
	virtual void set_power_mode(power_mode mode) = 0;
};
