#include "drivers/devices/w25qxx.h"

#include <algorithm>
#include <array>

namespace
{
constexpr std::uint8_t read_id_command = 0x9F;
constexpr std::uint8_t read_status_command = 0x05;
constexpr std::uint8_t write_enable_command = 0x06;
constexpr std::uint8_t read_data_command = 0x03;
constexpr std::uint8_t page_program_command = 0x02;
constexpr std::uint8_t sector_erase_command = 0x20;
constexpr std::uint8_t release_power_down_command = 0xAB;
constexpr std::uint8_t minimum_capacity_code = 16;
constexpr std::uint8_t maximum_capacity_code = 24;
constexpr std::uint8_t busy_flag = 0x01;
constexpr std::uint8_t write_enable_latch = 0x02;
constexpr std::uint32_t operation_timeout_ms = 1000;

std::array<std::uint8_t, 4> address_command(std::uint8_t command, std::uint32_t address)
{
	return { command,
		static_cast<std::uint8_t>(address >> 16),
		static_cast<std::uint8_t>(address >> 8),
		static_cast<std::uint8_t>(address) };
}
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::init()
{
	if (identified_)
	{
		return fail(component::spi_device, error_code::already_initialized);
	}
	if (!spi_device::initialized_)
	{
		if (auto started = spi_device::init(); !started)
		{
			return started;
		}
	}
	if (auto awake = release_power_down(); !awake)
	{
		return awake;
	}
	if (auto identified = read_jedec_id(); !identified)
	{
		return identified;
	}
	identified_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::read(std::uint32_t address, std::span<std::uint8_t> destination)
{
	if (!identified_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}
	if (auto valid = validate_range(address, destination.size()); !valid)
	{
		return valid;
	}
	const auto command = address_command(read_data_command, address);
	const spi_cs_guard guard{bus_, pin_cs};
	if (!guard.asserted())
	{
		return fail(component::spi_device, error_code::device_error);
	}
	if (auto sent = bus_.transmit(command); !sent)
	{
		return sent;
	}
	return bus_.receive(destination, operation_timeout_ms);
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::program(std::uint32_t address, std::span<const std::uint8_t> source)
{
	if (!identified_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}
	if (auto valid = validate_range(address, source.size()); !valid)
	{
		return valid;
	}
	while (!source.empty())
	{
		const auto page_offset = address % page_size;
		const auto count = std::min<std::size_t>(page_size - page_offset, source.size());
		if (auto written = program_page(address, source.first(count)); !written)
		{
			return written;
		}
		address += static_cast<std::uint32_t>(count);
		source = source.subspan(count);
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::erase_sector(std::uint32_t address)
{
	if (!identified_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}
	if (address % sector_size != 0 || address >= capacity_bytes_)
	{
		return fail(component::spi_device, error_code::invalid_argument);
	}
	if (auto enabled = write_enable(); !enabled)
	{
		return enabled;
	}
	const auto command = address_command(sector_erase_command, address);
	{
		const spi_cs_guard guard{bus_, pin_cs};
		if (!guard.asserted())
		{
			return fail(component::spi_device, error_code::device_error);
		}
		if (auto sent = bus_.transmit(command); !sent)
		{
			return sent;
		}
	}
	return wait_ready(operation_timeout_ms);
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::write_enable()
{
	const std::uint8_t command = write_enable_command;
	{
		const spi_cs_guard guard{bus_, pin_cs};
		if (!guard.asserted())
		{
			return fail(component::spi_device, error_code::device_error);
		}
		if (auto sent = bus_.transmit(std::span{ &command, 1 }); !sent)
		{
			return sent;
		}
	}
	std::uint8_t state = 0;
	if (auto read = read_status(state); !read)
	{
		return read;
	}
	return (state & write_enable_latch) != 0 ? status{} : fail(component::spi_device, error_code::device_error);
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::wait_ready(std::uint32_t timeout_ms)
{
	const auto started_at = HAL_GetTick();
	for (;;)
	{
		std::uint8_t state = 0;
		if (auto read = read_status(state); !read)
		{
			return read;
		}
		if ((state & busy_flag) == 0)
		{
			return {};
		}
		if (HAL_GetTick() - started_at >= timeout_ms)
		{
			return fail(component::spi_device, error_code::timeout_error);
		}
		pause_one_millisecond();
	}
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::read_status(std::uint8_t &value)
{
	const std::uint8_t command = read_status_command;
	const spi_cs_guard guard{bus_, pin_cs};
	if (!guard.asserted())
	{
		return fail(component::spi_device, error_code::device_error);
	}
	if (auto sent = bus_.transmit(std::span{ &command, 1 }); !sent)
	{
		return sent;
	}
	return bus_.receive(std::span{ &value, 1 });
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::read_jedec_id()
{
	const std::uint8_t command = read_id_command;
	std::array<std::uint8_t, 3> id{};
	{
		const spi_cs_guard guard{bus_, pin_cs};
		if (!guard.asserted())
		{
			return fail(component::spi_device, error_code::device_error);
		}
		if (auto sent = bus_.transmit(std::span{ &command, 1 }); !sent)
		{
			return sent;
		}
		if (auto received = bus_.receive(id); !received)
		{
			return received;
		}
	}
	jedec_id_ = id;
	const auto bus_reads_floating_or_shorted = id[0] == 0x00 || id[0] == 0xFF;
	const auto capacity_supported = id[2] >= minimum_capacity_code && id[2] <= maximum_capacity_code;
	if (bus_reads_floating_or_shorted || !capacity_supported)
	{
		return fail(component::spi_device, error_code::device_error);
	}
	manufacturer_id_ = id[0];
	capacity_bytes_ = std::uint32_t{1} << id[2];
	return {};
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::validate_range(std::uint32_t address, std::size_t size) const
{
	if (size == 0 || address >= capacity_bytes_ || size > capacity_bytes_ - address)
	{
		return fail(component::spi_device, error_code::invalid_argument);
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::program_page(std::uint32_t address, std::span<const std::uint8_t> source)
{
	if (auto enabled = write_enable(); !enabled)
	{
		return enabled;
	}
	const auto command = address_command(page_program_command, address);
	{
		const spi_cs_guard guard{bus_, pin_cs};
		if (!guard.asserted())
		{
			return fail(component::spi_device, error_code::device_error);
		}
		if (auto sent = bus_.transmit(command); !sent)
		{
			return sent;
		}
		if (auto sent = bus_.transmit(source); !sent)
		{
			return sent;
		}
	}
	return wait_ready(operation_timeout_ms);
}

//////////////////////////////////////////////////////////////////////////

status w25qxx::release_power_down()
{
	const std::uint8_t command = release_power_down_command;
	{
		const spi_cs_guard guard{bus_, pin_cs};
		if (!guard.asserted())
		{
			return fail(component::spi_device, error_code::device_error);
		}
		if (auto sent = bus_.transmit(std::span{ &command, 1 }); !sent)
		{
			return sent;
		}
	}
	pause_one_millisecond();
	return {};
}

//////////////////////////////////////////////////////////////////////////

void w25qxx::pause_one_millisecond() const
{
	if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
	{
		vTaskDelay(1);
		return;
	}
	HAL_Delay(1);
}

//////////////////////////////////////////////////////////////////////////
