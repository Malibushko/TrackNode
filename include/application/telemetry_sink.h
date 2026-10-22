#pragma once

#include "application/sample.h"
#include "application/sequence.h"
#include "common/power_mode.h"
#include "core/result.h"

struct telemetry_sample
{
	sensor_sample sensor;
	std::optional<gnss_sample> gnss;
	TickType_t recorded_at{};
	sequence_t id = 0;
};

class telemetry_sink
{
public:
	virtual ~telemetry_sink() = default;
	virtual status submit(const telemetry_sample &sample) = 0;
	[[nodiscard]] virtual sequence_t next_sample_id() const { return 1; }
	virtual status acknowledge(sequence_t)
	{
		return fail(component::service, error_code::not_implemented);
	}
	virtual void set_power_mode(power_mode mode) = 0;
};
