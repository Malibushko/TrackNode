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
		.name = "TrackNode",
		.service_uuid = "e0910001-7e83-4c9c-a89e-bfe03c940001"
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
		require(bluetooth.start_advertising().error().code == entry.expected);
		require(bluetooth.stop_advertising().error().code == entry.expected);
	}

	advertising_error = -EALREADY;
	require(bluetooth.start_advertising().has_value());
	require(bluetooth.stop_advertising().has_value());
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
	require(bluetooth.poll(0).error().code == ble::error_code::not_implemented);
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
	require(bluetooth.poll(0).error().code == ble::error_code::not_implemented);
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

	std::cout << "Bluetooth rollback, retry, stop/reinit and error mapping passed\n";
	return 0;
}
