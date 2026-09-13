#pragma once

#include "core/hardware_object.h"
#include "drivers/gpio.h"
#include "drivers/timer.h"

struct pwm_config
{
	::pin pin{};
	uint32_t alternate{};
	uint32_t channel{};
	uint32_t polarity{};
};

class pwm : public hardware_object
{

public:
	pwm(timer &timer, pwm_config config) : timer_(timer), config_(config)
	{
	}

	status init();
	status start();
	status stop();
	status set_duty(float fraction);

private:
	timer &timer_;
	pwm_config config_;
	bool initialized_ = false;
};
