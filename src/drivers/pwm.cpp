#include "drivers/pwm.h"
#include <cmath>

status pwm::init()
{
	if (initialized_)
	{
		return fail(component::pwm, error_code::already_initialized);
	}

	if (!timer_.initialized_)
	{
		return fail(component::pwm, error_code::not_initialized);
	}

	if (timer_.config_.instance != TIM2 && timer_.config_.instance != TIM5 && timer_.config_.auto_reload == 65535)
	{
		return fail(component::pwm, error_code::invalid_config);
	}

	if ((config_.channel != TIM_CHANNEL_1 && config_.channel != TIM_CHANNEL_2 && config_.channel != TIM_CHANNEL_3 &&
	     config_.channel != TIM_CHANNEL_4) ||
	    !IS_TIM_CCX_INSTANCE(timer_.config_.instance, config_.channel) || !IS_TIM_OC_POLARITY(config_.polarity))
	{
		return fail(component::pwm, error_code::invalid_config);
	}

	const uint8_t mask = static_cast<uint8_t>(1u << (config_.channel / 4u));
	if (timer_.claimed_channels_ & mask)
	{
		return fail(component::pwm, error_code::already_initialized);
	}

	gpio_config pin_config{};
	pin_config.pin = config_.pin;
	pin_config.mode = GPIO_MODE_AF_PP;
	pin_config.alternate = config_.alternate;

	gpio configured_pin{ pin_config };

	auto operation_status = configured_pin.init();
	if (!operation_status)
	{
		return operation_status;
	}

	TIM_OC_InitTypeDef hal_config{};
	hal_config.OCMode = TIM_OCMODE_PWM1;
	hal_config.Pulse = 0;
	hal_config.OCPolarity = config_.polarity;
	hal_config.OCFastMode = TIM_OCFAST_DISABLE;
	operation_status = from_hal(component::pwm, HAL_TIM_PWM_ConfigChannel(&timer_.handle_, &hal_config, config_.channel));
	if (operation_status)
	{
		initialized_ = true;
		timer_.claimed_channels_ |= mask;
	}

	return operation_status;
}

//////////////////////////////////////////////////////////////////////////

status pwm::start()
{
	if (!initialized_)
	{
		return fail(component::pwm, error_code::not_initialized);
	}

	return from_hal(component::pwm, HAL_TIM_PWM_Start(&timer_.handle_, config_.channel));
}

//////////////////////////////////////////////////////////////////////////

status pwm::stop()
{
	if (!initialized_)
	{
		return fail(component::pwm, error_code::not_initialized);
	}

	return from_hal(component::pwm, HAL_TIM_PWM_Stop(&timer_.handle_, config_.channel));
}

//////////////////////////////////////////////////////////////////////////

status pwm::set_duty(float fraction)
{
	if (!initialized_)
	{
		return fail(component::pwm, error_code::not_initialized);
	}

	if (!std::isfinite(fraction) || fraction < 0 || fraction > 1)
	{
		return fail(component::pwm, error_code::invalid_config);
	}

	const uint32_t ticks = timer_.config_.auto_reload + 1u;

	const uint32_t compare = static_cast<uint32_t>(static_cast<double>(fraction) * ticks);
	__HAL_TIM_SET_COMPARE(&timer_.handle_, config_.channel, compare);

	return {};
}

//////////////////////////////////////////////////////////////////////////
