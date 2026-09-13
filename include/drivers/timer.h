#pragma once

#include "core/hardware_object.h"
#include "core/result.h"

struct timer_config
{
	TIM_TypeDef *instance = nullptr;
	uint32_t prescaler {};
	uint32_t auto_reload {};
};
class pwm;

class timer : public hardware_object
{

public:
	explicit timer(timer_config config) : config_(config)
	{
	}

	status init();
	status start();
	status stop();
	result<uint32_t> counter() const;

private:
	friend class pwm;
	timer_config config_;
	TIM_HandleTypeDef handle_{};
	bool initialized_ = false;
	uint8_t claimed_channels_ = 0;
};
