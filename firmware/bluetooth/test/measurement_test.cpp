#include "drivers/devices/zephyr_bluetooth.h"
#include "drivers/protocols/gatt_uuid.h"
#include <iostream>
#include <stdexcept>

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("Measurement contract test failed");
	}
}

int main()
{
	using namespace bluetooth_measurement;
	snapshot value{
		.flags = sensor_valid | gnss_present | position_valid | speed_valid | utc_valid,
		.sensor_sequence = 0x12345678,
		.temperature = -125,
		.pressure = 1000123,
		.sensor_age_ms = 250,
		.sensor_lifetime_ms = 10000,
		.gnss_sequence = 0x123456789abcdef0,
		.gnss_age_ms = 4500,
		.latitude = -900000000,
		.longitude = 1800000000,
		.speed = 65535,
		.utc = UINT32_MAX
	};
	auto bytes = encode(value);
	require(bytes[2] == 0x78 && bytes[3] == 0x56 && bytes[6] == 0x83 && bytes[7] == 0xff);
	auto decoded = decode(bytes);
	require(decoded && encode(*decoded) == bytes);
	require(!decode(std::span{bytes}.first(bytes.size() - 1)));
	bytes[0] = 2;
	require(!decode(bytes));
	bytes = encode(value);
	bytes[1] |= 64;
	require(!decode(bytes));
	auto invalid = value;
	invalid.flags &= ~gnss_present;
	require(!valid(invalid));
	invalid = value;
	invalid.latitude = 900000001;
	require(!valid(invalid));
	invalid = value;
	invalid.temperature = INT16_MIN;
	require(!valid(invalid));
	invalid = value;
	invalid.sensor_lifetime_ms = 0;
	require(!valid(invalid));
	require(add_age(UINT32_MAX - 1, 2) == UINT32_MAX);

	ble::measurement_state state;
	require(state.current(1000).flags == 0);
	state.update(value, UINT32_MAX - 100);
	require(state.current(std::uint64_t{UINT32_MAX} + 200).gnss_age_ms == 4800);
	require(state.current(std::uint64_t{UINT32_MAX} + 501).flags & sensor_valid);
	require(!(state.current(std::uint64_t{UINT32_MAX} + 501).flags & gnss_fresh));
	require(!(state.current(std::uint64_t{UINT32_MAX} + 10000).flags & sensor_valid));
	require(state.current(std::uint64_t{UINT32_MAX} * 3).gnss_age_ms == UINT32_MAX);
	state.reset();
	require(state.current(100).flags == 0);

	const auto find = [&](const bt_uuid *uuid)
	{
		const bt_gatt_attr *found = nullptr;
		for (const auto *service : fake_gatt_services)
		{
			if (const auto *attribute = bt_gatt_find_by_uuid(service->attrs, service->attr_count, uuid))
			{
				found = attribute;
			}
		}
		require(found && found->read);
		return found;
	};
	const auto *temperature = find(&ble::gatt_uuid::temperature.uuid);
	const auto *pressure = find(&ble::gatt_uuid::pressure.uuid);
	const auto *gnss = find(&ble::gatt_uuid::gnss.uuid);
	const auto *status = find(&ble::gatt_uuid::measurement_status.uuid);
	ble::zephyr_bluetooth bluetooth{{.name = "TrackNode"}};
	require(bluetooth.init().has_value());
	bt_conn connection;
	registered_callback->connected(&connection, 0);
	require(bluetooth.poll(0)->link.connected);
	std::array<std::uint8_t, 20> read{};
	require(temperature->read(&connection, temperature, read.data(), read.size(), 0) == BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
	require(status->read(&connection, status, read.data(), read.size(), 0) == 18 && read[1] == 0);
	require(bluetooth.update_measurement(value).has_value());
	require(temperature->read(&connection, temperature, read.data(), read.size(), 0) == 2);
	require(get<std::int16_t>(read, 0) == -125);
	require(pressure->read(&connection, pressure, read.data(), read.size(), 0) == 4);
	require(get<std::uint32_t>(read, 0) == 1000123);
	require(gnss->read(&connection, gnss, read.data(), 3, 2) == 3);
	require(gnss->read(&connection, gnss, read.data(), read.size(), 21) == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
	registered_callback->connected(&connection, 0);
	subscriptions[temperature] = true;
	require(bluetooth.poll(0).has_value());
	require(notification_count == 1 && notifications.back().first == temperature);
	require(bluetooth.poll(0).has_value() && notification_count == 1);
	subscriptions[gnss] = true;
	subscriptions[status] = true;
	notify_error = -ENOMEM;
	require(bluetooth.poll(0).has_value() && notification_count == 1);
	notify_error = 0;
	require(bluetooth.poll(0).has_value() && notification_count == 3);
	require(notifications[1].first == gnss && notifications[1].second.size() == 20);
	require(notifications[2].first == status && notifications[2].second.size() == 18);
	simulated_time = 501;
	require(bluetooth.poll(0).has_value() && notification_count == 5);
	require(!(notifications.back().second[1] & gnss_fresh));
	simulated_time = 10000;
	require(bluetooth.poll(0).has_value());
	require(!(notifications.back().second[1] & sensor_valid));
	require(pressure->read(&connection, pressure, read.data(), read.size(), 0) == BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
	bluetooth.reset_measurement();
	require(bluetooth.poll(0).has_value());
	require(status->read(&connection, status, read.data(), read.size(), 0) == 18 && read[1] == 0);
	simulated_time = 0;
	require(bluetooth.update_measurement(value).has_value());
	bool replaced = false;
	notification_hook = [&]
	{
		if (!replaced)
		{
			replaced = true;
			value.sensor_sequence = 42;
			require(bluetooth.update_measurement(value).has_value());
		}
	};
	require(bluetooth.poll(0).has_value());
	notification_hook = {};
	require(bluetooth.poll(0).has_value());
	require(get<std::uint32_t>(notifications.back().second, 2) == 42);
	bluetooth.stop();
	std::cout << "Codec, read offsets, independent CCC, MTU 23, freshness, reset and notify retry passed\n";
}
