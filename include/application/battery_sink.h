#pragma once

#include "core/result.h"
#include "drivers/protocols/bluetooth_battery.h"

class battery_sink
{
public:
	virtual ~battery_sink() = default;
	virtual status submit_battery(const bluetooth_battery::snapshot &value) = 0;
};
