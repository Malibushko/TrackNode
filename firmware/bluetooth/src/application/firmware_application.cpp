#include "application/firmware_application.h"
#include "infrastructure/bridge_service.h"

namespace ble
{
firmware_application::firmware_application(bridge_service &bridge) : bridge_(bridge) {}

status firmware_application::start()
{
	startup_ = bridge_.start();
	return startup_;
}

void firmware_application::stop()
{
	bridge_.stop();
	startup_ = fail(component::application, error_code::not_initialized);
}

status firmware_application::poll()
{
	if (!startup_)
	{
		return startup_;
	}

	return bridge_.poll();
}
}
