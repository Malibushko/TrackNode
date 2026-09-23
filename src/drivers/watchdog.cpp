#include "drivers/watchdog.h"

namespace
{
constexpr uint32_t lsi_min_hz  = 17'000;  // F4 datasheet
constexpr uint32_t lsi_max_hz  = 47'000;
constexpr uint32_t reload_max  = 0xFFF;

constexpr uint32_t divider(uint32_t prescaler)
{
	switch (prescaler)
	{
	case IWDG_PRESCALER_4:   return 4;
	case IWDG_PRESCALER_8:   return 8;
	case IWDG_PRESCALER_16:  return 16;
	case IWDG_PRESCALER_32:  return 32;
	case IWDG_PRESCALER_64:  return 64;
	case IWDG_PRESCALER_128: return 128;
	case IWDG_PRESCALER_256: return 256;
	default:                 return 0;
	}
}
}

watchdog::watchdog(const watchdog_config &config) :
	config_(config)
{
}

status watchdog::init()
{
	if (initialized_)
	{
		return fail(component::watchdog, error_code::already_initialized);
	}

	if (config_.instance != IWDG)
	{
		return fail(component::watchdog, error_code::invalid_config);
	}

	const uint32_t div = divider(config_.prescaler);
	if (div == 0 || config_.reload > reload_max)
	{
		return fail(component::watchdog, error_code::invalid_config);
	}

	handle_.Instance = config_.instance;
	handle_.Init.Prescaler = config_.prescaler;
	handle_.Init.Reload = config_.reload;

	auto status = from_hal(component::watchdog, HAL_IWDG_Init(&handle_));

	initialized_ = status.has_value();

	return status;
}

status watchdog::refresh()
{
	if (!initialized_)
	{
		return fail(component::watchdog, error_code::not_initialized);
	}

	return from_hal(component::watchdog, HAL_IWDG_Refresh(&handle_));
}