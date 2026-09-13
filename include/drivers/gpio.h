#pragma once

#include "core/hardware_object.h"
#include "core/result.h"

struct pin
{
	GPIO_TypeDef *port = nullptr;
	uint16_t mask = 0;
};

struct gpio_config
{
	::pin pin{};
	uint32_t mode = GPIO_MODE_INPUT;
	uint32_t pull = GPIO_NOPULL;
	uint32_t speed = GPIO_SPEED_FREQ_LOW;
	uint32_t alternate = 0;
	bool initial_high = false;
};

class gpio : public hardware_object
{
public:

	enum class state
	{
		set,
		reset
	};

public:
	explicit gpio(gpio_config config) : config_(config)
	{
	}

	status init();
	[[nodiscard]] result<bool> read() const;
	status write(state signal);
	status toggle();

private:
	gpio_config config_;
	bool initialized_ = false;
};
