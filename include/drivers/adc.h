#pragma once

#include "core/hardware_object.h"
#include "drivers/gpio.h"

struct adc_config
{
	::pin pin{};
	uint32_t channel{};
	uint32_t sample_time{};
};

class adc : public hardware_object
{

public:
	explicit adc(adc_config config) : config_(config)
	{
	}

	status init();
	status select_channel(adc_config config);
	result<uint16_t> read_raw(uint32_t timeout_ms = 10);

private:
	adc_config config_;
	ADC_HandleTypeDef handle_{};
	bool initialized_ = false;
};
