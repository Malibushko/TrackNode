#pragma once

#include "core/task_event_source.h"
#include "drivers/gpio.h"
#include "libs/freertos/freertos_helpers.h"

#include <array>
#include <cstdint>

struct gpio_interrupt_config
{
	::pin pin{};
	std::uint32_t pull = GPIO_NOPULL;
	std::uint32_t irq_priority = 6;
};

class gpio_interrupt final : public hardware_object, public task_event_source
{
public:
	explicit gpio_interrupt(gpio_interrupt_config config) : config_(config) {}

public:
	status init();
	void set_listener(TaskHandle_t task) override;
	[[nodiscard]] bool high() const;

public:
	static void dispatch(std::uint16_t mask);

private:
	static std::array<gpio_interrupt *, 16> lines_;
	gpio_interrupt_config config_;
	TaskHandle_t volatile listener_ = nullptr;
	bool initialized_ = false;
};
