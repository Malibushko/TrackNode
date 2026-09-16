#pragma once

#include "platform/hal.h"
#include <tl/expected.hpp>
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
}

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
	i2c_device,
	rtos
};

enum class error_code
{
	invalid_config,
	not_initialized,
	already_initialized,
	device_error,
	uart_error,
	hal_failure,
	out_of_memory,
	os_failure
};

struct error
{
	::component component;
	error_code code;

	union {
		HAL_StatusTypeDef hal_status;
		BaseType_t rtos_status;
	};

	error(::component c, error_code ec, HAL_StatusTypeDef hs = HAL_OK)
		: component(c), code(ec), hal_status(hs) {}

	error(::component c, error_code ec, BaseType_t rs)
		: component(c), code(ec), rtos_status(rs) {}
};

template <class value_type>
using result = tl::expected<value_type, error>;

using status = result<void>;

inline tl::unexpected<error> fail(component source, error_code code, HAL_StatusTypeDef status = HAL_OK)
{
	return tl::make_unexpected(error{ source, code, status });
}

inline tl::unexpected<error> fail_rtos(error_code code, BaseType_t status = pdFAIL)
{
	return tl::make_unexpected(error{ component::rtos, code, status });
}

inline status from_hal(component source, HAL_StatusTypeDef status)
{
	if (status != HAL_OK)
	{
		return fail(source, error_code::hal_failure, status);
	}

	return {};
}

inline status from_freertos(BaseType_t rtos_status)
{
	if (rtos_status == pdPASS)
	{
		return {};
	}

	if (rtos_status == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)
	{
		return fail_rtos(error_code::out_of_memory, rtos_status);
	}

	return fail_rtos(error_code::os_failure, rtos_status);
}