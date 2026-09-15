#pragma once
#include "gpio.h"

#include <cstdint>
#include "core/hardware_object.h"
#include "core/result.h"

#include <span>

struct i2c_bus_config
{
	I2C_TypeDef *instance = nullptr;
	uint32_t clock_speed = 100000;
	uint32_t duty_cycle = I2C_DUTYCYCLE_2;
	uint32_t own_address1 = 0;
	uint32_t addressing_mode = I2C_ADDRESSINGMODE_7BIT;
	uint32_t dual_address_mode = I2C_DUALADDRESS_DISABLE;
	uint32_t own_address2 = 0;
	uint32_t general_call_mode = I2C_GENERALCALL_DISABLE;
	uint32_t no_stretch_mode = I2C_NOSTRETCH_DISABLE;

	pin pin_scl{};
	pin pin_sda{};
};

class i2c_bus : hardware_object
{
public:
	using handle_t = I2C_HandleTypeDef;

public:
	explicit i2c_bus(i2c_bus_config config) : i2c_handle(), config_(config)
	{
	}

	status init();

	status transmit(uint16_t address, std::span<uint8_t> data, uint32_t timeout = HAL_MAX_DELAY);
	status receive(uint16_t address, std::span<uint8_t> data, uint32_t timeout = HAL_MAX_DELAY);
	status mem_write(uint16_t address, uint16_t mem_address, std::span<uint8_t> data,
	                 uint16_t mem_addr_size = I2C_MEMADD_SIZE_8BIT, uint32_t timeout = HAL_MAX_DELAY);
	status mem_read(uint16_t address, uint16_t mem_address, std::span<uint8_t> data,
	                uint16_t mem_addr_size = I2C_MEMADD_SIZE_8BIT, uint32_t timeout = HAL_MAX_DELAY);

	handle_t &handle()
	{
		return i2c_handle;
	}

private:
	status validate_transfer(uint16_t address, std::span<uint8_t> data) const;
	status validate_memory(uint16_t address, uint16_t address_size) const;
	handle_t i2c_handle;
	i2c_bus_config config_;
	bool attempted_{ false };
	bool initialized_ = false;
};