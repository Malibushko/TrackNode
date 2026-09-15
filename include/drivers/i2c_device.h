#pragma once
#include "drivers/i2c_bus.h"

#include <span>

class i2c_device
{
public:
	i2c_device(i2c_bus &bus, uint8_t address) : bus_(bus), address_(address)
	{
	}

	status init();

	status read_registers(uint8_t first_register, std::span<uint8_t> destination, uint32_t timeout = HAL_MAX_DELAY);
	status write_register(uint8_t register_address, uint8_t value, uint32_t timeout = HAL_MAX_DELAY);

private:
	i2c_bus &bus_;
	uint8_t address_;
};