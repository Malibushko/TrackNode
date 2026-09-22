#include "drivers/uart.h"

#include "common/scope_guard.h"
#include "platform/stdio.h"

#include <algorithm>

namespace
{

uart *instance_usart1 = nullptr;
uart *instance_usart2 = nullptr;
uart *instance_usart6 = nullptr;

void bind_instance(USART_TypeDef *periph, uart *obj)
{
	if (periph == USART1)
	{
		instance_usart1 = obj;
	}
	else if (periph == USART2)
	{
		instance_usart2 = obj;
	}
	else if (periph == USART6)
	{
		instance_usart6 = obj;
	}
}

uart *get_instance(USART_TypeDef *periph)
{
	if (periph == USART1)
	{
		return instance_usart1;
	}
	if (periph == USART2)
	{
		return instance_usart2;
	}
	if (periph == USART6)
	{
		return instance_usart6;
	}

	return nullptr;
}

IRQn_Type irq_for(USART_TypeDef *instance)
{
	if (instance == USART1)
		return USART1_IRQn;
	if (instance == USART2)
		return USART2_IRQn;
	if (instance == USART6)
		return USART6_IRQn;

	return static_cast<IRQn_Type>(-1);
}

struct dma_line
{
	DMA_Stream_TypeDef *stream;
	uint32_t channel;
	IRQn_Type irqn;
};

dma_line rx_dma_for(USART_TypeDef *instance)
{
	if (instance == USART1)
		return { DMA2_Stream5, DMA_CHANNEL_4, DMA2_Stream5_IRQn };

	if (instance == USART2)
		return { DMA1_Stream5, DMA_CHANNEL_4, DMA1_Stream5_IRQn };

	if (instance == USART6)
		return { DMA2_Stream1, DMA_CHANNEL_5, DMA2_Stream1_IRQn };

	return { nullptr, 0, static_cast<IRQn_Type>(-1) };
}

dma_line tx_dma_for(USART_TypeDef *instance)
{
	if (instance == USART1)
		return { DMA2_Stream7, DMA_CHANNEL_4, DMA2_Stream7_IRQn };

	if (instance == USART2)
		return { DMA1_Stream6, DMA_CHANNEL_4, DMA1_Stream6_IRQn };

	if (instance == USART6)
		return { DMA2_Stream6, DMA_CHANNEL_5, DMA2_Stream6_IRQn };

	return { nullptr, 0, static_cast<IRQn_Type>(-1) };
}

void enable_dma_clock(DMA_Stream_TypeDef *stream)
{
	if (stream == DMA1_Stream0 || stream == DMA1_Stream1 || stream == DMA1_Stream2 ||
		stream == DMA1_Stream3 || stream == DMA1_Stream4 || stream == DMA1_Stream5 ||
		stream == DMA1_Stream6 || stream == DMA1_Stream7)
	{
		__HAL_RCC_DMA1_CLK_ENABLE();
	}
	else
	{
		__HAL_RCC_DMA2_CLK_ENABLE();
	}
}

//
// DMA structs
//

constexpr size_t k_max_dma_bindings = 6;
struct dma_binding { DMA_Stream_TypeDef *stream; DMA_HandleTypeDef *handle; };
dma_binding dma_bindings[k_max_dma_bindings]{};

void bind_dma(DMA_Stream_TypeDef *stream, DMA_HandleTypeDef *handle)
{
	for (auto &b : dma_bindings)
	{
		if (!b.stream || b.stream == stream)
		{
			b.stream = stream;
			b.handle = handle;
			return;
		}
	}
}

DMA_HandleTypeDef *get_dma_handle(DMA_Stream_TypeDef *stream)
{
	for (auto &b : dma_bindings)
	{
		if (b.stream == stream)
			return b.handle;
	}

	return nullptr;
}

} // anonymous namespace

uart::uart(uart_config config) : config_(config)
{
	bind_instance(config_.instance, this);
}

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
		auto operation_status = setup_pin(config_.tx)
									.and_then([&]{ return setup_dma(false); });
		if (!operation_status)
		{
			return operation_status;
		}
	}

	if (config_.mode & UART_MODE_RX)
	{
		auto operation_status = setup_pin(config_.rx)
									.and_then([&]{ return setup_dma(true);});

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
	if (!operation_status)
	{
		return operation_status;
	}

	operation_status = enable_irq();
	if (!operation_status)
	{
		return operation_status;
	}

	if ((config_.mode & UART_MODE_RX) && !rx_stream_buf_)
	{
		if (!config_.rx_buffer_size)
		{
			return fail(component::uart, error_code::invalid_config);
		}
		rx_stream_buf_ = xStreamBufferCreate(config_.rx_buffer_size, 1);
		if (!rx_stream_buf_)
		{
			return fail(component::uart, error_code::out_of_memory);
		}

		if (auto status = start_rx_dma(); !status)
		{
			return status;
		}
	}

	initialized_ = true;

	return {};
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

	ulTaskNotifyTake(pdTRUE, 0);

	tx_waiter_ = xTaskGetCurrentTaskHandle();
	tx_state_.store(tx_state::transmitting);

	common::scope_guard release_guard([this]
	{
		tx_state_.store(tx_state::idle);
		tx_waiter_ = nullptr;
	});

	if (auto status = from_hal(component::uart, HAL_UART_Transmit_DMA(&handle_, data, size)); !status)
	{
		return status;
	}

	if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)))
	{
		HAL_UART_AbortTransmit(&handle_);
		return fail(component::uart, error_code::timeout_error);
	}

	auto state = tx_state_.load();
	return state == tx_state::failed ? fail(component::uart, error_code::device_error) : status{};
}

status uart::setup_dma(bool is_rx)
{
	dma_line line = is_rx ? rx_dma_for(config_.instance) : tx_dma_for(config_.instance);
	if (!line.stream)
	{
		return fail(component::uart, error_code::invalid_config);
	}

	enable_dma_clock(line.stream);

	DMA_HandleTypeDef &hdma = is_rx ? hdma_rx_ : hdma_tx_;

	hdma.Instance                 = line.stream;
	hdma.Init.Channel             = line.channel;
	hdma.Init.Direction           = is_rx ? DMA_PERIPH_TO_MEMORY : DMA_MEMORY_TO_PERIPH;
	hdma.Init.PeriphInc           = DMA_PINC_DISABLE;
	hdma.Init.MemInc              = DMA_MINC_ENABLE;
	hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
	hdma.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
	hdma.Init.Mode                = is_rx ? DMA_CIRCULAR : DMA_NORMAL;
	hdma.Init.Priority            = DMA_PRIORITY_MEDIUM;
	hdma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

	if (HAL_DMA_Init(&hdma) != HAL_OK)
	{
		return fail(component::uart, error_code::device_error);
	}

	if (is_rx)
	{
		handle_.hdmarx = &hdma;
	}
	else
	{
		handle_.hdmatx = &hdma;
	}
	hdma.Parent = &handle_;

	bind_dma(line.stream, &hdma);

	HAL_NVIC_SetPriority(line.irqn, config_.irq_priority, 0);
	HAL_NVIC_EnableIRQ(line.irqn);

	return {};
}

status uart::start_rx_dma()
{
	last_read_pos_ = 0;
	return from_hal(
		component::uart,
		HAL_UARTEx_ReceiveToIdle_DMA(&handle_, rx_dma_buf_.data(), rx_dma_buf_.size()));
}

void uart::on_rx_event(size_t write_pos, BaseType_t *higher_priority_woken)
{
	if (write_pos > rx_dma_buf_.size())
	{
		return;
	}

	drain_rx(write_pos, higher_priority_woken);

	if (write_pos == rx_dma_buf_.size())
	{
		last_read_pos_ = 0;
	}
}

void uart::on_tx_event(BaseType_t *higher_priority_woken)
{
	if (tx_state_.load() != tx_state::transmitting)
	{
		return;
	}

	tx_state_.store(tx_state::completed);

	if (tx_waiter_)
	{
		vTaskNotifyGiveFromISR(tx_waiter_, higher_priority_woken);
	}
}

void uart::on_error(BaseType_t* higher_priority_woken)
{
	__HAL_UART_CLEAR_PEFLAG(&handle_);

	if (config_.mode & UART_MODE_RX)
	{
		if (auto status = start_rx_dma(); !status)
		{
			last_error_ = status;
		}
	}

	if (config_.mode & UART_MODE_TX)
	{
		if (tx_state_.load() == tx_state::transmitting)
		{
			tx_state_= tx_state::failed;
			if (tx_waiter_)
			{
				vTaskNotifyGiveFromISR(tx_waiter_, higher_priority_woken);
			}
		}
	}
}

void uart::drain_rx(size_t write_pos, BaseType_t *higher_priority_woken)
{
	if (write_pos == last_read_pos_)
	{
		return;
	}

	if (write_pos > last_read_pos_)
	{
		size_t len = write_pos - last_read_pos_;
		size_t sent = xStreamBufferSendFromISR(rx_stream_buf_, rx_dma_buf_.data() + last_read_pos_, len, higher_priority_woken);
		if (sent != len)
		{
			dropped_count_.fetch_add(len - sent, std::memory_order_relaxed);
		}
	}
	else
	{
		size_t first_len = rx_dma_buf_.size() - last_read_pos_;
		size_t sent1 = xStreamBufferSendFromISR(rx_stream_buf_, rx_dma_buf_.data() + last_read_pos_, first_len, higher_priority_woken);
		if (sent1 != first_len)
		{
			dropped_count_.fetch_add(first_len - sent1, std::memory_order_relaxed);
		}

		if (write_pos)
		{
			size_t sent2 = xStreamBufferSendFromISR(rx_stream_buf_, rx_dma_buf_.data(), write_pos, higher_priority_woken);
			if (sent2 != write_pos)
			{
				dropped_count_.fetch_add(write_pos - sent2, std::memory_order_relaxed);
			}
		}
	}

	last_read_pos_ = write_pos;
}

status uart::error()
{
	auto temp = last_error_;
	last_error_ = {};
	return temp;
}

status uart::enable_irq()
{
	IRQn_Type irq = irq_for(config_.instance);
	if (irq == static_cast<IRQn_Type>(-1))
	{
		return fail(component::uart, error_code::invalid_config);
	}

	HAL_NVIC_SetPriority(irq, config_.irq_priority, 0);
	HAL_NVIC_EnableIRQ(irq);
	return {};
}

size_t uart::available() const
{
	return rx_stream_buf_ ? xStreamBufferBytesAvailable(rx_stream_buf_) : 0;
}

uint32_t uart::dropped_count() const
{
	return dropped_count_.load(std::memory_order_relaxed);
}

result<size_t> uart::read(uint8_t *data, size_t size, uint32_t timeout_ms)
{
	if (!initialized_)
	{
		return fail(component::uart, error_code::not_initialized);
	}

	if (!data || size == 0 || !rx_stream_buf_)
	{
		return fail(component::uart, error_code::invalid_config);
	}

	TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
	size_t bytes_read = xStreamBufferReceive(rx_stream_buf_, data, size, ticks);

	return bytes_read;
}

//
// UART HAL Callbacks
//

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
	if (auto *obj = get_instance(huart->Instance))
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		obj->on_rx_event(size, &xHigherPriorityTaskWoken);

		if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
		{
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
		}
	}
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	if (auto *obj = get_instance(huart->Instance))
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		obj->on_tx_event(&xHigherPriorityTaskWoken);

		if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
		{
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
		}
	}
}

//
// HAL UART Callbacks
//

extern "C" void USART1_IRQHandler(void)
{
	if (instance_usart1)
	{
		HAL_UART_IRQHandler(&instance_usart1->handle());
	}
}

extern "C" void USART2_IRQHandler(void)
{
	if (instance_usart2)
	{
		HAL_UART_IRQHandler(&instance_usart2->handle());
	}
}

extern "C" void USART6_IRQHandler(void)
{
	if (instance_usart6)
	{
		HAL_UART_IRQHandler(&instance_usart6->handle());
	}
}

//
// DMA UART Callbacks
//

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (auto *obj = get_instance(huart->Instance))
	{
		BaseType_t higher_priority_woken = pdFALSE;
		obj->on_error(&higher_priority_woken);

		portYIELD_FROM_ISR(higher_priority_woken);
	}
}

extern "C" void DMA1_Stream5_IRQHandler(void)
{
	if (auto *h = get_dma_handle(DMA1_Stream5))
		HAL_DMA_IRQHandler(h);
}

extern "C" void DMA1_Stream6_IRQHandler(void)
{
	if (auto *h = get_dma_handle(DMA1_Stream6))
		HAL_DMA_IRQHandler(h);
}

extern "C" void DMA2_Stream1_IRQHandler(void)
{
	if (auto *h = get_dma_handle(DMA2_Stream1))
		HAL_DMA_IRQHandler(h);
}

extern "C" void DMA2_Stream5_IRQHandler(void)
{
	if (auto *h = get_dma_handle(DMA2_Stream5))
		HAL_DMA_IRQHandler(h);
}

extern "C" void DMA2_Stream6_IRQHandler(void)
{
	if (auto *h = get_dma_handle(DMA2_Stream6))
		HAL_DMA_IRQHandler(h);
}

extern "C" void DMA2_Stream7_IRQHandler(void)
{
	if (auto *h = get_dma_handle(DMA2_Stream7))
		HAL_DMA_IRQHandler(h);
}
