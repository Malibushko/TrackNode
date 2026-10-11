#pragma once

#include "application/telemetry_journal.h"
#include <algorithm>
#include <cmath>
#include <optional>

struct telemetry_archive_config
{
	std::int32_t temperature_delta_centidegrees = 50;
	std::uint32_t pressure_delta_pa = 100;
	double position_delta_m = 25;
	std::uint32_t heartbeat_seconds = 3600;
};

class telemetry_archive_policy
{
public:
	explicit telemetry_archive_policy(telemetry_archive_config config = {}) : config_(config) {}

	[[nodiscard]] bool should_archive(const telemetry_record &record) const
	{
		if (!previous_)
		{
			return true;
		}
		const auto availability_changed = previous_->environment_valid() != record.environment_valid() ||
			previous_->position_valid() != record.position_valid();
		const auto heartbeat_due = record.timestamp_seconds() - previous_->timestamp_seconds() >= config_.heartbeat_seconds;
		if (availability_changed || heartbeat_due)
		{
			return true;
		}
		if (record.environment_valid())
		{
			const auto temperature_delta = std::abs(record.temperature_centidegrees() - previous_->temperature_centidegrees());
			const auto pressure_delta = std::abs(static_cast<std::int64_t>(record.pressure_pa()) - previous_->pressure_pa());
			if (temperature_delta >= config_.temperature_delta_centidegrees || pressure_delta >= config_.pressure_delta_pa)
			{
				return true;
			}
		}
		return record.position_valid() && distance_m(record, *previous_) >= config_.position_delta_m;
	}

	void on_archived(const telemetry_record &record)
	{
		previous_ = record;
	}

private:
	static double distance_m(const telemetry_record &left, const telemetry_record &right)
	{
		constexpr double radians_per_unit = 3.14159265358979323846 / 180.0 / 1e7;
		const auto latitude = left.latitude_e7() * radians_per_unit;
		const auto previous_latitude = right.latitude_e7() * radians_per_unit;
		const auto longitude_delta = (static_cast<double>(left.longitude_e7()) - right.longitude_e7()) * radians_per_unit;
		const auto latitude_sine = std::sin((latitude - previous_latitude) / 2);
		const auto longitude_sine = std::sin(longitude_delta / 2);
		const auto haversine = latitude_sine * latitude_sine +
			std::cos(latitude) * std::cos(previous_latitude) * longitude_sine * longitude_sine;
		return 2 * 6371000 * std::asin(std::sqrt(std::clamp(haversine, 0.0, 1.0)));
	}

private:
	telemetry_archive_config config_;
	std::optional<telemetry_record> previous_;
};
