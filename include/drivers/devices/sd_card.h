#pragma once
#include "core/result.h"
#include "drivers/spi_bus.h"
#include "drivers/spi_device.h"

class sd_card : spi_device
{
private:
	static constexpr uint32_t MAX_DELAY = 100;
	static constexpr uint32_t WRITE_TIMEOUT = 1000;

public:
	struct r1_value_t
	{
		uint8_t raw = 0xFF;

		[[nodiscard]] bool received() const { return (raw & 0x80u) == 0; }
		[[nodiscard]] bool parameter_error() const { return (raw & 0x40u) != 0; }
		[[nodiscard]] bool address_error() const { return (raw & 0x20u) != 0; }
		[[nodiscard]] bool erase_sequence_error() const { return (raw & 0x10u) != 0; }
		[[nodiscard]] bool crc_error() const { return (raw & 0x08u) != 0; }
		[[nodiscard]] bool illegal_command() const { return (raw & 0x04u) != 0; }
		[[nodiscard]] bool erase_reset() const { return (raw & 0x02u) != 0; }
		[[nodiscard]] bool in_idle_state() const { return (raw & 0x01u) != 0; }
	};

	using r1_t = r1_value_t;

	struct r3_t
	{
		r1_t r1;
		uint32_t ocr;
	};

	struct r7_value_t
	{
		uint32_t raw = 0;

		[[nodiscard]] uint8_t check_pattern() const { return static_cast<uint8_t>(raw & 0xFFu); }
		[[nodiscard]] uint8_t voltage_accepted() const { return static_cast<uint8_t>((raw >> 8) & 0x0Fu); }
		[[nodiscard]] uint16_t reserved() const { return static_cast<uint16_t>((raw >> 12) & 0xFFFFu); }
		[[nodiscard]] uint8_t command_version() const { return static_cast<uint8_t>((raw >> 28) & 0x0Fu); }
	};

	static_assert(sizeof(r7_value_t) == sizeof(uint32_t));

	struct r7_t
	{
		r1_t r1;
		r7_value_t value;
	};
public:
	using spi_device::spi_device;

	status init();

	status go_idle_state(r1_t &r1, uint32_t timeout = MAX_DELAY);

    status app_send_op_cond(bool host_capacity_support, r1_t &r1, uint32_t timeout = MAX_DELAY);

	status send_op_cond(r1_t &r1, uint32_t timeout = MAX_DELAY);
    status send_if_cond(uint32_t timeout = MAX_DELAY);
    status send_csd(std::span<uint8_t, 16> csd, r1_t &r1, uint32_t timeout = MAX_DELAY);
    status send_cid(std::span<uint8_t, 16> cid, r1_t &r1, uint32_t timeout = MAX_DELAY);

    status stop_transmission(r1_t &r1, uint32_t timeout = MAX_DELAY);

    status set_blocklen(uint32_t block_length, r1_t &r1, uint32_t timeout = MAX_DELAY);

    status read_single_block(uint32_t address, std::span<uint8_t> data, r1_t &r1, uint32_t timeout = MAX_DELAY);
    status read_multiple_block(uint32_t address, std::span<uint8_t> data, r1_t &r1, uint32_t timeout = MAX_DELAY);

    status set_block_count(uint16_t number_of_blocks, r1_t &r1, uint32_t timeout = MAX_DELAY);
    status set_wr_block_erase_count(uint32_t number_of_blocks, r1_t &r1, uint32_t timeout = MAX_DELAY);

    status write_block(uint32_t address, std::span<const uint8_t> data, r1_t &r1, uint32_t timeout = WRITE_TIMEOUT);
    status write_multiple_block(uint32_t address, std::span<const uint8_t> data, r1_t &r1, uint32_t timeout = MAX_DELAY);

    status app_cmd(r1_t &r1, uint32_t timeout = MAX_DELAY);
    status read_ocr(r3_t &response, uint32_t timeout = MAX_DELAY);

private:
	status send_command(uint8_t index, uint32_t crc, uint32_t timeout, r1_t& response);
	status send_command(uint8_t index, uint32_t argument, uint32_t crc, uint32_t timeout, r1_t &response);
	status send_command(uint8_t index, uint32_t argument, uint32_t crc, uint32_t timeout, r7_t &response);
	status send_command(uint8_t index, uint32_t crc, uint32_t timeout, r3_t &response);

	[[nodiscard]] status validate_response(r1_t response) const;
	status card_warm_up();
	status busy_pause(uint8_t ticks);
};
