#include "drivers/devices/sd_card.h"

#include "common/common.h"

enum CMD
{
	CMD0,  CMD1,  CMD2,  CMD3,  CMD4,  CMD5,  CMD6,  CMD7,
	CMD8,  CMD9,  CMD10, CMD11, CMD12, CMD13, CMD14, CMD15,
	CMD16, CMD17, CMD18, CMD19, CMD20, CMD21, CMD22, CMD23,
	CMD24, CMD25, CMD26, CMD27, CMD28, CMD29, CMD30, CMD31,
	CMD32, CMD33, CMD34, CMD35, CMD36, CMD37, CMD38, CMD39,
	CMD40, CMD41, CMD42, CMD43, CMD44, CMD45, CMD46, CMD47,
	CMD48, CMD49, CMD50, CMD51, CMD52, CMD53, CMD54, CMD55,
	CMD56, CMD57, CMD58, CMD59, CMD60, CMD61, CMD62, CMD63,
	COUNT
};

static constexpr uint32_t DEFAULT_CRC = 0xFF;
static constexpr uint32_t DEFAULT_RESPONSE = 0xFF;
static constexpr uint8_t STOP_BIT = 1;
static constexpr uint8_t MAX_ATTEMPTS = 8;

status sd_card::init()
{
	auto status = spi_device::init()
					.and_then([&] { return card_warm_up(); });
	if (!status)
	{
		return status;
	}

	r1_t response;
	status = go_idle_state(response, MAX_DELAY).and_then([&] { return validate_response(response); });
	if (!status)
	{
		return status;
	}

	if (!response.in_idle_state())
	{
		return fail(component::spi_device, error_code::device_error);
	}

	return {};
}

status sd_card::go_idle_state(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	spi_cs_guard guard(pin_cs);
	if (auto status = send_command(CMD0, 0x95, timeout, r1); !status)
	{
		return status;
	}

	return {};
}

status sd_card::app_send_op_cond(bool host_capacity_support, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::send_op_cond(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::send_if_cond(uint8_t voltage_supplied, uint8_t check_pattern, r7_t &response, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::send_csd(std::span<uint8_t, 16> csd, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::send_cid(std::span<uint8_t, 16> cid, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::stop_transmission(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::set_blocklen(uint32_t block_length, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::read_single_block(uint32_t address, std::span<uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::read_multiple_block(uint32_t address, std::span<uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::set_block_count(uint16_t number_of_blocks, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::set_wr_block_erase_count(uint32_t number_of_blocks, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::write_block(uint32_t address, std::span<const uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::write_multiple_block(uint32_t address, std::span<const uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::app_cmd(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::read_ocr(r3_t &response, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

status sd_card::send_command(uint8_t index, uint32_t crc, uint32_t timeout, r1_t &response)
{
	return send_command(index, 0, crc, timeout, response);
}

status sd_card::send_command(uint8_t index, uint32_t argument, uint32_t crc, uint32_t timeout, r1_t &response)
{
	if (index >= CMD::COUNT)
	{
		return fail(component::spi_device, error_code::invalid_argument);
	}

	const uint8_t frame[6] = {
		static_cast<uint8_t>(0x40 | index),
		static_cast<uint8_t>(argument >> 24),
		static_cast<uint8_t>(argument >> 16),
		static_cast<uint8_t>(argument >> 8),
		static_cast<uint8_t>(argument),
		static_cast<uint8_t>(crc | STOP_BIT),
	};

	if (auto status = bus_.transmit(std::span{frame, 6}, timeout); !status)
	{
		return status;
	}

	for (uint8_t attempt = 0; attempt < MAX_ATTEMPTS; attempt++)
	{
		response.raw = DEFAULT_RESPONSE;
		auto status = bus_.receive(std::span{ (&response.raw), 1 }, timeout);
		if (!status)
		{
			return status;
		}

		if (response.received())
		{
			return {};
		}
	}

	return fail(component::spi_device, error_code::device_error);
}


status sd_card::validate_response(r1_t response) const
{
	if (!response.received()) {
		return fail(component::spi_device, error_code::invalid_argument);
	}

	if (response.illegal_command()) {
		return fail(component::spi_device, error_code::invalid_argument);
	}

	if (response.crc_error()) {
		return fail(component::spi_device, error_code::invalid_argument);
	}

	if (response.parameter_error() || response.address_error()) {
		return fail(component::spi_device, error_code::invalid_argument);
	}

	if (response.erase_sequence_error() || response.erase_reset()) {
		return fail(component::spi_device, error_code::invalid_argument);
	}

	return {};
}

status sd_card::card_warm_up()
{
	// Set CS=1 and MOSI=1
	// Minimum for 74 ticks
	static constexpr uint8_t warmup_ticks = 100;

	if (auto result = pin_cs.write(gpio::state::reset); !result)
	{
		return result;
	}

	uint8_t data[] = { 0xFF };

	for (uint8_t i = 0; i < warmup_ticks; i++)
	{
		auto status = bus_.transmit(data, MAX_DELAY);
		if (!status)
		{
			return status;
		}
	}

	return {};
}
