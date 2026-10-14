#pragma once

#include "drivers/adc.h"
#include "application/battery_estimator.h"
#include <optional>

class battery_service
{
public:
	struct config
	{
		adc_config battery_input{};
		pin usb_power_pin{};
		std::uint32_t upper_ohms = 10000;
		std::uint32_t lower_ohms = 10000;
		std::uint32_t sample_period_ms = 5000;
		std::uint16_t low_battery_mv = 3300;
		std::uint16_t recovery_battery_mv = 3500;
		std::span<const battery_level_point> level_profile = provisional_lipo_profile;
	};
	struct snapshot
	{
		std::uint32_t sampled_at_ms;
		std::uint16_t battery_mv;
		std::uint16_t supply_mv;
		std::uint16_t battery_raw;
		std::uint16_t battery_min_raw;
		std::uint16_t battery_max_raw;
		std::uint16_t vrefint_raw;
		std::uint8_t level_percent_estimate;
		bool usb_present;
		bool low_battery;
		bool low_supply;
	};

public:
	explicit battery_service(config settings);
	status sample_if_due(std::uint32_t now_ms);
	[[nodiscard]] result<snapshot> read() const;

private:
	struct averaged_reading
	{
		std::uint16_t mean;
		std::uint16_t trimmed_mean;
		std::uint16_t minimum;
		std::uint16_t maximum;
	};

	status init();
	result<averaged_reading> read_average(adc_config input);

private:
	config config_;
	adc adc_{adc_config{
		.channel = ADC_CHANNEL_VREFINT,
		.sample_time = ADC_SAMPLETIME_480CYCLES
	}};
	gpio usb_power_;
	std::optional<snapshot> latest_;
	battery_estimator estimator_;
	std::uint32_t last_attempt_ms_ = 0;
	std::uint16_t calibration_ = 0;
	bool attempted_ = false;
	bool adc_initialized_ = false;
	bool initialized_ = false;
	bool valid_ = false;
	bool low_battery_ = false;
	bool low_supply_ = false;
};
