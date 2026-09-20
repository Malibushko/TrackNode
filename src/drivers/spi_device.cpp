#include "drivers/spi_device.h"

#include "drivers/gpio.h"

spi_device::spi_device(spi_bus &bus, spi_device_config config) :
	bus_(bus),
	config_(config),
	pin_cs{ gpio_config	{ .pin = config_.cs_pin, .mode = GPIO_MODE_OUTPUT_PP, .pull = GPIO_NOPULL, .speed = GPIO_SPEED_FREQ_VERY_HIGH, .initial_high = true, .active_low = true } }
{

}

status spi_device::init()
{
	if (initialized_)
	{
		return fail(component::spi_device, error_code::already_initialized);
	}

	if (config_.cs_pin.port == nullptr)
	{
		return fail(component::spi_device, error_code::invalid_config);
	}

	auto status = pin_cs.init();
	initialized_ = status.has_value();
	return status;
}

status spi_device::read_registers(uint8_t first_register, std::span<uint8_t> destination, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	spi_cs_guard guard{pin_cs};

	const uint8_t cmd = first_register | config_.read_flag;

	auto status = bus_.transmit(std::span{ &cmd, 1 }, timeout);
	if (!status)
	{
		return status;
	}

	std::fill(destination.begin(), destination.end(), 0xFF);
	return bus_.receive(destination, timeout);
}

status spi_device::write_register(uint8_t register_address, uint8_t value, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	const uint8_t cmd = register_address & static_cast<uint8_t>(~config_.read_flag);
	const uint8_t buf[2] = { cmd, value };

	const spi_cs_guard guard(pin_cs);
	return bus_.transmit(buf, timeout);
}