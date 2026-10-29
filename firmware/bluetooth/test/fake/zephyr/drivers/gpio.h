#pragma once
#include <cstdint>
struct device;
struct gpio_callback;
using gpio_callback_handler_t = void (*)(const device *, gpio_callback *, std::uint32_t);
struct gpio_callback
{
	gpio_callback_handler_t handler = nullptr;
	std::uint32_t pin_mask = 0;
};
struct gpio_dt_spec
{
	const device *port = nullptr;
	std::uint8_t pin = 0;
	std::uint32_t dt_flags = 0;
};
inline constexpr std::uint32_t GPIO_INPUT = 1u << 16;
inline constexpr std::uint32_t GPIO_INT_DISABLE = 1u << 21;
inline constexpr std::uint32_t GPIO_INT_LEVEL_ACTIVE = 1u << 22;
inline gpio_callback *fake_gpio_callback = nullptr;
inline std::uint32_t fake_gpio_interrupt = 0;
#ifndef BIT
#define BIT(n) (1u << (n))
#endif
inline void gpio_init_callback(gpio_callback *callback, gpio_callback_handler_t handler, std::uint32_t mask)
{
	callback->handler = handler;
	callback->pin_mask = mask;
}
inline int gpio_add_callback_dt(const gpio_dt_spec *, gpio_callback *callback)
{
	fake_gpio_callback = callback;
	return 0;
}
inline int gpio_pin_configure_dt(const gpio_dt_spec *, std::uint32_t)
{
	return 0;
}
inline int gpio_pin_interrupt_configure_dt(const gpio_dt_spec *, std::uint32_t flags)
{
	fake_gpio_interrupt = flags;
	return 0;
}
