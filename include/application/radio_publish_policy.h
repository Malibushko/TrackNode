#pragma once

#include <cmath>
#include <cstdint>

// Compares each reading with the last one accepted into the radio queue.
class radio_publish_policy
{
public:
	static constexpr float temperature_delta_c = 0.5f;
	static constexpr float pressure_delta_pa = 1'000.0f;
	static constexpr std::uint32_t heartbeat_ms = 300'000;
	static constexpr std::uint32_t minimum_interval_ms = 1'000;

	[[nodiscard]] bool should_publish(float temperature_c, float pressure_pa,
	                                  std::uint32_t now_ms) const
	{
		if (!std::isfinite(temperature_c) || !std::isfinite(pressure_pa)) return false;
		if (!has_published_) return true;

		const auto elapsed_ms = now_ms - last_published_at_ms_;
		if (elapsed_ms < minimum_interval_ms) return false;

		return elapsed_ms >= heartbeat_ms ||
		       std::fabs(temperature_c - last_temperature_c_) >= temperature_delta_c ||
		       std::fabs(pressure_pa - last_pressure_pa_) >= pressure_delta_pa;
	}

	void on_published(float temperature_c, float pressure_pa, std::uint32_t now_ms)
	{
		last_temperature_c_ = temperature_c;
		last_pressure_pa_ = pressure_pa;
		last_published_at_ms_ = now_ms;
		has_published_ = true;
	}

private:
	float last_temperature_c_ = 0;
	float last_pressure_pa_ = 0;
	std::uint32_t last_published_at_ms_ = 0;
	bool has_published_ = false;
};
