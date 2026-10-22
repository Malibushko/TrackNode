#pragma once

#include "drivers/spi_device.h"

#include <cstdint>
#include <span>

class w25qxx : private spi_device
{
public:
	static constexpr std::uint32_t sector_size = 4096;
	static constexpr std::uint32_t page_size = 256;

	using spi_device::spi_device;

	status init();
	status read(std::uint32_t address, std::span<std::uint8_t> destination);
	status program(std::uint32_t address, std::span<const std::uint8_t> source);
	status erase_sector(std::uint32_t address);

	[[nodiscard]] std::uint32_t capacity_bytes() const { return capacity_bytes_; }
	[[nodiscard]] std::uint8_t manufacturer_id() const { return manufacturer_id_; }

private:
	status write_enable();
	status wait_ready(std::uint32_t timeout_ms);
	status read_status(std::uint8_t &value);
	status read_jedec_id();
	status release_power_down();
	status validate_range(std::uint32_t address, std::size_t size) const;
	status program_page(std::uint32_t address, std::span<const std::uint8_t> source);
	void pause_one_millisecond() const;

private:
	std::uint32_t capacity_bytes_ = 0;
	std::uint8_t manufacturer_id_ = 0;
	bool identified_ = false;
};
