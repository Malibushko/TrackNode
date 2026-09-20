#include "drivers/devices/sd_card.h"

status sd_card::init()
{
	if (initialized_)
	{
		return fail(component::i2c_device, error_code::already_initialized);
	}
	
	return {};
}

status sd_card::go_idle_state(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::app_send_op_cond(bool host_capacity_support, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::send_op_cond(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::send_if_cond(uint8_t voltage_supplied, uint8_t check_pattern, r7_t &response, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::send_csd(std::span<uint8_t, 16> csd, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::send_cid(std::span<uint8_t, 16> cid, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::stop_transmission(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::set_blocklen(uint32_t block_length, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::read_single_block(uint32_t address, std::span<uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::read_multiple_block(uint32_t address, std::span<uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::set_block_count(uint16_t number_of_blocks, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::set_wr_block_erase_count(uint32_t number_of_blocks, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::write_block(uint32_t address, std::span<const uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::write_multiple_block(uint32_t address, std::span<const uint8_t> data, r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::app_cmd(r1_t &r1, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}

status sd_card::read_ocr(r3_t &response, uint32_t timeout)
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}

	return fail(component::i2c_device, error_code::not_implemented);
}