#include "infrastructure/battery_service.h"
#include "application/battery_voltage.h"
#include "application/board_voltage.h"
#include "stm32f4xx_ll_adc.h"

battery_service::battery_service(config settings)
	: config_(settings), usb_power_(gpio_config{
		.pin = settings.usb_power_pin,
		.mode = GPIO_MODE_INPUT,
		.pull = GPIO_NOPULL
	})
{
}

//////////////////////////////////////////////////////////////////////////

status battery_service::init()
{
	if (initialized_)
	{
		return {};
	}
	const bool valid_settings = config_.sample_period_ms != 0 && config_.upper_ohms != 0 &&
	    config_.lower_ohms != 0 && config_.low_battery_mv < config_.recovery_battery_mv &&
	    config_.battery_input.channel <= ADC_CHANNEL_15 &&
	    config_.battery_input.sample_time == ADC_SAMPLETIME_480CYCLES &&
	    valid_battery_profile(config_.level_profile);
	if (!valid_settings)
	{
		return fail(component::board, error_code::invalid_config);
	}

	calibration_ = *VREFINT_CAL_ADDR;
	if (!supply_mv_from_vrefint(calibration_, calibration_))
	{
		return fail(component::adc, error_code::invalid_config);
	}
	if (!adc_initialized_)
	{
		if (auto started = adc_.init(); !started)
		{
			return started;
		}
		adc_initialized_ = true;
	}
	if (auto started = usb_power_.init(); !started)
	{
		return started;
	}
	initialized_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

result<battery_service::averaged_reading> battery_service::read_average(adc_config input)
{
	if (auto selected = adc_.select_channel(input); !selected)
	{
		return tl::make_unexpected(selected.error());
	}
	if (auto discarded = adc_.read_raw(); !discarded)
	{
		return tl::make_unexpected(discarded.error());
	}

	constexpr std::uint32_t count = 16;
	std::uint32_t sum = 0;
	std::array<std::uint16_t, count> samples{};
	std::uint16_t minimum = 4095;
	std::uint16_t maximum = 0;
	for (std::uint32_t index = 0; index < count; ++index)
	{
		const auto raw = adc_.read_raw();
		if (!raw)
		{
			return tl::make_unexpected(raw.error());
		}
		if (*raw == 0 || *raw >= 4095)
		{
			return fail(component::adc, error_code::device_error);
		}
		sum += *raw;
		samples[index] = *raw;
		minimum = *raw < minimum ? *raw : minimum;
		maximum = *raw > maximum ? *raw : maximum;
	}
	std::sort(samples.begin(), samples.end());
	constexpr std::uint32_t trim = count / 4;
	constexpr std::uint32_t retained = count - trim * 2;
	std::uint32_t trimmed_sum = 0;
	for (std::uint32_t index = trim; index < count - trim; ++index)
	{
		trimmed_sum += samples[index];
	}
	return averaged_reading{
		.mean = static_cast<std::uint16_t>((sum + count / 2) / count),
		.trimmed_mean = static_cast<std::uint16_t>((trimmed_sum + retained / 2) / retained),
		.minimum = minimum,
		.maximum = maximum
	};
}

//////////////////////////////////////////////////////////////////////////

status battery_service::sample_if_due(std::uint32_t now_ms)
{
	if (attempted_ && now_ms - last_attempt_ms_ < config_.sample_period_ms)
	{
		return {};
	}
	attempted_ = true;
	last_attempt_ms_ = now_ms;
	if (!valid_)
	{
		estimator_.reset();
	}
	valid_ = false;
	if (auto started = init(); !started)
	{
		return started;
	}

	const auto reference = read_average(adc_config{
		.channel = ADC_CHANNEL_VREFINT,
		.sample_time = ADC_SAMPLETIME_480CYCLES
	});
	if (!reference)
	{
		return tl::make_unexpected(reference.error());
	}
	const auto supply = supply_mv_from_vrefint(reference->mean, calibration_);
	if (!supply || *supply < 1800 || *supply > 3600)
	{
		return fail(component::adc, error_code::device_error);
	}
	const auto raw = read_average(config_.battery_input);
	if (!raw)
	{
		return tl::make_unexpected(raw.error());
	}
	const auto voltage = battery_mv_from_adc(raw->trimmed_mean, *supply, config_.upper_ohms, config_.lower_ohms);
	if (!voltage || *voltage < 2500 || *voltage > 4500)
	{
		return fail(component::adc, error_code::device_error);
	}
	const auto usb = usb_power_.read();
	if (!usb)
	{
		return tl::make_unexpected(usb.error());
	}

	low_battery_ = supply_is_low(*voltage, low_battery_, config_.low_battery_mv, config_.recovery_battery_mv);
	low_supply_ = supply_is_low(*supply, low_supply_, 3000, 3100);
	const auto estimate = estimator_.update(*voltage, now_ms, *usb == gpio::state::set,
	    config_.level_profile);
	latest_ = snapshot{
		.sampled_at_ms = now_ms,
		.battery_mv = estimate.mv,
		.supply_mv = *supply,
		.battery_raw = raw->mean,
		.battery_min_raw = raw->minimum,
		.battery_max_raw = raw->maximum,
		.vrefint_raw = reference->mean,
		.level_percent_estimate = estimate.percent,
		.usb_present = *usb == gpio::state::set,
		.low_battery = low_battery_,
		.low_supply = low_supply_
	};
	valid_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

result<battery_service::snapshot> battery_service::read() const
{
	if (!valid_ || !latest_)
	{
		return fail(component::board, error_code::not_ready);
	}
	return *latest_;
}
