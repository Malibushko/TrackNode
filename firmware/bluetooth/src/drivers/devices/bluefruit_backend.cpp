#include "drivers/devices/bluefruit_backend.h"
#include <bluefruit.h>

namespace ble
{
bluefruit_backend::bluefruit_backend(bluetooth_config config) : config_(config) {}

status bluefruit_backend::init()
{
	return fail(component::bluetooth, error_code::not_implemented);
}

void bluefruit_backend::stop() {}

status bluefruit_backend::send(data_view, session_type)
{
	return fail(component::bluetooth, error_code::not_implemented);
}

result<bluetooth_event> bluefruit_backend::poll(std::uint32_t)
{
	return fail(component::bluetooth, error_code::not_implemented);
}
}
