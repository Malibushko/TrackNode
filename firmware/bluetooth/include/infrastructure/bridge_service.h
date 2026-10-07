#pragma once

#include "core/result.h"

namespace ble_bridge
{
class bridge_codec;
}

namespace ble
{
class uart;
class bluetooth_backend;

class bridge_service
{
public:
	bridge_service(uart &transport, bluetooth_backend &bluetooth, ble_bridge::bridge_codec &codec);
	status start();
	void stop();
	status poll();

private:
	uart &transport_;
	bluetooth_backend &bluetooth_;
	ble_bridge::bridge_codec &codec_;
};
}
