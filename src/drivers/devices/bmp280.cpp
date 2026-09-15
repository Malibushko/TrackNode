#include "drivers/devices/bmp280.h"

#include "common/common.h"

#include <array>
#include <sys/types.h>

namespace
{
enum class reg : uint8_t
{
	chip_id = 0xD0,
	reset = 0xE0,
	status = 0xF3,
	ctrl_meas = 0xF4,
	config = 0xF5,
	pressure_msb = 0xF7,
	pressure_lsb = 0xF8,
	pressure_xlsb = 0xF9,
	temperature_msb = 0xFA,
	temperature_lsb = 0xFB,
	temperature_xlsb = 0xFC,
};
const uint32_t RW_TIMEOUT_DELAY = 100;

}

status bmp280::init()
{
	if (attempted_)
	{
		return fail(get_component_type(), error_code::already_initialized);
	}

	if (config_.temp_oversample == oversampling::skipped || config_.press_oversample == oversampling::skipped)
	{
		return fail(get_component_type(), error_code::invalid_config);
	}

	attempted_ = true;

	// config register (0xF5): [t_sb(3) | filter(3) | reserved(1) | spi3w_en(1)]
	const uint8_t config_reg =
	    (static_cast<uint8_t>(config_.standby) << 5) | (static_cast<uint8_t>(config_.filter) << 2);

	// ctrl_meas register (0xF4): [osrs_t(3) | osrs_p(3) | mode(2)]
	const uint8_t ctrl_meas_reg = (static_cast<uint8_t>(config_.temp_oversample) << 5) |
	                              (static_cast<uint8_t>(config_.press_oversample) << 2) |
	                              static_cast<uint8_t>(config_.mode);

	i2c_device &i2c_transport = transport_;

	std::array<uint8_t, 24> calibration_bytes{};
	auto calibration_status = i2c_transport.read_registers(0x88, std::span{ calibration_bytes }, RW_TIMEOUT_DELAY);
	if (!calibration_status)
	{
		return calibration_status;
	}

	const auto read_unsigned = [&calibration_bytes](size_t offset) -> uint16_t
	{
		return static_cast<uint16_t>(calibration_bytes[offset]) |
		       (static_cast<uint16_t>(calibration_bytes[offset + 1]) << 8);
	};

	const auto read_signed = [&read_unsigned](size_t offset) -> int16_t
	{
		const uint16_t value = read_unsigned(offset);
		return static_cast<int16_t>(value);
	};

	calib_ = calibration_data{
		.dig_t1 = read_unsigned(0),
		.dig_t2 = read_signed(2),
		.dig_t3 = read_signed(4),
		.dig_p1 = read_unsigned(6),
		.dig_p2 = read_signed(8),
		.dig_p3 = read_signed(10),
		.dig_p4 = read_signed(12),
		.dig_p5 = read_signed(14),
		.dig_p6 = read_signed(16),
		.dig_p7 = read_signed(18),
		.dig_p8 = read_signed(20),
		.dig_p9 = read_signed(22),
	};

	auto sleep_status = i2c_transport.write_register(static_cast<uint8_t>(reg::ctrl_meas),
	                                                 static_cast<uint8_t>(run_mode::sleep), RW_TIMEOUT_DELAY);

	if (!sleep_status)
	{
		return sleep_status;
	}

	const uint32_t started_at = HAL_GetTick();

	while (true)
	{
		auto measuring = is_measuring();

		if (!measuring)
		{
			return tl::make_unexpected(measuring.error());
		}

		if (!measuring.value())
		{
			break;
		}

		if (HAL_GetTick() - started_at >= RW_TIMEOUT_DELAY * 2)
		{
			return from_hal(get_component_type(), HAL_TIMEOUT);
		}

		common::delay(1);
	}

	return i2c_transport.write_register(static_cast<uint8_t>(reg::config), config_reg, RW_TIMEOUT_DELAY)
	    .and_then(
	        [&]() -> status
	        {
		        return i2c_transport.write_register(static_cast<uint8_t>(reg::ctrl_meas), ctrl_meas_reg,
		                                            RW_TIMEOUT_DELAY);
	        });
}

status bmp280::start_measurement()
{
	if (config_.mode == run_mode::normal)
	{
		return {};
	}

	if (config_.mode == run_mode::sleep)
	{
		return fail(component::i2c_device, error_code::invalid_config);
	}

	i2c_device &i2c_transport = transport_;

	const uint8_t ctrl_meas_reg = (static_cast<uint8_t>(config_.temp_oversample) << 5) |
	                              (static_cast<uint8_t>(config_.press_oversample) << 2) |
	                              static_cast<uint8_t>(config_.mode);

	auto write_result = i2c_transport.write_register(static_cast<uint8_t>(reg::ctrl_meas), ctrl_meas_reg);
	if (!write_result)
	{
		return tl::unexpected(write_result.error());
	}

	return {};
}

result<bool> bmp280::is_measuring()
{
	i2c_device &i2c_transport = transport_;

	uint8_t statusValue{};
	if (auto result = i2c_transport.read_registers(static_cast<uint8_t>(reg::status), std::span{ &statusValue, 1 }, RW_TIMEOUT_DELAY); !result)
	{
		return tl::make_unexpected(result.error());
	}

	return statusValue & (1 << 3);
}

result<bmp280::environmental_sample> bmp280::read_measurement()
{
	i2c_device &i2c_transport = transport_;

	std::array<uint8_t, 6> raw{};
	if (auto result =
	        i2c_transport.read_registers(static_cast<uint8_t>(reg::pressure_msb), std::span{ raw }, RW_TIMEOUT_DELAY);
	    !result)
	{
		return tl::make_unexpected(result.error());
	}

	int32_t adc_p = (static_cast<int32_t>(raw[0]) << 12) | (static_cast<int32_t>(raw[1]) << 4) |
	                (static_cast<int32_t>(raw[2]) >> 4);

	int32_t adc_t = (static_cast<int32_t>(raw[3]) << 12) | (static_cast<int32_t>(raw[4]) << 4) |
	                (static_cast<int32_t>(raw[5]) >> 4);

	environmental_sample sample;
	// $NOTE: this read should always come first because of t_fine
	sample.temperature_c = compensate_temperature(adc_t) / 100.0f;
	sample.pressure_pa = compensate_pressure(adc_p) / 256.0f;

	return sample;
}

component bmp280::get_component_type() const
{
	return component::i2c_device;
}

//
// Taken from datasheet
//

int32_t bmp280::compensate_temperature(int32_t adc_t)
{
	int32_t var1, var2, t;
	var1 = ((((adc_t >> 3) - (static_cast<int32_t>(calib_.dig_t1) << 1))) * static_cast<int32_t>(calib_.dig_t2)) >> 11;
	var2 = (((((adc_t >> 4) - static_cast<int32_t>(calib_.dig_t1)) *
	          ((adc_t >> 4) - static_cast<int32_t>(calib_.dig_t1))) >>
	         12) *
	        static_cast<int32_t>(calib_.dig_t3)) >>
	       14;
	t_fine_ = var1 + var2;
	t = (t_fine_ * 5 + 128) >> 8;
	return t;
}

uint32_t bmp280::compensate_pressure(int32_t adc_p)
{
	int64_t var1, var2, p;
	var1 = static_cast<int64_t>(t_fine_) - 128000;
	var2 = var1 * var1 * static_cast<int64_t>(calib_.dig_p6);
	var2 = var2 + ((var1 * static_cast<int64_t>(calib_.dig_p5)) << 17);
	var2 = var2 + (static_cast<int64_t>(calib_.dig_p4) << 35);
	var1 = ((var1 * var1 * static_cast<int64_t>(calib_.dig_p3)) >> 8) +
	       ((var1 * static_cast<int64_t>(calib_.dig_p2)) << 12);
	var1 = ((static_cast<int64_t>(1) << 47) + var1) * static_cast<int64_t>(calib_.dig_p1) >> 33;

	if (var1 == 0)
	{
		return 0;
	}

	p = 1048576 - adc_p;
	p = (((p << 31) - var2) * 3125) / var1;
	var1 = (static_cast<int64_t>(calib_.dig_p9) * (p >> 13) * (p >> 13)) >> 25;
	var2 = (static_cast<int64_t>(calib_.dig_p8) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (static_cast<int64_t>(calib_.dig_p7) << 4);
	return static_cast<uint32_t>(p);
}
