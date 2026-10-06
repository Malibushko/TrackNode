#include "drivers/devices/zephyr_bluetooth.h"
#include <zephyr/bluetooth/bluetooth.h>

namespace ble
{
zephyr_bluetooth::zephyr_bluetooth(bluetooth_config config) : config_(config) {}

status zephyr_bluetooth::init()
{
	return fail(component::bluetooth, error_code::not_implemented);
}

void zephyr_bluetooth::stop() {}

status zephyr_bluetooth::send(data_view, session_type)
{
	return fail(component::bluetooth, error_code::not_implemented);
}

result<bluetooth_event> zephyr_bluetooth::poll(std::uint32_t)
{
	return fail(component::bluetooth, error_code::not_implemented);
}
}
