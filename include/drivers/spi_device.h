#pragma once
#include "gpio.h"
#include "drivers/spi_bus.h"

#include <span>

struct spi_device_config
{
	static const uint8_t READ_FLAG = 0x80;

	pin cs_pin{};
	bool cs_active_low{false};
	uint8_t read_flag = spi_device_config::READ_FLAG;
};

class spi_cs_guard
{
public:
	explicit spi_cs_guard(gpio& pin_cs) : pin_cs(pin_cs)
	{
		auto status = pin_cs.write(gpio::state::set);
		if (!status)
		{
			has_error = true;
		}
	}

	~spi_cs_guard()
	{
		if (!has_error)
		{
			(void)pin_cs.write(gpio::state::reset);
		}
	}

	spi_cs_guard(const spi_cs_guard &) = delete;
	spi_cs_guard &operator=(const spi_cs_guard &) = delete;

private:
	gpio& pin_cs;
	bool has_error = false;
};

class spi_device
{
	static const uint32_t DEFAULT_TIMEOUT = 100;
public:
	spi_device(spi_bus &bus, spi_device_config config);

	status init();

	status read_registers(uint8_t first_register, std::span<uint8_t> destination, uint32_t timeout = DEFAULT_TIMEOUT);
	status write_register(uint8_t register_address, uint8_t value, uint32_t timeout = DEFAULT_TIMEOUT);

private:
	spi_bus &bus_;
	spi_device_config config_;

	gpio pin_cs;
	bool initialized_{false};
};