#pragma once

#include "core/hardware_object.h"
#include "drivers/gpio.h"
#include "libs/freertos/freertos_helpers.h"

#include <array>
#include <cstddef>
#include <atomic>
#include <cstdint>
#include <coroutine>

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
	enum class tx_state : uint8_t
	{
		idle,
		transmitting,
		completed,
		failed
	};

public:
	explicit uart(uart_config config);

	status init();
	status write(const uint8_t *data, size_t size, uint32_t timeout_ms = 100);

	class tx_awaitable
	{
	public:
		tx_awaitable(uart &owner, const uint8_t *data, size_t size)
		    : owner_(owner), data_(data), size_(size) {}

		bool await_ready() const noexcept { return size_ == 0; }
		bool await_suspend(std::coroutine_handle<>)
		{
			started_ = owner_.begin_write(data_, size_);
			return started_.has_value();
		}
		status await_resume()
		{
			if (!started_ || size_ == 0)
			{
				return started_;
			}
			return owner_.finish_write();
		}

	private:
		uart &owner_;
		const uint8_t *data_;
		size_t size_;
		status started_{};
	};

	[[nodiscard]] tx_awaitable write_async(const uint8_t *data, size_t size)
	{
		return tx_awaitable{ *this, data, size };
	}

	result<size_t> read(uint8_t *data, size_t size, uint32_t timeout_ms = portMAX_DELAY);

	[[nodiscard]] size_t available() const;
	[[nodiscard]] uint32_t dropped_count() const;

	[[nodiscard]] UART_HandleTypeDef &handle()
	{
		return handle_;
	}

	DMA_HandleTypeDef &hdma_rx() { return hdma_rx_; }
	DMA_HandleTypeDef &hdma_tx() { return hdma_tx_; }

private:
	status begin_write(const uint8_t *data, size_t size);
	status finish_write();
	static constexpr size_t rx_dma_buffer_capacity = 64;

	void on_rx_event(size_t write_pos, BaseType_t *higher_priority_woken);
	void on_tx_event(BaseType_t* higher_priority_woken);

	void on_error(BaseType_t* higher_priority_woken);

	void drain_rx(size_t write_pos, BaseType_t* higher_priority_woken);

	status error();

	status enable_irq();
	status setup_dma(bool is_rx);
	status start_rx_dma();

	uart_config config_;
	UART_HandleTypeDef handle_{};

	DMA_HandleTypeDef  hdma_rx_{};
	DMA_HandleTypeDef  hdma_tx_{};

	std::atomic<tx_state> tx_state_{};
	TaskHandle_t tx_waiter_{};

	std::array<uint8_t, rx_dma_buffer_capacity> rx_dma_buf_{};

	bool initialized_ = false;

	std::atomic<uint32_t> dropped_count_{ 0 };
	StreamBufferHandle_t rx_stream_buf_{};
	size_t last_read_pos_{};
	status last_error_{};

	friend void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);
	friend void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size);
	friend void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
};
