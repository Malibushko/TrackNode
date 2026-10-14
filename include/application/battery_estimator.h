#pragma once

#include "application/battery_voltage.h"
#include <algorithm>

class battery_estimator
{
public:
	struct estimate
	{
		std::uint16_t mv;
		std::uint8_t percent;
	};

	void reset()
	{
		initialized_ = false;
	}

	estimate update(std::uint16_t mv, std::uint32_t now_ms, bool usb_present,
	    std::span<const battery_level_point> curve = provisional_lipo_profile)
	{
		const bool new_measurement_regime = !initialized_ || usb_present != usb_present_ ||
		    now_ms - last_sample_ms_ >= 15000;
		last_sample_ms_ = now_ms;
		usb_present_ = usb_present;
		if (new_measurement_regime)
		{
			history_.fill(mv);
			next_ = 0;
			filtered_q4_ = static_cast<std::int32_t>(mv) * 16;
			percent_ = estimate_battery_level(mv, curve);
			initialized_ = true;
		}
		else
		{
			history_[next_] = mv;
			next_ = (next_ + 1) % history_.size();
			auto sorted = history_;
			std::sort(sorted.begin(), sorted.end());
			const auto delta = static_cast<std::int32_t>(sorted[1]) * 16 - filtered_q4_;
			filtered_q4_ += (delta + (delta >= 0 ? 2 : -2)) / 4;
		}
		const auto filtered_mv = static_cast<std::uint16_t>((filtered_q4_ + 8) / 16);
		const auto target = estimate_battery_level(filtered_mv, curve);
		const bool level_changed = target >= static_cast<int>(percent_) + 2 ||
		    target <= static_cast<int>(percent_) - 2;
		const bool reached_endpoint = target == 0 || target == 100;
		if (level_changed || reached_endpoint)
		{
			percent_ = target;
		}
		return estimate{
			.mv = filtered_mv,
			.percent = percent_
		};
	}

private:
	std::array<std::uint16_t, 3> history_{};
	std::size_t next_ = 0;
	std::int32_t filtered_q4_ = 0;
	std::uint32_t last_sample_ms_ = 0;
	std::uint8_t percent_ = 0;
	bool initialized_ = false;
	bool usb_present_ = false;
};
