#include "drivers/devices/zephyr_uart.h"
#include "drivers/devices/zephyr_bluetooth.h"
#include "drivers/protocols/ble_bridge.h"
#include "infrastructure/bridge_service.h"
#include "drivers/protocols/bridge_codec.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

namespace
{
constexpr char bluetooth_name[] = "TrackNode";
constexpr char service_uuid[] = "e0910001-7e83-4c9c-a89e-bfe03c940001";
constexpr char rx_uuid[] = "e0910002-7e83-4c9c-a89e-bfe03c940001";
constexpr char tx_uuid[] = "e0910003-7e83-4c9c-a89e-bfe03c940001";
}

int main()
{
	static ble::zephyr_uart transport{*DEVICE_DT_GET(DT_ALIAS(bridge_uart)), ble::uart_config{
		ble_bridge::baud_rate
	}};

	static ble::zephyr_bluetooth bluetooth{ble::bluetooth_config{
		bluetooth_name,
		service_uuid,
		rx_uuid,
		tx_uuid
	}};

	static ble_bridge::bridge_codec codec;

	static ble::bridge_config bridge_config {
		.transport = transport,
		.bluetooth = bluetooth,
		.codec = codec,

		.poll_frequency_ms = 500,
		.poll_transport_timeout_ms = 100
	};

	static ble::bridge_service bridge{bridge_config};

	const auto started = bridge.start();
	if (!started)
	{
		return 1;
	}

	while (true)
	{
		const auto polled = bridge.poll();
		if (!polled)
		{
			bridge.stop();
			return 1;
		}

		k_msleep(1);
	}
}
