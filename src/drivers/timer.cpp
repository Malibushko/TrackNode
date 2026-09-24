#include "drivers/timer.h"

status timer::init()
{
	if (initialized_)
	{
		return fail(component::timer, error_code::already_initialized);
	}

	auto *timer_instance = config_.instance;
	if (config_.prescaler > 65535 || config_.auto_reload == UINT32_MAX ||
	    ((timer_instance != TIM2 && timer_instance != TIM5) && config_.auto_reload > 65535))
	{
		return fail(component::timer, error_code::invalid_config);
	}

	if (timer_instance == TIM2)
	{
		__HAL_RCC_TIM2_CLK_ENABLE();
	}
	else if (timer_instance == TIM3)
	{
		__HAL_RCC_TIM3_CLK_ENABLE();
	}
	else if (timer_instance == TIM4)
	{
		__HAL_RCC_TIM4_CLK_ENABLE();
	}
	else if (timer_instance == TIM5)
	{
		__HAL_RCC_TIM5_CLK_ENABLE();
	}
	else if (timer_instance == TIM9)
	{
		__HAL_RCC_TIM9_CLK_ENABLE();
	}
	else if (timer_instance == TIM10)
	{
		__HAL_RCC_TIM10_CLK_ENABLE();
	}
	else if (timer_instance == TIM11)
	{
		__HAL_RCC_TIM11_CLK_ENABLE();
	}
	else
	{
		return fail(component::timer, error_code::invalid_config);
	}

	handle_.Instance = timer_instance;
	handle_.Init.Prescaler = config_.prescaler;
	handle_.Init.CounterMode = TIM_COUNTERMODE_UP;
	handle_.Init.Period = config_.auto_reload;
	handle_.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	handle_.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

	auto operation_status = from_hal(component::timer, HAL_TIM_Base_Init(&handle_));
	initialized_ = static_cast<bool>(operation_status);

	return operation_status;
}

//////////////////////////////////////////////////////////////////////////

status timer::start()
{
	if (!initialized_)
	{
		return fail(component::timer, error_code::not_initialized);
	}

	return from_hal(component::timer, HAL_TIM_Base_Start(&handle_));
}

//////////////////////////////////////////////////////////////////////////

status timer::stop()
{
	if (!initialized_)
	{
		return fail(component::timer, error_code::not_initialized);
	}

	if (claimed_channels_)
	{
		return fail(component::timer, error_code::invalid_config);
	}

	return from_hal(component::timer, HAL_TIM_Base_Stop(&handle_));
}

//////////////////////////////////////////////////////////////////////////

result<uint32_t> timer::counter() const
{
	if (!initialized_)
	{
		return fail(component::timer, error_code::not_initialized);
	}

	return handle_.Instance->CNT;
}

//////////////////////////////////////////////////////////////////////////
