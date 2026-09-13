#pragma once

#include "core/hardware_object.h"
#include "drivers/gpio.h"
#include <cstddef>

struct uart_config
{
	USART_TypeDef *instance = nullptr;
	pin tx{};
	pin rx{};
	uint32_t alternate;
	uint32_t baud_rate;
	uint32_t mode;
};

class uart : public hardware_object
{

public:
	explicit uart(uart_config config) : config_(config)
	{
	}

	status init();
	status write(const uint8_t *data, size_t size, uint32_t timeout_ms = 100);
	status read(uint8_t *data, size_t size, uint32_t timeout_ms = 100);

private:
	uart_config config_;
	UART_HandleTypeDef handle_{};
	bool initialized_ = false;
};
