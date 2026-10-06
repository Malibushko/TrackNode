#pragma once

#include "application/telemetry_sink.h"
#include "application/radio_publish_policy.h"

class radio_telemetry_filter final : public telemetry_sink
{
public:
	explicit radio_telemetry_filter(telemetry_sink &sink) : sink_(sink) {}

	status submit(const telemetry_sample &sample) override
	{
		const auto now = static_cast<std::uint32_t>(pdTICKS_TO_MS(sample.recorded_at));
		if (mode_ == power_mode::low &&
			!publish_.should_publish(sample.sensor.temperature_c, sample.sensor.pressure_pa, now))
			return {};

		auto accepted = sink_.submit(sample);
		if (accepted)
			publish_.on_published(sample.sensor.temperature_c, sample.sensor.pressure_pa, now);
		return accepted;
	}

	void set_power_mode(power_mode mode) override
	{
		mode_ = mode;
		sink_.set_power_mode(mode);
	}

private:
	telemetry_sink &sink_;
	radio_publish_policy publish_;
	power_mode mode_ = power_mode::high;
};
