#include "drivers/uart.h"

status uart::init()
{
	if (initialized_)
	{
		return fail(component::uart, error_code::already_initialized);
	}

	if (!config_.baud_rate || !IS_UART_MODE(config_.mode) || config_.alternate > 15)
	{
		return fail(component::uart, error_code::invalid_config);
	}

	if (config_.instance == USART1)
	{
		__HAL_RCC_USART1_CLK_ENABLE();
	}
	else if (config_.instance == USART2)
	{
		__HAL_RCC_USART2_CLK_ENABLE();
	}
	else if (config_.instance == USART6)
	{
		__HAL_RCC_USART6_CLK_ENABLE();
	}
	else
	{
		return fail(component::uart, error_code::invalid_config);
	}

	auto setup_pin = [this](pin pin) -> status
	{
		gpio_config pin_config{};
		pin_config.pin = pin;
		pin_config.mode = GPIO_MODE_AF_PP;
		pin_config.speed = GPIO_SPEED_FREQ_HIGH;
		pin_config.alternate = config_.alternate;

		gpio configured_pin{ pin_config };
		return configured_pin.init();
	};

	if (config_.mode & UART_MODE_TX)
	{
		auto operation_status = setup_pin(config_.tx);
		if (!operation_status)
		{
			return operation_status;
		}
	}

	if (config_.mode & UART_MODE_RX)
	{
		auto operation_status = setup_pin(config_.rx);
		if (!operation_status)
		{
			return operation_status;
		}
	}

	handle_.Instance = config_.instance;
	handle_.Init.BaudRate = config_.baud_rate;
	handle_.Init.WordLength = UART_WORDLENGTH_8B;
	handle_.Init.StopBits = UART_STOPBITS_1;
	handle_.Init.Parity = UART_PARITY_NONE;
	handle_.Init.Mode = config_.mode;
	handle_.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	handle_.Init.OverSampling = UART_OVERSAMPLING_16;

	auto operation_status = from_hal(component::uart, HAL_UART_Init(&handle_));
	initialized_ = static_cast<bool>(operation_status);

	return operation_status;
}

status uart::write(const uint8_t *data, size_t size, uint32_t timeout_ms)
{
	if (!initialized_)
	{
		return fail(component::uart, error_code::not_initialized);
	}

	if (!(config_.mode & UART_MODE_TX) || size > UINT16_MAX || (!data && size))
	{
		return fail(component::uart, error_code::invalid_config);
	}

	if (!size)
	{
		return {};
	}

	return from_hal(component::uart,
	                HAL_UART_Transmit(&handle_, const_cast<uint8_t *>(data), static_cast<uint16_t>(size), timeout_ms));
}

status uart::read(uint8_t *data, size_t size, uint32_t timeout_ms)
{
	if (!initialized_)
	{
		return fail(component::uart, error_code::not_initialized);
	}

	if (!(config_.mode & UART_MODE_RX) || size > UINT16_MAX || (!data && size))
	{
		return fail(component::uart, error_code::invalid_config);
	}

	if (!size)
	{
		return {};
	}

	return from_hal(component::uart, HAL_UART_Receive(&handle_, data, static_cast<uint16_t>(size), timeout_ms));
}
