#pragma once

#include "application/battery_voltage.h"
#include <algorithm>
#include <cstdlib>

class battery_estimator
{
public:
	struct config
	{
		std::uint32_t rate_period_ms = 300000;
	};

	battery_estimator() = default;
	explicit battery_estimator(config settings) : config_(settings)
	{
	}

	struct estimate
	{
		std::uint8_t percent;
		bool confirming;
		bool reanchored;
	};

	estimate update(std::uint16_t mv, std::uint32_t now_ms, bool usb_present,
	    std::span<const battery_level_point> curve = provisional_lipo_profile)
	{
		const auto target = static_cast<std::int32_t>(estimate_battery_level(mv, curve)) * 1000;
		if (!initialized_)
		{
			level_milli_ = target;
			last_ms_ = now_ms;
			usb_present_ = usb_present;
			initialized_ = true;
			return current(false, false);
		}
		if (usb_present != usb_present_)
		{
			candidate_count_ = 0;
			usb_present_ = usb_present;
		}
		const auto elapsed = std::min(now_ms - last_ms_, config_.rate_period_ms);
		last_ms_ = now_ms;
		const auto difference = target - level_milli_;
		const bool needs_confirmation = std::abs(difference) >= 3000;
		if (needs_confirmation)
		{
			if (candidate_count_ == 0 || std::abs(target - candidate_milli_) > 1500)
			{
				candidate_milli_ = target;
				candidate_count_ = 1;
			}
			else
			{
				++candidate_count_;
			}
			if (candidate_count_ >= 3)
			{
				level_milli_ = target;
				candidate_count_ = 0;
				return current(false, true);
			}
		}
		else
		{
			candidate_count_ = 0;
		}
		const auto rate = difference > 0 && usb_present ? 3000u : 300u;
		const auto budget = static_cast<std::int32_t>(static_cast<std::uint64_t>(rate) * elapsed / config_.rate_period_ms);
		level_milli_ += std::clamp(difference, -budget, budget);
		return current(needs_confirmation, false);
	}

	void interrupt_confirmation()
	{
		candidate_count_ = 0;
	}

private:
	estimate current(bool confirming, bool reanchored) const
	{
		return estimate{
			.percent = static_cast<std::uint8_t>((level_milli_ + 500) / 1000),
			.confirming = confirming,
			.reanchored = reanchored
		};
	}

private:
	config config_{};
	std::int32_t level_milli_ = 0;
	std::int32_t candidate_milli_ = 0;
	std::uint32_t last_ms_ = 0;
	std::uint8_t candidate_count_ = 0;
	bool initialized_ = false;
	bool usb_present_ = false;
};
