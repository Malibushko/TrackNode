#include "drivers/i2c_device.h"

namespace
{
uint16_t to_hal_address(uint8_t address)
{
	return static_cast<uint16_t>(address) << 1;
}

constexpr uint8_t MAX_ADDRESS = 0x7Fu;
}

status i2c_device::read_registers(uint8_t first_register, std::span<uint8_t> destination, uint32_t timeout)
{
	if (address_ > MAX_ADDRESS)
	{
		return fail(component::i2c_device, error_code::invalid_config);
	}

	return bus_.mem_read(to_hal_address(address_), first_register, destination, I2C_MEMADD_SIZE_8BIT, timeout);
}

status i2c_device::write_register(uint8_t register_address, uint8_t value, uint32_t timeout)
{
	if (address_ > MAX_ADDRESS)
	{
		return fail(component::i2c_device, error_code::invalid_config);
	}

	uint8_t buf[2] = { register_address, value };
	const uint16_t hal_address = static_cast<uint16_t>(address_) << 1;

	return bus_.transmit(hal_address, buf, timeout);
}