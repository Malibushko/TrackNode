#pragma once

#include "drivers/adc.h"
#include <optional>

// Owned and called only by node_controller; no additional RTOS task.
class board_state_service
{
public:
	struct config
	{
		std::uint32_t sample_period_ms = 5000;
		std::uint16_t low_voltage_mv = 3000;
		std::uint16_t recovery_voltage_mv = 3100;
	};
	struct snapshot
	{
		std::uint32_t sampled_at_ms;
		std::uint16_t supply_mv;
		std::uint16_t vrefint_raw;
		bool low_voltage;
	};

	board_state_service() : board_state_service(config{}) {}
	explicit board_state_service(config settings) : config_(settings) {}
	status sample_if_due(std::uint32_t now_ms);
	[[nodiscard]] result<snapshot> read() const;

private:
	status init();
	config config_;
	adc adc_{adc_config{
	    .channel = ADC_CHANNEL_VREFINT,
	    .sample_time = ADC_SAMPLETIME_480CYCLES,
	}};
	std::optional<snapshot> latest_;
	std::uint32_t last_attempt_ms_ = 0;
	std::uint16_t calibration_ = 0;
	bool attempted_ = false;
	bool initialized_ = false;
	bool valid_ = false;
	bool low_voltage_ = false;
};
