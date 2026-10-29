#include "drivers/gpio_interrupt.h"

#include <bit>

std::array<gpio_interrupt *, 16> gpio_interrupt::lines_{};

namespace
{
IRQn_Type line_irq(std::uint32_t line)
{
	switch (line)
	{
		case 0:
			return EXTI0_IRQn;
		case 1:
			return EXTI1_IRQn;
		case 2:
			return EXTI2_IRQn;
		case 3:
			return EXTI3_IRQn;
		case 4:
			return EXTI4_IRQn;
		default:
			return line <= 9 ? EXTI9_5_IRQn : EXTI15_10_IRQn;
	}
}

//////////////////////////////////////////////////////////////////////////

status enable_port_clock(GPIO_TypeDef *port)
{
	if (port == GPIOA)
	{
		__HAL_RCC_GPIOA_CLK_ENABLE();
	}
	else if (port == GPIOB)
	{
		__HAL_RCC_GPIOB_CLK_ENABLE();
	}
	else if (port == GPIOC)
	{
		__HAL_RCC_GPIOC_CLK_ENABLE();
	}
	else if (port == GPIOH)
	{
		__HAL_RCC_GPIOH_CLK_ENABLE();
	}
	else
	{
		return fail(component::gpio, error_code::invalid_config);
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

void handle_lines(std::uint32_t first, std::uint32_t last)
{
	for (auto line = first; line <= last; ++line)
	{
		const auto mask = static_cast<std::uint16_t>(1u << line);
		if (__HAL_GPIO_EXTI_GET_IT(mask) != 0u)
		{
			HAL_GPIO_EXTI_IRQHandler(mask);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
}

//////////////////////////////////////////////////////////////////////////

status gpio_interrupt::init()
{
	if (initialized_)
	{
		return fail(component::gpio, error_code::already_initialized);
	}

	const auto pin = config_.pin;
	const auto single_pin = pin.port && pin.mask && (pin.mask & (pin.mask - 1u)) == 0;
	const auto lowest_safe_priority = configMAX_SYSCALL_INTERRUPT_PRIORITY >> (8U - __NVIC_PRIO_BITS);
	const auto irq_may_use_rtos = config_.irq_priority >= lowest_safe_priority && config_.irq_priority < (1U << __NVIC_PRIO_BITS);
	if (!single_pin || !IS_GPIO_PULL(config_.pull) || !irq_may_use_rtos)
	{
		return fail(component::gpio, error_code::invalid_config);
	}

	const auto line = static_cast<std::uint32_t>(std::countr_zero(pin.mask));
	const auto line_taken_by_other_pin = lines_[line] && lines_[line] != this;
	if (line_taken_by_other_pin)
	{
		return fail(component::gpio, error_code::invalid_config);
	}
	if (auto clocked = enable_port_clock(pin.port); !clocked)
	{
		return clocked;
	}
	__HAL_RCC_SYSCFG_CLK_ENABLE();

	lines_[line] = this;
	GPIO_InitTypeDef hal_config{};
	hal_config.Pin = pin.mask;
	hal_config.Mode = GPIO_MODE_IT_RISING_FALLING;
	hal_config.Pull = config_.pull;
	hal_config.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(pin.port, &hal_config);
	__HAL_GPIO_EXTI_CLEAR_IT(pin.mask);

	const auto irq = line_irq(line);
	HAL_NVIC_SetPriority(irq, config_.irq_priority, 0);
	HAL_NVIC_EnableIRQ(irq);
	initialized_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

void gpio_interrupt::set_listener(TaskHandle_t task)
{
	listener_ = task;
}

//////////////////////////////////////////////////////////////////////////

bool gpio_interrupt::high() const
{
	return HAL_GPIO_ReadPin(config_.pin.port, config_.pin.mask) == GPIO_PIN_SET;
}

//////////////////////////////////////////////////////////////////////////

void gpio_interrupt::dispatch(std::uint16_t mask)
{
	const auto line = static_cast<std::uint32_t>(std::countr_zero(mask));
	auto *owner = line < lines_.size() ? lines_[line] : nullptr;
	if (!owner)
	{
		return;
	}

	BaseType_t higher_priority_woken = pdFALSE;
	signal_task_event_from_isr(owner->listener_, &higher_priority_woken);
	portYIELD_FROM_ISR(higher_priority_woken);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_GPIO_EXTI_Callback(std::uint16_t mask)
{
	gpio_interrupt::dispatch(mask);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void EXTI0_IRQHandler()
{
	handle_lines(0, 0);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void EXTI1_IRQHandler()
{
	handle_lines(1, 1);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void EXTI2_IRQHandler()
{
	handle_lines(2, 2);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void EXTI3_IRQHandler()
{
	handle_lines(3, 3);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void EXTI4_IRQHandler()
{
	handle_lines(4, 4);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void EXTI9_5_IRQHandler()
{
	handle_lines(5, 9);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void EXTI15_10_IRQHandler()
{
	handle_lines(10, 15);
}

//////////////////////////////////////////////////////////////////////////
