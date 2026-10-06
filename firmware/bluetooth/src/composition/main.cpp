#include "application/firmware_application.h"
#include "composition/firmware.h"
#include "drivers/devices/arduino_uart.h"
#include "drivers/devices/bluefruit_backend.h"
#include "infrastructure/bridge_service.h"
#include "protocols/bridge_codec.h"
#include <Arduino.h>

namespace ble
{
firmware_application &firmware()
{
	static arduino_uart transport{Serial1, uart_config{}};
	static bluefruit_backend bluetooth{bluetooth_config{
		"TrackNode",
		"e0910001-7e83-4c9c-a89e-bfe03c940001",
		"e0910002-7e83-4c9c-a89e-bfe03c940001",
		"e0910003-7e83-4c9c-a89e-bfe03c940001"
	}};

	static bridge_codec codec;
	static bridge_service bridge{transport, bluetooth, codec};
	static firmware_application application{bridge};

	return application;
}
}
