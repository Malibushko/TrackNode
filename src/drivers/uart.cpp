#include "drivers/uart.h"

#include "platform/stdio.h"

#include <algorithm>

namespace {

uart* instance_usart1 = nullptr;
uart* instance_usart2 = nullptr;
uart* instance_usart6 = nullptr;

void bind_instance(USART_TypeDef* periph, uart* obj)
{
	if (periph == USART1)
		instance_usart1 = obj;
	else if (periph == USART2)
		instance_usart2 = obj;
	else if (periph == USART6)
		instance_usart6 = obj;
}

void unbind_instance(USART_TypeDef* periph)
{
	bind_instance(periph, nullptr);
}

inline uart* get_instance(USART_TypeDef* periph)
{
	if (periph == USART1)
		return instance_usart1;
	if (periph == USART2)
		return instance_usart2;
	if (periph == USART6)
		return instance_usart6;

	return nullptr;
}

IRQn_Type irq_for(USART_TypeDef* instance)
{
	if (instance == USART1)
		return USART1_IRQn;
	if (instance == USART2)
		return USART2_IRQn;
	if (instance == USART6)
		return USART6_IRQn;

	return static_cast<IRQn_Type>(-1);
}

}

uart::uart(uart_config config) : config_(config)
{
	rx_stream_buf_ = xStreamBufferCreate(config_.rx_buffer_size, 1);
	bind_instance(config_.instance, this);
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
    if (!operation_status)
    {
        return operation_status;
    }

    initialized_ = true;

    if (config_.mode & UART_MODE_RX)
    {
        operation_status = enable_irq();
        if (!operation_status)
        {
            initialized_ = false;
            return operation_status;
        }
        request_next_byte();
    }

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

    return from_hal(component::uart, HAL_UART_Transmit(&handle_, data, static_cast<uint16_t>(size), timeout_ms));
}

void uart::request_next_byte()
{
    HAL_UART_Receive_IT(&handle_, &rx_byte_, 1);
}

void uart::on_rx_complete(BaseType_t* higher_priority_woken)
{
	size_t sent = xStreamBufferSendFromISR(rx_stream_buf_, &rx_byte_, sizeof(rx_byte_), higher_priority_woken);

	if (!sent)
	{
		dropped_count_.fetch_add(1, std::memory_order_relaxed);
	}

    request_next_byte();
}

void uart::on_error()
{
    __HAL_UART_CLEAR_PEFLAG(&handle_);

    request_next_byte();
}

size_t uart::available() const
{
    return xStreamBufferBytesAvailable(rx_stream_buf_);
}

uint32_t uart::dropped_count() const
{
    return dropped_count_.load(std::memory_order_relaxed);
}

result<size_t> uart::read(uint8_t *data, size_t size, uint32_t timeout_ms)
{
	if (!initialized_)
		return fail(component::uart, error_code::not_initialized);

	if (!data || size == 0)
		return fail(component::uart, error_code::invalid_config);

	TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
	size_t bytes_read = xStreamBufferReceive(rx_stream_buf_, data, size, ticks);

	return bytes_read;
}

//
// UART HAL Callbacks
//

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (auto* obj = get_instance(huart->Instance))
    {
    	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    	obj->on_rx_complete(&xHigherPriorityTaskWoken);

    	if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    	{
    		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    	}
    }
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (auto* obj = get_instance(huart->Instance))
    {
        obj->on_error();
    }
}

extern "C" void USART1_IRQHandler(void)
{
	if (instance_usart1)
		HAL_UART_IRQHandler(&instance_usart1->handle());
}

extern "C" void USART2_IRQHandler(void)
{
	if (instance_usart2)
		HAL_UART_IRQHandler(&instance_usart2->handle());
}

extern "C" void USART6_IRQHandler(void)
{
	if (instance_usart6)
		HAL_UART_IRQHandler(&instance_usart6->handle());
}
