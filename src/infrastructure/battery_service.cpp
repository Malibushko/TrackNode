#include "infrastructure/battery_service.h"
#include "application/battery_voltage.h"
#include "application/board_voltage.h"
#include "stm32f4xx_ll_adc.h"

namespace
{
	constexpr std::size_t series_trim_count = 3;
	constexpr std::uint16_t max_series_spread_mv = 60;
}

battery_service::battery_service(config settings)
	: config_(settings), usb_power_(gpio_config{
		.pin = settings.usb_power_pin,
		.mode = GPIO_MODE_INPUT,
		.pull = GPIO_NOPULL
	}), estimator_(settings.estimator)
{
}

//////////////////////////////////////////////////////////////////////////

status battery_service::init()
{
	if (initialized_)
	{
		return {};
	}
	const bool valid_settings = config_.upper_ohms != 0 &&
	    config_.lower_ohms != 0 && config_.low_battery_mv < config_.recovery_battery_mv &&
	    config_.battery_input.channel <= ADC_CHANNEL_15 &&
	    config_.battery_input.sample_time == ADC_SAMPLETIME_480CYCLES &&
	    valid_battery_profile(config_.level_profile) && config_.sample_interval_ms != 0 &&
	    config_.estimator.rate_period_ms != 0;
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

result<battery_service::snapshot> battery_service::read_once(std::uint32_t now_ms)
{
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
	low_supply_ = supply_is_low(*supply, low_supply_, 3000, 3100);
	return snapshot{
		.sampled_at_ms = now_ms,
		.battery_mv = *voltage,
		.supply_mv = *supply,
		.battery_raw = raw->mean,
		.battery_min_raw = raw->minimum,
		.battery_max_raw = raw->maximum,
		.vrefint_raw = reference->mean,
		.reading_valid = true,
		.usb_present = latest_.usb_present,
		.low_supply = low_supply_
	};
}

//////////////////////////////////////////////////////////////////////////

result<battery_service::snapshot> battery_service::read() const
{
	if (!latest_.reading_valid)
	{
		return fail(component::board, error_code::not_ready);
	}
	return latest_;
}

//////////////////////////////////////////////////////////////////////////

result<bool> battery_service::poll_power()
{
	if (auto started = init(); !started)
	{
		return tl::make_unexpected(started.error());
	}
	const auto usb = usb_power_.read();
	if (!usb)
	{
		return tl::make_unexpected(usb.error());
	}
	const bool present = *usb == gpio::state::set;
	const bool changed = present != latest_.usb_present;
	if (changed)
	{
		latest_.usb_present = present;
		sampling_ = false;
		latest_.update_pending = true;
		++latest_.revision;
		estimator_.interrupt_confirmation();
	}
	return changed;
}

//////////////////////////////////////////////////////////////////////////

void battery_service::set_pending(bool value)
{
	if (latest_.update_pending != value)
	{
		latest_.update_pending = value;
		++latest_.revision;
	}
}

//////////////////////////////////////////////////////////////////////////

void battery_service::begin_series(std::uint32_t now_ms, bool under_load)
{
	series_count_ = 0;
	low_count_ = 0;
	recovery_count_ = 0;
	series_loaded_ = under_load;
	sampling_ = true;
	retry_ = false;
	last_sample_ms_ = now_ms - config_.sample_interval_ms;
	set_pending(true);
}

//////////////////////////////////////////////////////////////////////////

status battery_service::sample_step(std::uint32_t now_ms, bool under_load)
{
	if (!sampling_ || now_ms - last_sample_ms_ < config_.sample_interval_ms)
	{
		return {};
	}
	last_sample_ms_ = now_ms;
	series_loaded_ = series_loaded_ || under_load;
	const auto value = read_once(now_ms);
	if (!value)
	{
		sampling_ = false;
		retry_ = true;
		latest_.unstable = true;
		latest_.quality = 3;
		set_pending(false);
		++latest_.revision;
		estimator_.interrupt_confirmation();
		return tl::make_unexpected(value.error());
	}
	low_count_ = value->battery_mv <= config_.low_battery_mv ? low_count_ + 1 : 0;
	recovery_count_ = value->battery_mv >= config_.recovery_battery_mv ? recovery_count_ + 1 : 0;
	const bool was_low = low_battery_;
	if (low_count_ >= 3)
	{
		low_battery_ = true;
	}
	if (recovery_count_ >= 3)
	{
		low_battery_ = false;
	}
	if (was_low != low_battery_)
	{
		latest_.low_battery = low_battery_;
		++latest_.revision;
	}
	series_[series_count_++] = *value;
	if (series_count_ == series_.size())
	{
		finish_series();
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

void battery_service::finish_series()
{
	sampling_ = false;
	const auto completed_at = series_.back().sampled_at_ms;
	std::sort(series_.begin(), series_.end(), [](const snapshot &left, const snapshot &right)
	{
		return left.battery_mv < right.battery_mv;
	});
	const auto retained_begin = series_trim_count;
	const auto retained_end = series_.size() - series_trim_count;
	const auto retained_count = retained_end - retained_begin;
	const auto series_spread_mv = series_[retained_end - 1].battery_mv - series_[retained_begin].battery_mv;
	const bool unstable = series_spread_mv > max_series_spread_mv;
	if (unstable)
	{
		latest_.quality = 3;
		latest_.unstable = true;
		latest_.update_pending = false;
		retry_ = true;
		++latest_.revision;
		estimator_.interrupt_confirmation();
		return;
	}
	std::uint32_t sum = 0;
	for (std::size_t index = retained_begin; index < retained_end; ++index)
	{
		sum += series_[index].battery_mv;
	}
	const auto voltage = static_cast<std::uint16_t>((sum + retained_count / 2) / retained_count);
	const auto estimate = estimator_.update(voltage, completed_at, latest_.usb_present, config_.level_profile);
	const auto revision = latest_.revision + 1;
	latest_ = series_[series_.size() / 2];
	latest_.sampled_at_ms = completed_at;
	latest_.revision = revision;
	latest_.battery_mv = voltage;
	latest_.level_percent_estimate = estimate.percent;
	latest_.quality = series_loaded_ ? 2 : 1;
	latest_.low_battery = low_battery_;
	latest_.reanchored = estimate.reanchored;
	retry_ = estimate.confirming;
	for (const auto &value : series_)
	{
		latest_.battery_min_raw = std::min(latest_.battery_min_raw, value.battery_min_raw);
		latest_.battery_max_raw = std::max(latest_.battery_max_raw, value.battery_max_raw);
	}
}

//////////////////////////////////////////////////////////////////////////

void battery_service::mark_loaded()
{
	series_loaded_ = true;
	if (!sampling_ && latest_.quality == 1)
	{
		latest_.quality = 2;
		++latest_.revision;
	}
}

//////////////////////////////////////////////////////////////////////////

bool battery_service::sampling() const
{
	return sampling_;
}

//////////////////////////////////////////////////////////////////////////

bool battery_service::needs_retry() const
{
	return retry_;
}

//////////////////////////////////////////////////////////////////////////

battery_service::snapshot battery_service::current() const
{
	return latest_;
}
