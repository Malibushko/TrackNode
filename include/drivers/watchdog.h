#pragma once
#include "core/hardware_object.h"
#include "core/result.h"
#include "platform/hal.h"

struct watchdog_config
{
	IWDG_TypeDef* instance = nullptr;
	uint32_t prescaler = IWDG_PRESCALER_64;
	uint32_t reload = 999;
};

class watchdog : hardware_object
{
public:
	watchdog(const watchdog_config & config);

	status init();
	status refresh();

private:
	bool initialized_{ false };
	watchdog_config config_;
	IWDG_HandleTypeDef handle_;
};