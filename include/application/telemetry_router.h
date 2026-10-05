#pragma once

#include "application/telemetry_sink.h"
#include "application/radio_publish_policy.h"
#include <array>

enum class telemetry_policy { every_sample, on_change_in_low_power };

// One producer (node_controller). Configure and seal before starting tasks.
class telemetry_router final : public telemetry_sink
{
public:
	status add(telemetry_sink &sink, telemetry_policy policy = telemetry_policy::every_sample)
	{
		if (sealed_ || count_ == entries_.size()) return fail(component::service, error_code::invalid_config);
		for (std::size_t i = 0; i < count_; ++i)
			if (entries_[i].sink == &sink) return fail(component::service, error_code::invalid_config);
		entries_[count_++] = { &sink, policy, {} };
		return {};
	}
	void seal() { sealed_ = true; }

	status submit(const telemetry_sample &sample) override
	{
		if (!sealed_) return fail(component::service, error_code::not_initialized);
		status result{};
		const auto now = static_cast<std::uint32_t>(pdTICKS_TO_MS(sample.recorded_at));
		for (std::size_t i = 0; i < count_; ++i)
		{
			auto &entry = entries_[i];
			if (mode_ == power_mode::low && entry.policy == telemetry_policy::on_change_in_low_power &&
			    !entry.publish.should_publish(sample.sensor.temperature_c, sample.sensor.pressure_pa, now)) continue;
			auto accepted = entry.sink->submit(sample);
			if (accepted) entry.publish.on_published(sample.sensor.temperature_c, sample.sensor.pressure_pa, now);
			else if (result) result = tl::make_unexpected(accepted.error());
		}
		return result;
	}

	void set_power_mode(power_mode mode) override
	{
		mode_ = mode;
		for (std::size_t i = 0; i < count_; ++i) entries_[i].sink->set_power_mode(mode);
	}

private:
	struct entry { telemetry_sink *sink = nullptr; telemetry_policy policy{}; radio_publish_policy publish; };
	std::array<entry, 4> entries_{};
	std::size_t count_ = 0;
	power_mode mode_ = power_mode::high;
	bool sealed_ = false;
};
