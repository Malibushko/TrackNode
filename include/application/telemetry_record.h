#pragma once

#include "application/sequence.h"
#include <array>

struct telemetry_record
{
	static constexpr std::size_t size = 31;
	std::array<std::uint8_t, size> bytes{};

	[[nodiscard]] sequence_t sample_id() const
	{
		return get(0, 8);
	}

	[[nodiscard]] std::uint32_t timestamp_seconds() const
	{
		return static_cast<std::uint32_t>(get(8, 4));
	}

	[[nodiscard]] std::uint32_t position_timestamp_seconds() const
	{
		return static_cast<std::uint32_t>(get(12, 4));
	}

	[[nodiscard]] bool environment_valid() const
	{
		return (bytes[16] & 1) != 0;
	}

	[[nodiscard]] bool position_valid() const
	{
		return (bytes[16] & 2) != 0;
	}

	[[nodiscard]] std::int16_t temperature_centidegrees() const
	{
		return static_cast<std::int16_t>(get(17, 2));
	}

	[[nodiscard]] std::uint32_t pressure_pa() const
	{
		return static_cast<std::uint32_t>(get(19, 4));
	}

	[[nodiscard]] std::int32_t latitude_e7() const
	{
		return static_cast<std::int32_t>(get(23, 4));
	}

	[[nodiscard]] std::int32_t longitude_e7() const
	{
		return static_cast<std::int32_t>(get(27, 4));
	}

	void set_identity(sequence_t sample_id, std::uint32_t seconds)
	{
		put(0, sample_id, 8);
		put(8, seconds, 4);
	}

	void set_environment(std::int16_t temperature, std::uint32_t pressure)
	{
		bytes[16] |= 1;
		put(17, static_cast<std::uint16_t>(temperature), 2);
		put(19, pressure, 4);
	}

	void set_position(std::uint32_t seconds, std::int32_t latitude, std::int32_t longitude)
	{
		bytes[16] |= 2;
		put(12, seconds, 4);
		put(23, static_cast<std::uint32_t>(latitude), 4);
		put(27, static_cast<std::uint32_t>(longitude), 4);
	}

private:
	void put(std::size_t offset, std::uint64_t value, std::size_t length)
	{
		for (std::size_t index = 0; index < length; ++index)
		{
			bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
		}
	}

	[[nodiscard]] std::uint64_t get(std::size_t offset, std::size_t length) const
	{
		std::uint64_t value = 0;
		for (std::size_t index = 0; index < length; ++index)
		{
			value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8);
		}
		return value;
	}
};

static_assert(sizeof(telemetry_record) == telemetry_record::size);
