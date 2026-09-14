#include "drivers/i2c_bus.h"

status i2c_bus::init()
{
	if (attempted_)
	{
		return fail(component::i2c_bus, error_code::already_initialized);
	}

	attempted_ = true;

	if (!IS_I2C_CLOCK_SPEED(config_.clock_speed) || !IS_I2C_DUTY_CYCLE(config_.duty_cycle) ||
	    !IS_I2C_ADDRESSING_MODE(config_.addressing_mode) || !IS_I2C_DUAL_ADDRESS(config_.dual_address_mode) ||
	    !IS_I2C_GENERAL_CALL(config_.general_call_mode) || !IS_I2C_NO_STRETCH(config_.no_stretch_mode) ||
	    !IS_I2C_OWN_ADDRESS1(config_.own_address1) || !IS_I2C_OWN_ADDRESS2(config_.own_address2))
	{
		return fail(component::i2c_bus, error_code::invalid_config);
	}

	uint32_t alternate = 0;

	if (config_.instance == I2C1)
	{
		alternate = GPIO_AF4_I2C1;

		__HAL_RCC_I2C1_CLK_ENABLE();
	}
	else if (config_.instance == I2C2)
	{
		alternate = GPIO_AF4_I2C2;

		__HAL_RCC_I2C2_CLK_ENABLE();
	}
	else if (config_.instance == I2C3)
	{
		alternate = GPIO_AF4_I2C3;

		__HAL_RCC_I2C3_CLK_ENABLE();
	}
	else
	{
		return fail(component::i2c_bus, error_code::invalid_config);
	}

	i2c_handle.Instance = config_.instance;
	i2c_handle.Init.ClockSpeed = config_.clock_speed;
	i2c_handle.Init.DutyCycle = config_.duty_cycle;
	i2c_handle.Init.OwnAddress1 = config_.own_address1;
	i2c_handle.Init.AddressingMode = config_.addressing_mode;
	i2c_handle.Init.DualAddressMode = config_.dual_address_mode;
	i2c_handle.Init.OwnAddress2 = config_.own_address2;
	i2c_handle.Init.GeneralCallMode = config_.general_call_mode;
	i2c_handle.Init.NoStretchMode = config_.no_stretch_mode;

	for (const pin &pin : { config_.pin_scl, config_.pin_sda })
	{
		gpio_config pin_config{};
		pin_config.pin = pin;
		pin_config.mode = GPIO_MODE_AF_OD;
		pin_config.pull = GPIO_PULLUP;
		pin_config.speed = GPIO_SPEED_FREQ_HIGH;

		pin_config.alternate = alternate;

		gpio configured_pin{ pin_config };

		auto status = configured_pin.init();
		if (!status)
		{
			return status;
		}
	}

	auto init_status = from_hal(component::i2c_bus, HAL_I2C_Init(&i2c_handle));
	initialized_ = static_cast<bool>(init_status);
	return init_status;
}

status i2c_bus::transmit(uint16_t address, std::span<uint8_t> data, uint32_t timeout)
{
	auto validation_status = validate_transfer(address, data);
	if (!validation_status)
	{
		return validation_status;
	}

	return from_hal(component::i2c_bus, HAL_I2C_Master_Transmit(&handle(), address, data.data(),
	                                                            static_cast<uint16_t>(data.size()), timeout));
}

status i2c_bus::receive(uint16_t address, std::span<uint8_t> data, uint32_t timeout)
{
	auto validation_status = validate_transfer(address, data);
	if (!validation_status)
	{
		return validation_status;
	}

	return from_hal(component::i2c_bus, HAL_I2C_Master_Receive(&handle(), address, data.data(),
	                                                           static_cast<uint16_t>(data.size()), timeout));
}

status i2c_bus::mem_write(uint16_t address, uint16_t mem_address, std::span<uint8_t> data, uint16_t mem_addr_size,
                          uint32_t timeout)
{
	auto validation_status = validate_transfer(address, data);
	if (!validation_status)
	{
		return validation_status;
	}

	validation_status = validate_memory(mem_address, mem_addr_size);
	if (!validation_status)
	{
		return validation_status;
	}

	auto result = HAL_I2C_Mem_Write(&i2c_handle, address, mem_address, mem_addr_size, data.data(),
	                                static_cast<uint16_t>(data.size()), timeout);

	return from_hal(component::i2c_bus, result);
}

status i2c_bus::mem_read(uint16_t address, uint16_t mem_address, std::span<uint8_t> data, uint16_t mem_addr_size,
                         uint32_t timeout)
{
	auto validation_status = validate_transfer(address, data);
	if (!validation_status)
	{
		return validation_status;
	}

	validation_status = validate_memory(mem_address, mem_addr_size);
	if (!validation_status)
	{
		return validation_status;
	}

	auto result = HAL_I2C_Mem_Read(&i2c_handle, address, mem_address, mem_addr_size, data.data(),
	                               static_cast<uint16_t>(data.size()), timeout);
	return from_hal(component::i2c_bus, result);
}

status i2c_bus::validate_transfer(uint16_t address, std::span<uint8_t> data) const
{
	if (!initialized_)
	{
		return fail(component::i2c_bus, error_code::not_initialized);
	}

	const bool valid_address = config_.addressing_mode == I2C_ADDRESSINGMODE_7BIT
	                               ? address <= 0xFEu && (address & 1u) == 0
	                               : address <= 0x3FFu;

	if (!valid_address || data.empty() || data.size() > UINT16_MAX || data.data() == nullptr)
	{
		return fail(component::i2c_bus, error_code::invalid_config);
	}

	return {};
}

status i2c_bus::validate_memory(uint16_t address, uint16_t address_size) const
{
	if ((address_size != I2C_MEMADD_SIZE_8BIT && address_size != I2C_MEMADD_SIZE_16BIT) ||
	    (address_size == I2C_MEMADD_SIZE_8BIT && address > UINT8_MAX))
	{
		return fail(component::i2c_bus, error_code::invalid_config);
	}

	return {};
}
