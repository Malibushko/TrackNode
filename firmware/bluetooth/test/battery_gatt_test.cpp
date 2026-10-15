#include "drivers/devices/zephyr_gatt_server.h"
#include "drivers/protocols/gatt_uuid.h"
#include <cassert>
#include <iostream>

int main()
{
	using namespace bluetooth_battery;
	snapshot value{
		.flags = reading_valid | usb_present,
		.level = 72,
		.quality = 2,
		.battery_mv = 3981,
		.supply_mv = 3300,
		.battery_raw = 2470,
		.vrefint_raw = 1500,
		.minimum_raw = 2460,
		.maximum_raw = 2480
	};
	const auto bytes = encode(value);
	assert(bytes[0] == 2 && bytes[2] == 72 && bytes[4] == 0x8d && bytes[5] == 0x0f);
	assert(decode(bytes) && encode(*decode(bytes)) == bytes);
	assert(decode(bytes)->quality == 2);
	auto legacy = bytes;
	legacy[0] = 1;
	legacy[3] = 0;
	assert(decode(legacy) && decode(legacy)->quality == 0);
	assert(aged(value, 300000).flags & reading_valid);
	assert(!decode(std::span{bytes}.first(19)));
	auto invalid = bytes;
	invalid[2] = 101;
	assert(!decode(invalid));
	invalid = bytes;
	invalid[3] = 4;
	assert(!decode(invalid));
	invalid = bytes;
	invalid[1] = 64;
	assert(!decode(invalid));
	assert(!(aged(value, lifetime_ms).flags & reading_valid));
	value.age_ms = UINT32_MAX - 1;
	assert(aged(value, 20).age_ms == UINT32_MAX);
	value.age_ms = 0;

	const auto find = [](const bt_uuid *uuid)
	{
		for (const auto *service : fake_gatt_services)
		{
			if (const auto *attribute = bt_gatt_find_by_uuid(service->attrs, service->attr_count, uuid))
			{
				return attribute;
			}
		}
		return static_cast<const bt_gatt_attr *>(nullptr);
	};
	const auto *level = find(&ble::gatt_uuid::battery_level.uuid);
	const auto *status = find(&ble::gatt_uuid::battery_status.uuid);
	assert(level && status && level->read && status->read);
	ble::zephyr_gatt_server gatt;
	bool authorized = false;
	assert(gatt.bind(ble::gatt_callbacks{
		.context = &authorized,
		.received = [](void *, bt_conn *, ble::data_view) -> ble::status { return {}; },
		.authorized = [](void *context, bt_conn *) { return *static_cast<bool *>(context); }
	}));
	bt_conn connection;
	std::array<std::uint8_t, 20> read{};
	assert(status->read(&connection, status, read.data(), read.size(), 0) == BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION));
	authorized = true;
	assert(level->read(&connection, level, read.data(), read.size(), 0) == BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
	assert(status->read(&connection, status, read.data(), read.size(), 0) == 20 && read[1] == 0);
	assert(gatt.update_battery(value));
	assert(level->read(&connection, level, read.data(), read.size(), 0) == 1 && read[0] == 72);
	assert(status->read(&connection, status, read.data(), read.size(), 0) == 20 && read == bytes);
	assert(status->read(&connection, status, read.data(), 2, 4) == 2 && read[0] == 0x8d);
	assert(status->read(&connection, status, read.data(), 20, 21) == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
	subscriptions[level] = true;
	gatt.notify_battery(&connection);
	assert(notifications.size() == 1 && notifications.back().second == std::vector<std::uint8_t>{72});
	gatt.notify_battery(&connection);
	assert(notifications.size() == 1);
	subscriptions[status] = true;
	gatt.notify_battery(&connection);
	assert(notifications.size() == 2 && notifications.back().second.size() == 20);
	value.level = 80;
	assert(gatt.update_battery(value));
	notify_error = -ENOMEM;
	gatt.notify_battery(&connection);
	assert(notifications.size() == 2);
	notify_error = 0;
	gatt.notify_battery(&connection);
	assert(notifications.size() == 4 && notifications.back().second[2] == 80);
	simulated_time = lifetime_ms;
	gatt.notify_battery(&connection);
	assert(notifications.size() == 5 && notifications.back().first == status && notifications.back().second[1] == usb_present);
	assert(level->read(&connection, level, read.data(), read.size(), 0) == BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
	assert(status->read(&connection, status, read.data(), read.size(), 0) == 20 && read[1] == usb_present);
	assert(gatt.update_battery(value));
	gatt.reset_battery();
	assert(level->read(&connection, level, read.data(), read.size(), 0) == BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
	authorized = false;
	const auto count = notifications.size();
	gatt.notify_battery(&connection);
	assert(notifications.size() == count);
	gatt.unbind();
	assert(!gatt.update_battery(value));
	std::cout << "PASS: battery codec, BAS/status read/notify, authorization, CCC, retry, freshness and reset\n";
}
