#pragma once
#include "core/result.h"

constexpr std::uint32_t ADC_CHANNEL_VREFINT = 17;
constexpr std::uint32_t ADC_SAMPLETIME_480CYCLES = 480;
struct adc_config
{
	std::uint32_t channel;
	std::uint32_t sample_time;
};
class adc
{
public:
	explicit adc(adc_config) {}
	status init() { ++init_count; return {}; }
	result<std::uint16_t> read_raw()
	{
		++read_count;
		if (fail_read) return fail(component::adc, error_code::timeout_error);
		return raw;
	}
	inline static std::uint16_t raw = 1500;
	inline static unsigned read_count = 0;
	inline static unsigned init_count = 0;
	inline static bool fail_read = false;
};
