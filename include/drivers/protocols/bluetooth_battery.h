#pragma once

#include "drivers/protocols/bluetooth_measurement.h"

namespace bluetooth_battery
{
inline constexpr std::size_t wire_size = 20;
inline constexpr std::uint32_t lifetime_ms = 15000;
enum flag : std::uint8_t
{
	reading_valid = 1,
	usb_present = 2,
	low_battery = 4
};

struct snapshot
{
	std::uint8_t flags = 0;
	std::uint8_t level = 0;
	std::uint16_t battery_mv = 0;
	std::uint16_t supply_mv = 0;
	std::uint16_t battery_raw = 0;
	std::uint16_t vrefint_raw = 0;
	std::uint16_t minimum_raw = 0;
	std::uint16_t maximum_raw = 0;
	std::uint32_t age_ms = 0;
};

inline bool valid(const snapshot &value)
{
	if ((value.flags & ~std::uint8_t{7}) != 0 || value.level > 100)
	{
		return false;
	}
	if (!(value.flags & reading_valid))
	{
		return value.level == 0 && value.battery_mv == 0 && value.supply_mv == 0 &&
		    value.battery_raw == 0 && value.vrefint_raw == 0 && value.minimum_raw == 0 && value.maximum_raw == 0;
	}
	return value.battery_mv >= 2500 && value.battery_mv <= 4500 &&
	    value.supply_mv >= 1800 && value.supply_mv <= 3600 &&
	    value.minimum_raw > 0 && value.minimum_raw <= value.battery_raw &&
	    value.battery_raw <= value.maximum_raw && value.maximum_raw < 4095 &&
	    value.vrefint_raw > 0 && value.vrefint_raw < 4095;
}

inline std::array<std::uint8_t, wire_size> encode(const snapshot &value)
{
	using bluetooth_measurement::put;
	std::array<std::uint8_t, wire_size> bytes{};
	bytes[0] = 1;
	bytes[1] = value.flags;
	bytes[2] = value.level;
	put(bytes, 4, value.battery_mv);
	put(bytes, 6, value.supply_mv);
	put(bytes, 8, value.battery_raw);
	put(bytes, 10, value.vrefint_raw);
	put(bytes, 12, value.minimum_raw);
	put(bytes, 14, value.maximum_raw);
	put(bytes, 16, value.age_ms);
	return bytes;
}

inline std::optional<snapshot> decode(std::span<const std::uint8_t> bytes)
{
	if (bytes.size() != wire_size || bytes[0] != 1 || bytes[3] != 0)
	{
		return std::nullopt;
	}
	using bluetooth_measurement::get;
	snapshot value{
		.flags = bytes[1],
		.level = bytes[2],
		.battery_mv = get<std::uint16_t>(bytes, 4),
		.supply_mv = get<std::uint16_t>(bytes, 6),
		.battery_raw = get<std::uint16_t>(bytes, 8),
		.vrefint_raw = get<std::uint16_t>(bytes, 10),
		.minimum_raw = get<std::uint16_t>(bytes, 12),
		.maximum_raw = get<std::uint16_t>(bytes, 14),
		.age_ms = get<std::uint32_t>(bytes, 16)
	};
	if (!valid(value))
	{
		return std::nullopt;
	}
	return value;
}

inline snapshot aged(snapshot value, std::uint32_t elapsed)
{
	value.age_ms = bluetooth_measurement::add_age(value.age_ms, elapsed);
	if (value.age_ms >= lifetime_ms)
	{
		return snapshot{.age_ms = value.age_ms};
	}
	return value;
}
}
