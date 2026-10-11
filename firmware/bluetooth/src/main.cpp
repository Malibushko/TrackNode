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
}

int main()
{
	static ble::zephyr_uart transport{*DEVICE_DT_GET(DT_ALIAS(bridge_uart)), ble::uart_config{
		ble_bridge::baud_rate
	}};

	static ble::zephyr_bluetooth bluetooth{ble::bluetooth_config{
		.name = bluetooth_name
	}};

	static ble_bridge::bridge_codec codec;

	static ble::bridge_config bridge_config {
		.transport = transport,
		.bluetooth = bluetooth,
		.codec = codec,
		.uptime_ms = []{ return k_uptime_get_32(); },

		.poll_frequency_ms = 1,
		.poll_transport_timeout_ms = 10,
		.poll_bluetooth_timeout_ms = 0
	};

	static ble::bridge_service bridge{bridge_config};

	while (true)
	{
		const auto started = bridge.start();
		if (!started)
		{
			k_msleep(1000);
			continue;
		}

		while (true)
		{
			const auto polled = bridge.poll();
			if (!polled)
			{
				bridge.stop();
				k_msleep(1000);
				break;
			}

			k_msleep(bridge_config.poll_frequency_ms);
		}
	}
}
