#pragma once

#include "drivers/protocols/bluetooth_measurement.h"
#include <algorithm>

namespace ble
{
class measurement_state
{
public:
	void reset()
	{
		value_ = {};
		present_ = false;
	}

	void update(const bluetooth_measurement::snapshot &value, std::uint64_t now)
	{
		value_ = value;
		received_at_ = now;
		present_ = true;
	}

	bluetooth_measurement::snapshot current(std::uint64_t now) const
	{
		using namespace bluetooth_measurement;
		auto value = value_;
		if (!present_)
		{
			return value;
		}
		const auto elapsed = static_cast<std::uint32_t>(std::min<std::uint64_t>(now - received_at_, UINT32_MAX));
		value.sensor_age_ms = add_age(value.sensor_age_ms, elapsed);
		value.gnss_age_ms = add_age(value.gnss_age_ms, elapsed);
		if (value.sensor_age_ms >= value.sensor_lifetime_ms)
		{
			value.flags &= ~sensor_valid;
		}
		if ((value.flags & gnss_present) && value.gnss_age_ms <= gnss_freshness_ms)
		{
			value.flags |= gnss_fresh;
		}
		return value;
	}

	static std::array<std::uint8_t, 18> status_value(const bluetooth_measurement::snapshot &value)
	{
		using namespace bluetooth_measurement;
		std::array<std::uint8_t, 18> bytes{};
		bytes[0] = schema;
		bytes[1] = value.flags;
		put(bytes, 2, value.sensor_sequence);
		put(bytes, 6, value.sensor_age_ms);
		put(bytes, 10, value.gnss_sequence);
		return bytes;
	}

	static std::array<std::uint8_t, 20> gnss_value(const bluetooth_measurement::snapshot &value)
	{
		using namespace bluetooth_measurement;
		std::array<std::uint8_t, 20> bytes{};
		bytes[0] = schema;
		bytes[1] = value.flags & ~sensor_valid;
		put(bytes, 2, value.latitude);
		put(bytes, 6, value.longitude);
		put(bytes, 10, value.speed);
		put(bytes, 12, value.utc);
		put(bytes, 16, value.gnss_age_ms);
		return bytes;
	}

private:
	bluetooth_measurement::snapshot value_{};
	std::uint64_t received_at_ = 0;
	bool present_ = false;
};
}
