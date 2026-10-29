#pragma once

#include "core/result.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class w25qxx
{
public:
	static constexpr std::uint32_t sector_size = 4096;
	static constexpr std::uint32_t page_size = 256;

	explicit w25qxx(std::uint32_t capacity = 16 * sector_size) : memory(capacity, 0xFF) {}

	status init()
	{
		if (fail_init)
		{
			return fail(component::spi_device, error_code::device_error);
		}
		return {};
	}

	status read(std::uint32_t address, std::span<std::uint8_t> destination)
	{
		if (!in_range(address, destination.size()))
		{
			return fail(component::spi_device, error_code::invalid_argument);
		}
		std::copy_n(memory.begin() + address, destination.size(), destination.begin());
		bytes_read += destination.size();
		return {};
	}

	status program(std::uint32_t address, std::span<const std::uint8_t> source)
	{
		if (!in_range(address, source.size()))
		{
			return fail(component::spi_device, error_code::invalid_argument);
		}
		for (std::size_t index = 0; index < source.size(); ++index)
		{
			if (program_budget)
			{
				if (*program_budget == 0)
				{
					return fail(component::spi_device, error_code::timeout_error);
				}
				--*program_budget;
			}
			memory[address + index] &= source[index];
		}
		return {};
	}

	status erase_sector(std::uint32_t address)
	{
		if (address % sector_size != 0 || !in_range(address, sector_size))
		{
			return fail(component::spi_device, error_code::invalid_argument);
		}
		std::fill_n(memory.begin() + address, sector_size, 0xFF);
		++erase_count;
		return {};
	}

	[[nodiscard]] std::uint32_t capacity_bytes() const { return static_cast<std::uint32_t>(memory.size()); }
	[[nodiscard]] std::uint8_t manufacturer_id() const { return 0xEF; }
	[[nodiscard]] std::array<std::uint8_t, 3> jedec_id() const { return reported_jedec_id; }

	std::vector<std::uint8_t> memory;
	std::array<std::uint8_t, 3> reported_jedec_id{0xEF, 0x40, 0x17};
	bool fail_init = false;
	std::optional<std::size_t> program_budget;
	std::size_t bytes_read = 0;
	std::size_t erase_count = 0;

private:
	[[nodiscard]] bool in_range(std::uint32_t address, std::size_t size) const
	{
		return size != 0 && address < memory.size() && size <= memory.size() - address;
	}
};
