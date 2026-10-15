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
		std::uint16_t low_battery_mv = 3300;
		std::uint16_t recovery_battery_mv = 3500;
		std::span<const battery_level_point> level_profile = provisional_lipo_profile;
		std::uint32_t sample_interval_ms = 1000;
		battery_estimator::config estimator{};
	};
	struct snapshot
	{
		std::uint32_t sampled_at_ms = 0;
		std::uint32_t revision = 0;
		std::uint16_t battery_mv = 0;
		std::uint16_t supply_mv = 0;
		std::uint16_t battery_raw = 0;
		std::uint16_t battery_min_raw = 0;
		std::uint16_t battery_max_raw = 0;
		std::uint16_t vrefint_raw = 0;
		std::uint8_t level_percent_estimate = 0;
		std::uint8_t quality = 0;
		bool reading_valid = false;
		bool usb_present = false;
		bool low_battery = false;
		bool low_supply = false;
		bool update_pending = false;
		bool unstable = false;
		bool reanchored = false;
	};

public:
	explicit battery_service(config settings);
	result<bool> poll_power();
	void set_pending(bool value);
	void begin_series(std::uint32_t now_ms, bool under_load);
	status sample_step(std::uint32_t now_ms, bool under_load);
	void mark_loaded();
	[[nodiscard]] bool sampling() const;
	[[nodiscard]] bool needs_retry() const;
	[[nodiscard]] snapshot current() const;
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
	result<snapshot> read_once(std::uint32_t now_ms);
	void finish_series();

private:
	config config_;
	adc adc_{adc_config{
		.channel = ADC_CHANNEL_VREFINT,
		.sample_time = ADC_SAMPLETIME_480CYCLES
	}};
	gpio usb_power_;
	snapshot latest_{};
	std::array<snapshot, 30> series_{};
	battery_estimator estimator_;
	std::uint32_t last_sample_ms_ = 0;
	std::size_t series_count_ = 0;
	std::uint16_t calibration_ = 0;
	bool adc_initialized_ = false;
	bool initialized_ = false;
	bool sampling_ = false;
	bool series_loaded_ = false;
	bool retry_ = false;
	bool low_battery_ = false;
	bool low_supply_ = false;
	std::uint32_t low_count_ = 0;
	std::uint32_t recovery_count_ = 0;
};
