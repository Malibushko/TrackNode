#include "infrastructure/board_state_service.h"
#include "application/board_voltage.h"
#include "stm32f4xx_ll_adc.h"

status board_state_service::init()
{
	if (initialized_) return {};
	if (config_.sample_period_ms == 0 || config_.low_voltage_mv >= config_.recovery_voltage_mv)
		return fail(component::board, error_code::invalid_config);
	calibration_ = *VREFINT_CAL_ADDR;
	if (!supply_mv_from_vrefint(calibration_, calibration_))
		return fail(component::adc, error_code::invalid_config);
	auto started = adc_.init();
	initialized_ = started.has_value();
	return started;
}

status board_state_service::sample_if_due(std::uint32_t now_ms)
{
	if (attempted_ && now_ms - last_attempt_ms_ < config_.sample_period_ms) return {};
	attempted_ = true;
	last_attempt_ms_ = now_ms;
	valid_ = false;
	if (auto started = init(); !started) return started;

	constexpr std::uint32_t count = 16;
	std::uint32_t sum = 0;
	for (std::uint32_t index = 0; index < count; ++index)
	{
		const auto raw = adc_.read_raw();
		if (!raw) return tl::make_unexpected(raw.error());
		if (*raw == 0 || *raw >= 4095)
			return fail(component::adc, error_code::device_error);
		sum += *raw;
	}
	const auto raw = static_cast<std::uint16_t>((sum + count / 2) / count);
	const auto mv = supply_mv_from_vrefint(raw, calibration_);
	if (!mv) return fail(component::adc, error_code::device_error);
	low_voltage_ = supply_is_low(*mv, low_voltage_, config_.low_voltage_mv,
	                            config_.recovery_voltage_mv);
	latest_ = snapshot{now_ms, *mv, raw, low_voltage_};
	valid_ = true;
	return {};
}

result<board_state_service::snapshot> board_state_service::read() const
{
	if (!valid_ || !latest_) return fail(component::board, error_code::not_ready);
	return *latest_;
}
