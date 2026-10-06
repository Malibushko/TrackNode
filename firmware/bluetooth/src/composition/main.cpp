#include "application/firmware_application.h"
#include "composition/firmware.h"
#include "drivers/devices/zephyr_uart.h"
#include "drivers/devices/zephyr_bluetooth.h"
#include "drivers/protocols/ble_bridge.h"
#include "infrastructure/bridge_service.h"
#include "protocols/bridge_codec.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

namespace ble
{
firmware_application &firmware()
{
	static zephyr_uart transport{*DEVICE_DT_GET(DT_ALIAS(bridge_uart)), uart_config{
		ble_bridge::baud_rate
	}};

	static zephyr_bluetooth bluetooth{bluetooth_config{
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
