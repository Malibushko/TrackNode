#include "drivers/devices/zephyr_bluetooth.h"
#include <array>
#include <iostream>
#include <stdexcept>

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("Bluetooth initialization test failed");
	}
}

int main()
{
	ble::zephyr_bluetooth bluetooth{{
		.name = "TrackNode"
	}};

	registration_error = -ENOMEM;
	require(bluetooth.init().error().code == ble::error_code::no_resources);
	require(registered_callback == nullptr);
	registration_error = 0;
	enable_error = -EIO;
	require(bluetooth.init().error().code == ble::error_code::io);
	require(registered_callback == nullptr);
	enable_error = 0;
	name_error = -EINVAL;
	require(bluetooth.init().error().code == ble::error_code::invalid_config);
	require(registered_callback == nullptr);
	name_error = 0;
	require(bluetooth.init().has_value());
	require(registered_callback != nullptr);
	require(bluetooth.init().error().code == ble::error_code::already_initialized);
	bluetooth.stop();
	require(registered_callback == nullptr);
	bluetooth.stop();
	require(bluetooth.init().has_value());

	struct mapping
	{
		int native;
		ble::error_code expected;
	};
	const std::array cases{
		mapping{-EINVAL, ble::error_code::invalid_config},
		mapping{-EMSGSIZE, ble::error_code::invalid_config},
		mapping{-ENOSYS, ble::error_code::not_supported},
		mapping{-ENOTSUP, ble::error_code::not_supported},
		mapping{-ENODEV, ble::error_code::not_ready},
		mapping{-EHOSTDOWN, ble::error_code::not_ready},
		mapping{-ETIMEDOUT, ble::error_code::timeout},
		mapping{-EAGAIN, ble::error_code::busy},
		mapping{-EBUSY, ble::error_code::busy},
		mapping{-ENOMEM, ble::error_code::no_resources},
		mapping{-ENOBUFS, ble::error_code::no_resources},
		mapping{-ENOTCONN, ble::error_code::not_connected},
		mapping{-ECONNRESET, ble::error_code::not_connected},
		mapping{-EACCES, ble::error_code::access_denied},
		mapping{-EPERM, ble::error_code::access_denied},
		mapping{-EEXIST, ble::error_code::already_initialized},
		mapping{-EIO, ble::error_code::io},
		mapping{-12345, ble::error_code::io}
	};

	for (const auto &entry : cases)
	{
		advertising_error = entry.native;
		require(bluetooth.start_advertise().error().code == entry.expected);
		require(bluetooth.stop_advertise().error().code == entry.expected);
	}

	advertising_error = -EALREADY;
	require(bluetooth.start_advertise().has_value());
	require(bluetooth.stop_advertise().has_value());
	advertising_error = 0;
	bluetooth.stop();
	require(registered_callback == nullptr);
	require(bluetooth.poll(0).error().code == ble::error_code::not_initialized);
	require(bluetooth.init().has_value());
	bt_conn connection;
	registered_callback->connected(&connection, 1);
	require(connection.references == 1);
	registered_callback->connected(&connection, 0);
	require(connection.references == 3);
	registered_callback->disconnected(&connection, 0x13);
	require(connection.references == 3);
	bluetooth.stop();
	require(connection.references == 1);
	require(connection.disconnects == 0);

	require(bluetooth.init().has_value());
	for (int i = 0; i < 8; ++i)
	{
		registered_callback->connected(&connection, 0);
	}
	require(connection.references == 10);
	registered_callback->connected(&connection, 0);
	require(connection.references == 10);
	require(bluetooth.poll(0).error().code == ble::error_code::overflow);
	require(bluetooth.poll(100).error().code == ble::error_code::overflow);
	bluetooth.stop();
	require(connection.references == 1);
	require(connection.disconnects == 1);
	bluetooth.stop();
	require(connection.disconnects == 1);

	require(bluetooth.init().has_value());
	require(bluetooth.poll(0)->type == ble::bluetooth_event::kind::idle);
	for (int i = 0; i < 8; ++i)
	{
		registered_callback->connected(&connection, 0);
	}
	registered_callback->disconnected(&connection, 0x13);
	require(connection.references == 9);
	require(bluetooth.poll(0).error().code == ble::error_code::overflow);
	bluetooth.stop();
	require(connection.references == 1);
	require(connection.disconnects == 1);

	require(spinlock_depth == 0);
	{
		auto original = ble::retain_connection(&connection);
		require(connection.references == 2);
		auto moved = std::move(original);
		require(!original && moved.get() == &connection);
		require(connection.references == 2);
	}
	require(connection.references == 1);

	require(bluetooth.init().has_value());
	registered_callback->connected(&connection, 0);
	const auto connected = bluetooth.poll(0);
	require(connected && connected->type == ble::bluetooth_event::kind::link_changed);
	require(connected->link.connected && connection.references == 2);
	registered_callback->disconnected(&connection, 0x13);
	const auto disconnected = bluetooth.poll(0);
	require(disconnected && !disconnected->link.connected);
	require(connection.references == 1);
	pump = {};
	require(bluetooth.poll(5)->type == ble::bluetooth_event::kind::idle);
	msgq_get_hook = [&]
	{
		for (int i = 0; i < 9; ++i)
		{
			registered_callback->connected(&connection, 0);
		}
	};
	require(bluetooth.poll(0).error().code == ble::error_code::overflow);
	msgq_get_hook = {};
	require(connection.references == 9);
	bluetooth.stop();
	require(connection.references == 1);
	require(spinlock_depth == 0);

	require(bluetooth.init().has_value());
	msgq_get_hook = [&] { bluetooth.stop(); };
	require(bluetooth.poll(0).error().code == ble::error_code::not_initialized);
	msgq_get_hook = {};
	require(spinlock_depth == 0);

	fake_bond_count = 0;
	k_sem activity;
	k_sem_init(&activity, 0, 1);
	bluetooth.set_activity_signal(&activity);
	require(bluetooth.init().has_value());
	fake_bond_count = 1;
	require(advertising_interval_min == BT_GAP_ADV_FAST_INT_MIN_1);
	const auto unbonded_fast_window = bluetooth.next_deadline_ms();
	require(unbonded_fast_window && *unbonded_fast_window == 300000);
	simulated_time += 300000;
	require(bluetooth.poll(0)->type == ble::bluetooth_event::kind::idle);
	require(advertising_interval_min == BT_GAP_ADV_SLOW_INT_MIN);
	require(!bluetooth.next_deadline_ms());
	const auto starts_before_request = advertising_starts;
	require(bluetooth.start_advertise().has_value());
	const auto requested_fast_burst = advertising_starts == starts_before_request + 1 &&
		advertising_interval_min == BT_GAP_ADV_FAST_INT_MIN_1;
	require(requested_fast_burst && *bluetooth.next_deadline_ms() == 30000);
	require(bluetooth.start_advertise().has_value());
	require(advertising_starts == starts_before_request + 1);
	activity.count = 0;
	registered_callback->connected(&connection, 0);
	require(activity.count == 1);
	const auto connected_link = bluetooth.poll(0);
	require(connected_link && connected_link->link.connected);
	require(!bluetooth.next_deadline_ms());
	registered_callback->disconnected(&connection, 0x13);
	require(bluetooth.poll(0).has_value());
	bluetooth.stop();
	bluetooth.set_activity_signal(nullptr);
	require(spinlock_depth == 0);

	std::cout << "Bluetooth rollback, retry, stop/reinit and error mapping passed\n";
	return 0;
}
