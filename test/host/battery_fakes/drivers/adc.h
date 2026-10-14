#pragma once
#include "drivers/gpio.h"
#include <vector>

constexpr std::uint32_t ADC_CHANNEL_VREFINT = 17;
constexpr std::uint32_t ADC_CHANNEL_1 = 1;
constexpr std::uint32_t ADC_CHANNEL_15 = 15;
constexpr std::uint32_t ADC_SAMPLETIME_480CYCLES = 480;
struct adc_config
{
	::pin pin{};
	std::uint32_t channel{};
	std::uint32_t sample_time{};
};
class adc
{
public:
	explicit adc(adc_config settings) : channel_(settings.channel) {}
	status init()
	{
		++init_count;
		return {};
	}
	status select_channel(adc_config settings)
	{
		channel_ = settings.channel;
		selected_channels.push_back(channel_);
		return {};
	}
	result<std::uint16_t> read_raw()
	{
		++read_count;
		if (channel_ == fail_channel)
		{
			return fail(component::adc, error_code::timeout_error);
		}
		if (channel_ == ADC_CHANNEL_VREFINT)
		{
			return reference_raw;
		}
		if (!battery_samples.empty())
		{
			return battery_samples[battery_index++ % battery_samples.size()];
		}
		return battery_raw;
	}
	inline static std::uint16_t reference_raw = 1500;
	inline static std::uint16_t battery_raw = 2470;
	inline static unsigned read_count = 0;
	inline static unsigned init_count = 0;
	inline static std::uint32_t fail_channel = UINT32_MAX;
	inline static std::vector<std::uint32_t> selected_channels;
	inline static std::vector<std::uint16_t> battery_samples;
	inline static std::size_t battery_index = 0;

private:
	std::uint32_t channel_;
};
