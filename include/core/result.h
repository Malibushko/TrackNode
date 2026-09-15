#pragma once

#include "platform/hal.h"
#include <tl/expected.hpp>

enum class component
{
	undefined,
	board,
	clock,
	gpio,
	uart,
	timer,
	pwm,
	adc,
	i2c_bus,
	i2c_device
};

enum class error_code
{
	invalid_config,
	not_initialized,
	already_initialized,
	uart_error,
	hal_failure
};

struct error
{
	::component component;
	error_code code;
	HAL_StatusTypeDef status = HAL_OK;
};

template <class value_type>
using result = tl::expected<value_type, error>;

using status = result<void>;

inline tl::unexpected<error> fail(component source, error_code code, HAL_StatusTypeDef status = HAL_OK)
{
	return tl::make_unexpected(error{ source, code, status });
}

inline status from_hal(component source, HAL_StatusTypeDef status)
{
	if (status != HAL_OK)
	{
		return fail(source, error_code::hal_failure, status);
	}

	return {};
}
