#include "drivers/adc.h"

status adc::init()
{
	if (initialized_)
	{
		return fail(component::adc, error_code::already_initialized);
	}

	if (config_.channel > ADC_CHANNEL_15 || !IS_ADC_SAMPLE_TIME(config_.sample_time))
	{
		return fail(component::adc, error_code::invalid_config);
	}

	gpio_config pin_config{};
	pin_config.pin = config_.pin;
	pin_config.mode = GPIO_MODE_ANALOG;

	gpio configured_pin{ pin_config };

	auto status = configured_pin.init();
	if (!status)
	{
		return status;
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

	status = from_hal(component::adc, HAL_ADC_Init(&handle_));
	if (!status)
	{
		return status;
	}

	ADC_ChannelConfTypeDef channel{};
	channel.Channel = config_.channel;
	channel.Rank = 1;
	channel.SamplingTime = config_.sample_time;

	status = from_hal(component::adc, HAL_ADC_ConfigChannel(&handle_, &channel));

	initialized_ = static_cast<bool>(status);

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
