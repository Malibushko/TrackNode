#pragma once

#include "common/ring_buffer.h"
#include "core/hardware_object.h"
#include "common/ring_buffer.h"
#include "drivers/gpio.h"
#include "libs/freertos/freertos_helpers.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

struct uart_config
{
	USART_TypeDef *instance = nullptr;
	pin tx{};
	pin rx{};
	uint32_t alternate;
	uint32_t baud_rate;
	uint32_t mode;
	uint32_t irq_priority = 5;
	size_t rx_buffer_size = 128;
};

class uart : public hardware_object
{

public:
	explicit uart(uart_config config);

	status init();
	status write(const uint8_t *data, size_t size, uint32_t timeout_ms = 100);

	result<size_t> read(uint8_t *data, size_t size, uint32_t timeout_ms = portMAX_DELAY);

	[[nodiscard]] size_t available() const;
	[[nodiscard]] uint32_t dropped_count() const;

	[[nodiscard]] UART_HandleTypeDef &handle()
	{
		return handle_;
	}

private:
	static constexpr size_t rx_buffer_capacity = 128;

	void on_rx_complete(BaseType_t *higher_priority_woken);
	void on_error();
	void request_next_byte();
	status enable_irq();

	uart_config config_;
	UART_HandleTypeDef handle_{};
	bool initialized_ = false;

	uint8_t rx_byte_ = 0;
	std::atomic<uint32_t> dropped_count_{ 0 };
	StreamBufferHandle_t rx_stream_buf_{};

	friend void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
	friend void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);
};