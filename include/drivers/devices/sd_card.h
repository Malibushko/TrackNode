#pragma once
#include "core/result.h"
#include "drivers/spi_bus.h"
#include "drivers/spi_device.h"

class sd_card : spi_device
{
private:
	static constexpr uint32_t MAX_DELAY = 100;

public:
	using r1_t = uint8_t;

	struct r3_t
	{
		r1_t r1;
		uint32_t ocr;
	};

	union r7_value_t
	{
		uint32_t raw;
		struct
		{
			uint32_t check_pattern    : 8;
			uint32_t voltage_accepted : 4;
			uint32_t reserved         : 16;
			uint32_t command_version  : 4;
		} bits;
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
    status send_if_cond(uint8_t voltage_supplied, uint8_t check_pattern, r7_t &response, uint32_t timeout = MAX_DELAY);
    status send_csd(std::span<uint8_t, 16> csd, r1_t &r1, uint32_t timeout = MAX_DELAY);
    status send_cid(std::span<uint8_t, 16> cid, r1_t &r1, uint32_t timeout = MAX_DELAY);

    status stop_transmission(r1_t &r1, uint32_t timeout = MAX_DELAY);

    status set_blocklen(uint32_t block_length, r1_t &r1, uint32_t timeout = MAX_DELAY);

    status read_single_block(uint32_t address, std::span<uint8_t> data, r1_t &r1, uint32_t timeout = MAX_DELAY);
    status read_multiple_block(uint32_t address, std::span<uint8_t> data, r1_t &r1, uint32_t timeout = MAX_DELAY);

    status set_block_count(uint16_t number_of_blocks, r1_t &r1, uint32_t timeout = MAX_DELAY);
    status set_wr_block_erase_count(uint32_t number_of_blocks, r1_t &r1, uint32_t timeout = MAX_DELAY);

    status write_block(uint32_t address, std::span<const uint8_t> data, r1_t &r1, uint32_t timeout = MAX_DELAY);
    status write_multiple_block(uint32_t address, std::span<const uint8_t> data, r1_t &r1, uint32_t timeout = MAX_DELAY);

    status app_cmd(r1_t &r1, uint32_t timeout = MAX_DELAY);
    status read_ocr(r3_t &response, uint32_t timeout = MAX_DELAY);

private:

private:
	bool initialized_{ false };
};