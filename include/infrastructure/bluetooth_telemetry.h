#pragma once

#include "application/telemetry_sink.h"
#include "drivers/protocols/bluetooth_measurement.h"
#include <cmath>

static_assert(gnss_freshness_limit_ms == bluetooth_measurement::gnss_freshness_ms);

inline bluetooth_measurement::snapshot format_bluetooth_measurement(const telemetry_sample &sample,
	TickType_t now, std::uint32_t lifetime_ms)
{
	using namespace bluetooth_measurement;
	snapshot value{
		.sensor_sequence = sample.sensor.sequence,
		.sensor_age_ms = pdTICKS_TO_MS(now - sample.sensor.timestamp),
		.sensor_lifetime_ms = lifetime_ms
	};
	const double temperature = std::round(static_cast<double>(sample.sensor.temperature_c) * 100);
	const double pressure = std::round(static_cast<double>(sample.sensor.pressure_pa) * 10);
	const auto sensor_values_encodable = sample.sensor.valid && std::isfinite(temperature) &&
		temperature >= -27315 && temperature <= INT16_MAX &&
		std::isfinite(pressure) && pressure >= 0 && pressure <= UINT32_MAX;
	if (sensor_values_encodable)
	{
		value.flags |= sensor_valid;
		value.temperature = static_cast<std::int16_t>(temperature);
		value.pressure = static_cast<std::uint32_t>(pressure);
	}
	if (!sample.gnss)
	{
		return value;
	}
	const auto &gnss = *sample.gnss;
	value.flags |= gnss_present;
	value.gnss_sequence = gnss.sequence;
	value.gnss_age_ms = pdTICKS_TO_MS(now - gnss.received_at);
	const auto position_values_encodable = gnss.position && std::isfinite(gnss.position->latitude) &&
		std::isfinite(gnss.position->longitude) && std::abs(gnss.position->latitude) <= 90 &&
		std::abs(gnss.position->longitude) <= 180;
	if (position_values_encodable)
	{
		value.flags |= position_valid;
		value.latitude = static_cast<std::int32_t>(std::round(gnss.position->latitude * 10000000));
		value.longitude = static_cast<std::int32_t>(std::round(gnss.position->longitude * 10000000));
	}
	const auto speed_value_encodable = gnss.speed_mps && std::isfinite(*gnss.speed_mps) &&
		*gnss.speed_mps >= 0 && *gnss.speed_mps <= 655.35f;
	if (speed_value_encodable)
	{
		value.flags |= speed_valid;
		value.speed = static_cast<std::uint16_t>(std::round(static_cast<double>(*gnss.speed_mps) * 100));
	}
	const auto utc_value_encodable = gnss.utc_time && *gnss.utc_time >= 0 &&
		static_cast<std::uint64_t>(*gnss.utc_time) <= UINT32_MAX;
	if (utc_value_encodable)
	{
		value.flags |= utc_valid;
		value.utc = static_cast<std::uint32_t>(*gnss.utc_time);
	}
	return value;
}
