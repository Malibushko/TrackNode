#include "drivers/devices/xiao_ble_bridge.h"

status xiao_ble_bridge::init()
{
	// TODO(user): UART ownership, bridge handshake and matching XIAO firmware.
	return fail(component::service, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

void xiao_ble_bridge::stop()
{
	// TODO(user): release bridge resources, including partial initialization.
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::send(std::span<const std::uint8_t>, std::uint32_t)
{
	// TODO(user): framing, fragmentation, flow control and stale-session rejection.
	return fail(component::service, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

result<bluetooth_event> xiao_ble_bridge::poll(std::uint32_t)
{
	// TODO(user): decode UART frames into backend events with a bounded wait.
	return fail(component::service, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////
