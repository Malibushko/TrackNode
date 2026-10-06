#pragma once

#include "core/result.h"

namespace ble
{
class bridge_service;

class firmware_application
{
public:
	explicit firmware_application(bridge_service &bridge);
	status start();
	void stop();
	status poll();

private:
	bridge_service &bridge_;
	status startup_ = fail(component::application, error_code::not_initialized);
};
}
