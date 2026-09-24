#include "drivers/devices/sd_card.h"

#include "common/common.h"
#include "misc/bits_utils"

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
static constexpr uint8_t DEFAULT_RESPONSE = 0xFF;
static constexpr uint8_t STOP_BIT = 1;
static constexpr uint8_t MAX_ATTEMPTS = 8;

status sd_card::init()
{
	sector_count_ = 0;
	auto init_status = initialized_ ? ::status{} : spi_device::init();
	if (init_status)
	{
		init_status = bus_.set_baud_rate_prescaler(SPI_BAUDRATEPRESCALER_256)
		             .and_then([&] { return card_warm_up(); });
	}
	if (!init_status)
	{
		return init_status;
	}

	r1_t response;
	init_status = go_idle_state(response, MAX_DELAY).and_then([&] { return validate_response(response); });
	if (!init_status)
	{
		return init_status;
	}

	if (!response.in_idle_state())
	{
		return fail(component::spi_device, error_code::device_error);
	}

	if (auto result = send_if_cond(MAX_DELAY); !result)
	{
		return result;
	}

	const auto init_start_tick = common::get_tick();
	const auto init_timeout_limit = pdMS_TO_TICKS(1000);

	while (true)
	{
		if (auto result = app_cmd(response).and_then([&]{ return app_send_op_cond(true, response, MAX_DELAY); }); !result)
		{
			return result;
		}

		if (response.raw == 0x0)
		{
			break;
		}

		if (common::get_tick() - init_start_tick >= init_timeout_limit)
			break;

		common::delay(10);
	}

	if (response.raw != 0x0)
	{
		return fail(component::spi_device, error_code::device_error);
	}

	r3_t ocr;
	if (auto result = read_ocr(ocr, MAX_DELAY); !result)
	{
		return result;
	}

	if ((ocr.ocr & (1u << 30)) == 0)
	{
		return fail(component::spi_device, error_code::device_error);
	}

	if (auto result = bus_.set_baud_rate_prescaler(SPI_BAUDRATEPRESCALER_8); !result)
	{
		return result;
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status sd_card::sector_count(uint64_t& o_count)
{
	if (sector_count_ == 0)
	{
		std::array<uint8_t, 16> csd{};

		r1_t response;
		if (auto status = send_csd(csd, response, MAX_DELAY); !status)
		{
			return status;
		}

		const auto csd_structure = (csd[0] >> 6) & 0x03;

		if (csd_structure != 1) {
			return fail(component::spi_device, error_code::device_error);
		}

		uint32_t c_size = (static_cast<uint32_t>(csd[7] & 0x3F) << 16)
						| (static_cast<uint32_t>(csd[8])        << 8)
						|  static_cast<uint32_t>(csd[9]);

		sector_count_ = static_cast<uint64_t>(c_size + 1) * 1024;
	}

	o_count = sector_count_;
	return {};
}

//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

status sd_card::app_send_op_cond(bool host_capacity_support, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	spi_cs_guard guard{pin_cs};

	uint32_t argument{};

	if (host_capacity_support)
	{
		argument |= (1 << 30);
	} else
	{
		argument &= ~(1 << 30);
	}

	return send_command(CMD41, argument, DEFAULT_CRC, timeout, r1)
				.and_then([&]{ return validate_response(r1); })
				.and_then([&]{ return busy_pause(1); });
}

//////////////////////////////////////////////////////////////////////////

status sd_card::send_op_cond(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

status sd_card::send_if_cond(uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	spi_cs_guard guard{pin_cs};

	r7_t cmd8_response{};

	// $NOTE: 0x000001AA is datasheet value used to check if card is SDC Ver. 1 or not
	auto status = send_command(CMD8, 0x000001AA, 0x87, timeout, cmd8_response);
	if (!status)
	{
		return status;
	}

	if (cmd8_response.r1.illegal_command())
	{
		// card is SDC Ver. 1 or MMC Ver 3.
		// not supported right now
		return fail(component::spi_device, error_code::not_implemented);
	}

	if (auto validate_result = validate_response(cmd8_response.r1); !validate_result)
	{
		return validate_result;
	}

	// correct values from datasheet
	// https://elm-chan.org/docs/mmc/mmc_e.html
	if (cmd8_response.value.raw != 0x000001AA ||
		cmd8_response.value.voltage_accepted() != 1 ||
		cmd8_response.value.check_pattern() != 0xAA)
	{
		return fail(component::spi_device, error_code::device_error);
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status sd_card::send_csd(std::span<uint8_t, 16> csd, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	spi_cs_guard guard{ pin_cs };

	auto status = send_command(CMD9, DEFAULT_CRC, timeout, r1)
						.and_then([&] { return validate_response(r1); });

	if (!status)
	{
		return status;
	}

	if (r1.raw != 0)
	{
		return fail(component::spi_device, error_code::invalid_argument);
	}

	const auto start_tick = common::get_tick();
	const auto timeout_limit = pdMS_TO_TICKS(timeout);

	uint8_t rx = {};

	while (true)
	{
		rx = 0xFF;

		if (auto result = bus_.receive(std::span{ &rx, 1 }, timeout); !result)
			return result;

		if (rx == 0xFE)
		{
			break;
		}

		if (common::get_tick() - start_tick >= timeout_limit)
		{
			return fail(component::spi_device, error_code::timeout_error);
		}
	}

	std::fill(csd.begin(), csd.end(), 0xFF);
	if (auto response = bus_.receive(csd, timeout); !response)
	{
		return response;
	}

	uint8_t crc[2] = {0xFF, 0xFF};
	if (auto response = bus_.receive(std::span{ crc, 2 }, timeout); !response)
	{
		return response;
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status sd_card::send_cid(std::span<uint8_t, 16> cid, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

status sd_card::stop_transmission(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

status sd_card::set_blocklen(uint32_t block_length, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

status sd_card::read_single_block(uint32_t address, std::span<uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	if (data.size() != 512)
	{
		return fail(component::spi_device, error_code::invalid_argument);
	}

	spi_cs_guard guard{ pin_cs };

	auto status = send_command(CMD17, address, DEFAULT_CRC, timeout, r1)
						.and_then([&] { return validate_response(r1); });

	if (!status)
	{
		return status;
	}

	if (r1.raw != 0)
	{
		return fail(component::spi_device, error_code::invalid_argument);
	}

	const auto start_tick = common::get_tick();
	const auto timeout_limit = pdMS_TO_TICKS(timeout);

	uint8_t rx = {};

	while (true)
	{
		rx = 0xFF;

		if (auto result = bus_.receive(std::span{ &rx, 1 }, timeout); !result)
			return result;

		if (rx == 0xFE)
		{
			break;
		}

		if (common::get_tick() - start_tick >= timeout_limit)
		{
			return fail(component::spi_device, error_code::timeout_error);
		}
	}

	std::fill(data.begin(), data.end(), 0xFF);
	if (auto response = bus_.receive(data, timeout); !response)
	{
		return response;
	}
	
	uint8_t crc[2] = {0xFF, 0xFF};
	if (auto response = bus_.receive(std::span{ crc, 2 }, timeout); !response)
	{
		return response;
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status sd_card::read_multiple_block(uint32_t address, std::span<uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

status sd_card::set_block_count(uint16_t number_of_blocks, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

status sd_card::set_wr_block_erase_count(uint32_t number_of_blocks, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

status sd_card::write_block(uint32_t address, std::span<const uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	if (data.size() != 512)
	{
		return fail(component::spi_device, error_code::invalid_argument);
	}

	spi_cs_guard guard{pin_cs};

	auto result = send_command(CMD24, address, DEFAULT_CRC, timeout, r1)
					.and_then([&] { return validate_response(r1); });
	if (!result)
	{
		return result;
	}
	if (r1.raw != 0)
	{
		return fail(component::spi_device, error_code::device_error);
	}

	constexpr uint8_t start_token = 0xFE;
	if (result = bus_.transmit(std::span{&start_token, 1}, timeout); !result)
	{
		return result;
	}
	if (result = bus_.transmit(data, timeout); !result)
	{
		return result;
	}
	constexpr uint8_t crc[2] = {0xFF, 0xFF};
	if (result = bus_.transmit(crc, timeout); !result)
	{
		return result;
	}

	uint8_t response = 0xFF;
	if (result = bus_.receive(std::span{&response, 1}, timeout); !result)
	{
		return result;
	}
	if ((response & 0x1Fu) != 0x05u)
	{
		return fail(component::spi_device, error_code::device_error);
	}

	const auto start_tick = common::get_tick();
	const auto timeout_limit = pdMS_TO_TICKS(timeout);
	do
	{
		response = 0xFF;
		if (result = bus_.receive(std::span{&response, 1}, timeout); !result)
		{
			return result;
		}
		if (response == 0xFF)
		{
			return {};
		}
	} while (common::get_tick() - start_tick < timeout_limit);

	return fail(component::spi_device, error_code::timeout_error);
}

//////////////////////////////////////////////////////////////////////////

status sd_card::write_multiple_block(uint32_t address, std::span<const uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	return fail(component::spi_device, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

status sd_card::app_cmd(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	spi_cs_guard guard{pin_cs};

	auto status = send_command(CMD55, DEFAULT_CRC, timeout, r1)
					.and_then([&]{ return validate_response(r1); })
					.and_then([&]{ return busy_pause(1); });

	return status;
}

//////////////////////////////////////////////////////////////////////////

status sd_card::read_ocr(r3_t &response, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::spi_device, error_code::not_initialized);
	}

	spi_cs_guard guard{pin_cs};
	return send_command(CMD58, DEFAULT_CRC, timeout, response)
				.and_then([&] { return validate_response(response.r1); });
}

//////////////////////////////////////////////////////////////////////////

status sd_card::send_command(uint8_t index, uint32_t crc, uint32_t timeout, r1_t &response)
{
	return send_command(index, 0, crc, timeout, response);
}

//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

status sd_card::send_command(uint8_t index, uint32_t argument, uint32_t crc, uint32_t timeout, r7_t &response)
{
	if (auto status = send_command(index, argument, crc, timeout, response.r1); !status)
	{
		return status;
	} else
	{
		response.value.raw = 0xFFFFFFFF;
		if (auto result = bus_.receive(std::span{ reinterpret_cast<uint8_t *>(&response.value.raw), sizeof(response.value.raw) }, timeout ); !result)
		{
			return result;
		}

		response.value.raw = utils::reverse_bytes(response.value.raw);

		return {};
	}
}

//////////////////////////////////////////////////////////////////////////

status sd_card::send_command(uint8_t index, uint32_t crc, uint32_t timeout, r3_t &response)
{
	if (auto status = send_command(index, crc, timeout, response.r1); !status)
	{
		return status;
	} else
	{
		response.ocr = 0xFFFFFFFF;
		if (auto result = bus_.receive(std::span{ reinterpret_cast<uint8_t *>(&response.ocr), sizeof(response.ocr) }, timeout ); !result)
		{
			return result;
		}

		response.ocr = utils::reverse_bytes(response.ocr);

		return {};
	}
}

//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

status sd_card::card_warm_up()
{
	// Set CS=1 and MOSI=1
	// Minimum for 74 ticks
	static constexpr uint8_t warmup_ticks = 100;
	return busy_pause(warmup_ticks);
}

//////////////////////////////////////////////////////////////////////////

status sd_card::busy_pause(uint8_t ticks)
{
	if (auto result = pin_cs.write(gpio::state::reset); !result)
	{
		return result;
	}

	uint8_t data[] = { 0xFF };

	for (uint8_t i = 0; i < ticks; i++)
	{
		auto status = bus_.transmit(data, MAX_DELAY);
		if (!status)
		{
			return status;
		}
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////
