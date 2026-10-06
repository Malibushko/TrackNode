#pragma once

#include "core/result.h"

namespace ble
{
class uart;
class bluetooth_backend;
class bridge_codec;

class bridge_service
{
public:
	bridge_service(uart &transport, bluetooth_backend &bluetooth, bridge_codec &codec);
	status start();
	void stop();
	status poll();

private:
	uart &transport_;
	bluetooth_backend &bluetooth_;
	bridge_codec &codec_;
};
}
