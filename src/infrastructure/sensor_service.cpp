#include "infrastructure/sensor_service.h"

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
	return device_.read_measurement();
}

//////////////////////////////////////////////////////////////////////////
