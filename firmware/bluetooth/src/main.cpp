#include "drivers/devices/zephyr_uart.h"
#include "drivers/devices/zephyr_bluetooth.h"
#include "drivers/protocols/ble_bridge.h"
#include "infrastructure/bridge_service.h"
#include "drivers/protocols/bridge_codec.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <algorithm>

namespace
{
constexpr char bluetooth_name[] = "TrackNode";
constexpr std::uint32_t idle_wakeup_ms = 60000;
const gpio_dt_spec bridge_rx_wake = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), bridge_rx_gpios);

k_sem activity;
}

int main()
{
	k_sem_init(&activity, 0, 1);

	static ble::zephyr_uart transport{*DEVICE_DT_GET(DT_ALIAS(bridge_uart)), ble::uart_config{
		.baud_rate = ble_bridge::baud_rate,
		.wake_input = &bridge_rx_wake,
		.awake_hold_ms = ble_bridge::receiver_awake_hold_ms,
		.peer_sleep_after_ms = ble_bridge::peer_sleep_after_ms,
		.peer_wake_guard_ms = ble_bridge::peer_wake_guard_ms
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

		.write_timeout_ms = 10
	};

	static ble::bridge_service bridge{bridge_config};
	transport.set_activity_signal(&activity);
	bluetooth.set_activity_signal(&activity);

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
			transport.resume_if_woken();
			const auto drained = bridge.drain();
			if (!drained)
			{
				bridge.stop();
				k_msleep(1000);
				break;
			}
			if (*drained)
			{
				continue;
			}

			(void)transport.suspend_when_idle();
			const auto wake_in_ms = std::min(bridge.next_wakeup_ms().value_or(idle_wakeup_ms),
				transport.suspend_after_ms().value_or(idle_wakeup_ms));
			(void)k_sem_take(&activity, K_MSEC(wake_in_ms));
		}
	}
}
