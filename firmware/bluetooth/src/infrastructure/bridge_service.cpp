#include "infrastructure/bridge_service.h"

namespace ble
{
bridge_service::bridge_service(uart &transport, bluetooth_backend &bluetooth, ble_bridge::bridge_codec &codec)
	: transport_(transport), bluetooth_(bluetooth), codec_(codec) {}

status bridge_service::start()
{
	return fail(component::service, error_code::not_implemented);
}

void bridge_service::stop() {}

status bridge_service::poll()
{
	return fail(component::service, error_code::not_implemented);
}
}
