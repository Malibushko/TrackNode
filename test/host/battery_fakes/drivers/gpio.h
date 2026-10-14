#pragma once
#include "core/result.h"

struct pin
{
	GPIO_TypeDef *port = nullptr;
	std::uint16_t mask = 0;
};
struct gpio_config
{
	::pin pin{};
	std::uint32_t mode = GPIO_MODE_INPUT;
	std::uint32_t pull = GPIO_NOPULL;
};
class gpio
{
public:
	enum class state { set, reset };
	explicit gpio(gpio_config) {}
	status init()
	{
		return {};
	}
	result<state> read() const
	{
		return usb_present ? state::set : state::reset;
	}
	inline static bool usb_present = false;
};
