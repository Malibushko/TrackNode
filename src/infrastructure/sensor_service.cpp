#include "infrastructure/sensor_service.h"
#include "common/common.h"

namespace
{
constexpr std::uint32_t conversion_poll_ms = 2;
constexpr std::uint32_t conversion_poll_attempts = 10;
}

//////////////////////////////////////////////////////////////////////////

status sensor_service::init()
{
	if (initialized_)
	{
		return {};
	}
	auto result = device_.init();
	initialized_ = result.has_value();
	return result;
}

//////////////////////////////////////////////////////////////////////////

result<bmp280::environmental_sample> sensor_service::read()
{
	if (!initialized_)
	{
		return fail(component::i2c_device, error_code::not_initialized);
	}
	if (device_.measures_on_request())
	{
		if (auto converted = convert_once(); !converted)
		{
			return tl::make_unexpected(converted.error());
		}
	}
	return device_.read_measurement();
}

//////////////////////////////////////////////////////////////////////////

status sensor_service::convert_once()
{
	if (auto started = device_.start_measurement(); !started)
	{
		return started;
	}
	common::delay(device_.max_measurement_time_ms());

	for (std::uint32_t attempt = 0; attempt < conversion_poll_attempts; ++attempt)
	{
		const auto measuring = device_.is_measuring();
		if (!measuring)
		{
			return tl::make_unexpected(measuring.error());
		}
		if (!*measuring)
		{
			return {};
		}
		common::delay(conversion_poll_ms);
	}

	return fail(component::i2c_device, error_code::timeout_error);
}

//////////////////////////////////////////////////////////////////////////
