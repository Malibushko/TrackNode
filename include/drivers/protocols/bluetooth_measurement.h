#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace bluetooth_measurement
{
inline constexpr std::uint8_t schema = 1;
inline constexpr std::size_t wire_size = 46;
inline constexpr std::uint32_t gnss_freshness_ms = 5000;
enum flag : std::uint8_t
{
	sensor_valid = 1,
	gnss_present = 2,
	position_valid = 4,
	speed_valid = 8,
	utc_valid = 16,
	gnss_fresh = 32
};

struct snapshot
{
	std::uint8_t flags = 0;
	std::uint32_t sensor_sequence = 0;
	std::int16_t temperature = 0;
	std::uint32_t pressure = 0;
	std::uint32_t sensor_age_ms = 0;
	std::uint32_t sensor_lifetime_ms = 0;
	std::uint64_t gnss_sequence = 0;
	std::uint32_t gnss_age_ms = 0;
	std::int32_t latitude = 0;
	std::int32_t longitude = 0;
	std::uint16_t speed = 0;
	std::uint32_t utc = 0;
};

template<class T>
inline void put(std::span<std::uint8_t> output, std::size_t offset, T value)
{
	using U = std::make_unsigned_t<T>;
	const auto bits = std::bit_cast<U>(value);
	for (std::size_t index = 0; index < sizeof(T); ++index)
	{
		output[offset + index] = static_cast<std::uint8_t>(bits >> (index * 8));
	}
}

template<class T>
inline T get(std::span<const std::uint8_t> input, std::size_t offset)
{
	using U = std::make_unsigned_t<T>;
	U bits = 0;
	for (std::size_t index = 0; index < sizeof(T); ++index)
	{
		bits |= static_cast<U>(static_cast<U>(input[offset + index]) << (index * 8));
	}
	return std::bit_cast<T>(bits);
}

inline bool valid(const snapshot &value)
{
	return (value.flags & ~std::uint8_t{31}) == 0 &&
		(!(value.flags & sensor_valid) || (value.temperature >= -27315 && value.sensor_lifetime_ms != 0)) &&
		((value.flags & gnss_present) || !(value.flags & (position_valid | speed_valid | utc_valid))) &&
		(!(value.flags & position_valid) || (value.latitude >= -900000000 && value.latitude <= 900000000 &&
			value.longitude >= -1800000000 && value.longitude <= 1800000000));
}

inline std::array<std::uint8_t, wire_size> encode(const snapshot &value)
{
	std::array<std::uint8_t, wire_size> output{};
	output[0] = schema;
	output[1] = value.flags;
	put(output, 2, value.sensor_sequence);
	put(output, 6, value.temperature);
	put(output, 8, value.pressure);
	put(output, 12, value.sensor_age_ms);
	put(output, 16, value.sensor_lifetime_ms);
	put(output, 20, value.gnss_sequence);
	put(output, 28, value.gnss_age_ms);
	put(output, 32, value.latitude);
	put(output, 36, value.longitude);
	put(output, 40, value.speed);
	put(output, 42, value.utc);
	return output;
}

inline std::optional<snapshot> decode(std::span<const std::uint8_t> input)
{
	if (input.size() != wire_size || input[0] != schema)
	{
		return std::nullopt;
	}
	snapshot value{
		.flags = input[1],
		.sensor_sequence = get<std::uint32_t>(input, 2),
		.temperature = get<std::int16_t>(input, 6),
		.pressure = get<std::uint32_t>(input, 8),
		.sensor_age_ms = get<std::uint32_t>(input, 12),
		.sensor_lifetime_ms = get<std::uint32_t>(input, 16),
		.gnss_sequence = get<std::uint64_t>(input, 20),
		.gnss_age_ms = get<std::uint32_t>(input, 28),
		.latitude = get<std::int32_t>(input, 32),
		.longitude = get<std::int32_t>(input, 36),
		.speed = get<std::uint16_t>(input, 40),
		.utc = get<std::uint32_t>(input, 42)
	};
	if (!valid(value))
	{
		return std::nullopt;
	}
	return value;
}

inline std::uint32_t add_age(std::uint32_t age, std::uint32_t elapsed)
{
	return elapsed > UINT32_MAX - age ? UINT32_MAX : age + elapsed;
}
}
