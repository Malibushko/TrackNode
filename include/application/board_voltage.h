#pragma once

#include <cstdint>
#include <optional>

// F411 factory VREFINT calibration is a 12-bit ADC reading at VDDA=3300 mV.
inline std::optional<std::uint16_t> supply_mv_from_vrefint(std::uint16_t raw, std::uint16_t calibration)
{
	if (raw == 0 || raw >= 4095 || calibration == 0 || calibration >= 4095)
		return std::nullopt;

	const std::uint32_t mv = (3300u * calibration + raw / 2u) / raw;
	if (mv > UINT16_MAX)
		return std::nullopt;

	return static_cast<std::uint16_t>(mv);
}

inline bool supply_is_low(std::uint16_t mv, bool was_low, std::uint16_t low_mv, std::uint16_t recovery_mv)
{
	return was_low ? mv < recovery_mv : mv <= low_mv;
}
