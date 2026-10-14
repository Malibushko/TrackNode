#include "drivers/adc.h"

status adc::init()
{
	if (initialized_)
	{
		return fail(component::adc, error_code::already_initialized);
	}

	const bool internal_vref = config_.channel == ADC_CHANNEL_VREFINT;
	if ((!internal_vref && config_.channel > ADC_CHANNEL_15) ||
	    !IS_ADC_SAMPLE_TIME(config_.sample_time))
	{
		return fail(component::adc, error_code::invalid_config);
	}

	if (internal_vref && config_.sample_time != ADC_SAMPLETIME_480CYCLES)
	{
		return fail(component::adc, error_code::invalid_config);
	}

	__HAL_RCC_ADC1_CLK_ENABLE();

	handle_.Instance = ADC1;
	handle_.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
	handle_.Init.Resolution = ADC_RESOLUTION_12B;
	handle_.Init.ScanConvMode = DISABLE;
	handle_.Init.ContinuousConvMode = DISABLE;
	handle_.Init.DiscontinuousConvMode = DISABLE;
	handle_.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
	handle_.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	handle_.Init.NbrOfConversion = 1;
	handle_.Init.EOCSelection = ADC_EOC_SINGLE_CONV;

	auto status = from_hal(component::adc, HAL_ADC_Init(&handle_));
	if (!status)
	{
		return status;
	}

	initialized_ = true;
	status = select_channel(config_);
	initialized_ = static_cast<bool>(status);
	return status;
}

//////////////////////////////////////////////////////////////////////////

status adc::select_channel(adc_config config)
{
	if (!initialized_)
	{
		return fail(component::adc, error_code::not_initialized);
	}

	const bool internal_vref = config.channel == ADC_CHANNEL_VREFINT;
	const bool valid_channel = internal_vref || config.channel <= ADC_CHANNEL_15;
	const bool valid_sample_time = IS_ADC_SAMPLE_TIME(config.sample_time) &&
	    (!internal_vref || config.sample_time == ADC_SAMPLETIME_480CYCLES);
	if (!valid_channel || !valid_sample_time)
	{
		return fail(component::adc, error_code::invalid_config);
	}

	if (!internal_vref)
	{
		gpio configured_pin{gpio_config{
			.pin = config.pin,
			.mode = GPIO_MODE_ANALOG
		}};
		if (auto configured = configured_pin.init(); !configured)
		{
			return configured;
		}
	}

	ADC_ChannelConfTypeDef channel{};
	channel.Channel = config.channel;
	channel.Rank = 1;
	channel.SamplingTime = config.sample_time;

	auto status = from_hal(component::adc, HAL_ADC_ConfigChannel(&handle_, &channel));
	if (status && internal_vref)
	{
		for (std::uint32_t remaining = SystemCoreClock / 100000u + 1u;
		     remaining != 0; --remaining)
		{
			__NOP();
		}
	}

	if (status)
	{
		config_ = config;
	}

	return status;
}

//////////////////////////////////////////////////////////////////////////

result<uint16_t> adc::read_raw(uint32_t timeout_ms)
{
	if (!initialized_)
	{
		return fail(component::adc, error_code::not_initialized);
	}

	auto status = from_hal(component::adc, HAL_ADC_Start(&handle_));
	if (!status)
	{
		return tl::make_unexpected(status.error());
	}

	status = from_hal(component::adc, HAL_ADC_PollForConversion(&handle_, timeout_ms));
	if (!status)
	{
		HAL_ADC_Stop(&handle_);
		return tl::make_unexpected(status.error());
	}

	const auto value = static_cast<uint16_t>(HAL_ADC_GetValue(&handle_));

	status = from_hal(component::adc, HAL_ADC_Stop(&handle_));
	if (!status)
	{
		return tl::make_unexpected(status.error());
	}

	return value;
}

//////////////////////////////////////////////////////////////////////////
