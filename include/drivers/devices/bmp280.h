#pragma once
#include "drivers/i2c_device.h"

//	The BMP280 is a compact digital barometric pressure and temperature sensor designed by Bosch Sensortec
//	for mobile and microcontroller applications
// https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf

class bmp280
{
public:
	struct environmental_sample
	{
		float temperature_c = 0.f;
		float pressure_pa = 0.f;
	};

	enum class run_mode : uint8_t
	{
		sleep = 0,
		forced = 1, // 0b01 и 0b10 оба переводят в forced mode
		normal = 3,
	};

	enum class standby_time : uint8_t
	{
		ms_0_5 = 0,
		ms_62_5 = 1,
		ms_125 = 2,
		ms_250 = 3,
		ms_500 = 4,
		ms_1000 = 5,
		ms_2000 = 6,
		ms_4000 = 7,
	};

	enum class iir_filter : uint8_t
	{
		off = 0,
		coefficients_2 = 1,
		coefficients_4 = 2,
		coefficients_8 = 3,
		coefficients_16 = 4,
	};

	enum class oversampling : uint8_t
	{
		skipped = 0,
		x1 = 1,
		x2 = 2,
		x4 = 3,
		x8 = 4,
		x16 = 5,
	};

	struct config
	{
		run_mode mode = run_mode::normal;
		standby_time standby = standby_time::ms_0_5;
		iir_filter filter = iir_filter::coefficients_16;
		oversampling temp_oversample = oversampling::x16;
		oversampling press_oversample = oversampling::x16;
	};

public:
	explicit bmp280(i2c_device &transport, config config) : transport_(transport), config_(config), attempted_(false)
	{
	}

	status init();

	status start_measurement();
	result<bool> is_measuring();
	[[nodiscard]] bool measures_on_request() const { return config_.mode == run_mode::forced; }
	[[nodiscard]] uint32_t max_measurement_time_ms() const;
	result<environmental_sample> read_measurement();

private:
	[[nodiscard]] component get_component_type() const;
	int32_t compensate_temperature(int32_t raw_value);
	uint32_t compensate_pressure(int32_t raw_value);

private:
	struct calibration_data
	{
		uint16_t dig_t1 = 0;
		int16_t dig_t2 = 0;
		int16_t dig_t3 = 0;
		uint16_t dig_p1 = 0;
		int16_t dig_p2 = 0;
		int16_t dig_p3 = 0;
		int16_t dig_p4 = 0;
		int16_t dig_p5 = 0;
		int16_t dig_p6 = 0;
		int16_t dig_p7 = 0;
		int16_t dig_p8 = 0;
		int16_t dig_p9 = 0;
	};

private:
	i2c_device &transport_;
	config config_;
	bool attempted_;

	calibration_data calib_;
	int32_t t_fine_ = 0;
};
