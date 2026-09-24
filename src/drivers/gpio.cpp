#include "drivers/gpio.h"

status gpio::init()
{
	if (initialized_)
	{
		return fail(component::gpio, error_code::already_initialized);
	}

	const auto pin = config_.pin;
	if (!pin.port || !pin.mask || (pin.mask & (pin.mask - 1u)) || !IS_GPIO_MODE(config_.mode) ||
	    !IS_GPIO_PULL(config_.pull) || !IS_GPIO_SPEED(config_.speed) || config_.alternate > 15u)
	{
		return fail(component::gpio, error_code::invalid_config);
	}

	if (config_.mode != GPIO_MODE_INPUT && config_.mode != GPIO_MODE_ANALOG && config_.mode != GPIO_MODE_OUTPUT_PP &&
	    config_.mode != GPIO_MODE_OUTPUT_OD && config_.mode != GPIO_MODE_AF_PP && config_.mode != GPIO_MODE_AF_OD)
	{
		return fail(component::gpio, error_code::invalid_config);
	}

	if (pin.port == GPIOA)
	{
		__HAL_RCC_GPIOA_CLK_ENABLE();
	}
	else if (pin.port == GPIOB)
	{
		__HAL_RCC_GPIOB_CLK_ENABLE();
	}
	else if (pin.port == GPIOC)
	{
		__HAL_RCC_GPIOC_CLK_ENABLE();
	}
	else if (pin.port == GPIOD)
	{
		__HAL_RCC_GPIOD_CLK_ENABLE();
	}
	else if (pin.port == GPIOE)
	{
		__HAL_RCC_GPIOE_CLK_ENABLE();
	}
	else if (pin.port == GPIOH)
	{
		__HAL_RCC_GPIOH_CLK_ENABLE();
	}
	else
	{
		return fail(component::gpio, error_code::invalid_config);
	}

	if (config_.mode == GPIO_MODE_OUTPUT_PP || config_.mode == GPIO_MODE_OUTPUT_OD)
	{
		HAL_GPIO_WritePin(pin.port, pin.mask, config_.initial_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
	}

	GPIO_InitTypeDef hal_config{};
	hal_config.Pin = pin.mask;
	hal_config.Mode = config_.mode;
	hal_config.Pull = config_.pull;
	hal_config.Speed = config_.speed;
	hal_config.Alternate = config_.alternate;

	HAL_GPIO_Init(pin.port, &hal_config);
	initialized_ = true;

	return {};
}

//////////////////////////////////////////////////////////////////////////

result<bool> gpio::read() const
{
	if (!initialized_)
	{
		return fail(component::gpio, error_code::not_initialized);
	}

	if (config_.mode == GPIO_MODE_ANALOG)
	{
		return fail(component::gpio, error_code::invalid_config);
	}

	return HAL_GPIO_ReadPin(config_.pin.port, config_.pin.mask) == GPIO_PIN_SET;
}

//////////////////////////////////////////////////////////////////////////

status gpio::write(state signal)
{
	if (!initialized_)
	{
		return fail(component::gpio, error_code::not_initialized);
	}

	if (config_.mode != GPIO_MODE_OUTPUT_PP && config_.mode != GPIO_MODE_OUTPUT_OD)
	{
		return fail(component::gpio, error_code::invalid_config);
	}

	if (config_.active_low)
	{
		signal = signal == state::set ? state::reset : state::set;
	}

	HAL_GPIO_WritePin(config_.pin.port, config_.pin.mask, signal == state::set ? GPIO_PIN_SET : GPIO_PIN_RESET);

	return {};
}

//////////////////////////////////////////////////////////////////////////

status gpio::toggle()
{
	if (!initialized_)
	{
		return fail(component::gpio, error_code::not_initialized);
	}

	if (config_.mode != GPIO_MODE_OUTPUT_PP && config_.mode != GPIO_MODE_OUTPUT_OD)
	{
		return fail(component::gpio, error_code::invalid_config);
	}

	HAL_GPIO_TogglePin(config_.pin.port, config_.pin.mask);

	return {};
}

//////////////////////////////////////////////////////////////////////////
