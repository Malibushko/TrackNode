#include "drivers/pwm.h"
#include <cassert>
#include <limits>
#include <cstdio>

GPIO_TypeDef gpio_a;
TIM_TypeDef tim2, tim3, tim5;
HAL_StatusTypeDef config_status = HAL_OK;
HAL_StatusTypeDef start_status = HAL_OK;

status gpio::init()
{
	return {};
}

status timer::init()
{
	handle_.Instance = config_.instance;

	initialized_ = true;

	return {};
}

HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *, TIM_OC_InitTypeDef *, uint32_t)
{
	return config_status;
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *, uint32_t)
{
	return start_status;
}

HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *, uint32_t)
{
	return HAL_OK;
}

int main()
{
	::timer timer{ { TIM2, 71, 999 } };
	::pwm pwm{ timer, {} };
	assert(!pwm.init());
	assert(!pwm.start());
	assert(!pwm.set_duty(0.5f));
	assert(timer.init());
	config_status = HAL_ERROR;
	auto failure = pwm.init();
	assert(!failure && failure.error().status == HAL_ERROR);
	config_status = HAL_OK;
	assert(pwm.init());
	assert(!pwm.init());
	assert(pwm.set_duty(0));
	assert(tim2.CCR[1] == 0);
	assert(pwm.set_duty(0.25f));
	assert(tim2.CCR[1] == 250);
	assert(pwm.set_duty(0.5f));
	assert(tim2.CCR[1] == 500);
	assert(pwm.set_duty(0.75f));
	assert(tim2.CCR[1] == 750);
	assert(pwm.set_duty(1));
	assert(tim2.CCR[1] == 1000);
	assert(!pwm.set_duty(-0.1f));
	assert(!pwm.set_duty(1.1f));
	assert(!pwm.set_duty(std::numeric_limits<float>::quiet_NaN()));
	assert(!pwm.set_duty(std::numeric_limits<float>::infinity()));
	assert(tim2.CCR[1] == 1000);
	start_status = HAL_BUSY;
	assert(pwm.start().error().status == HAL_BUSY);
	start_status = HAL_OK;
	assert(pwm.start());
	assert(pwm.stop());

	::pwm duplicate{ timer, {} };
	assert(!duplicate.init());
	pwm_config other_config;
	other_config.channel = TIM_CHANNEL_1;
	::pwm other{ timer, other_config };
	assert(other.init());
	assert(other.set_duty(0.25f));
	assert(tim2.CCR[0] == 250 && tim2.CCR[1] == 1000);

	::timer narrow{ { TIM3, 0, 65535 } };
	assert(narrow.init());
	::pwm overflow{ narrow, {} };
	assert(!overflow.init());
	::timer wide{ { TIM5, 0, UINT32_MAX - 1u } };
	assert(wide.init());
	::pwm wide_pwm{ wide, {} };
	assert(wide_pwm.init());
	assert(wide_pwm.set_duty(1));
	assert(tim5.CCR[1] == UINT32_MAX);

	bool next_called = false;
	status failed = fail(component::uart, error_code::hal_failure, HAL_TIMEOUT);
	auto chained = failed.and_then(
	    [&]() -> status
	    {
		    next_called = true;
		    return {};
	    });
	assert(!chained && !next_called && chained.error().status == HAL_TIMEOUT);
	std::puts("PASS: PWM bounds, error propagation, channel ownership, expected short-circuit");
}
