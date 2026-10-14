#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

struct battery_level_point
{
	std::uint16_t mv;
	std::uint8_t percent;
};

inline constexpr std::array<battery_level_point, 9> provisional_lipo_profile{{
	{.mv = 3300,
	 .percent = 0},
	{.mv = 3500,
	 .percent = 5},
	{.mv = 3600,
	 .percent = 10},
	{.mv = 3700,
	 .percent = 20},
	{.mv = 3800,
	 .percent = 40},
	{.mv = 3900,
	 .percent = 60},
	{.mv = 4000,
	 .percent = 75},
	{.mv = 4100,
	 .percent = 90},
	{.mv = 4200,
	 .percent = 100}
}};

inline bool valid_battery_profile(std::span<const battery_level_point> curve)
{
	if (curve.size() < 2 || curve.front().percent != 0 || curve.back().percent != 100)
	{
		return false;
	}
	for (std::size_t index = 1; index < curve.size(); ++index)
	{
		if (curve[index].mv <= curve[index - 1].mv ||
		    curve[index].percent < curve[index - 1].percent || curve[index].percent > 100)
		{
			return false;
		}
	}
	return true;
}

inline std::optional<std::uint16_t> battery_mv_from_adc(std::uint16_t raw,
    std::uint16_t supply_mv, std::uint32_t upper_ohms, std::uint32_t lower_ohms)
{
	if (raw == 0 || raw >= 4095 || supply_mv == 0 || upper_ohms == 0 || lower_ohms == 0)
	{
		return std::nullopt;
	}

	const std::uint64_t numerator = static_cast<std::uint64_t>(raw) * supply_mv *
	    (static_cast<std::uint64_t>(upper_ohms) + lower_ohms);
	const std::uint64_t denominator = 4095ull * lower_ohms;
	const auto mv = (numerator + denominator / 2) / denominator;
	if (mv > UINT16_MAX)
	{
		return std::nullopt;
	}
	return static_cast<std::uint16_t>(mv);
}

inline std::uint8_t estimate_battery_level(std::uint16_t mv,
    std::span<const battery_level_point> curve = provisional_lipo_profile)
{
	if (!valid_battery_profile(curve))
	{
		return 0;
	}
	if (mv <= curve.front().mv)
	{
		return curve.front().percent;
	}
	for (std::size_t index = 1; index < curve.size(); ++index)
	{
		const auto lower = curve[index - 1];
		const auto upper = curve[index];
		if (mv <= upper.mv)
		{
			const std::uint32_t span = upper.mv - lower.mv;
			const std::uint32_t level = lower.percent +
			    ((mv - lower.mv) * (upper.percent - lower.percent) + span / 2) / span;
			return static_cast<std::uint8_t>(level);
		}
	}
	return curve.back().percent;
}
