#pragma once

#include "common/buffer.h"
#include "core/result.h"
#include "infrastructure/measurement_state.h"
#include "drivers/protocols/bluetooth_battery.h"

#include <zephyr/bluetooth/gatt.h>

namespace ble
{
struct gatt_callbacks
{
	void *context = nullptr;
	status (*received)(void *context, bt_conn *connection, data_view data) = nullptr;
	bool (*authorized)(void *context, bt_conn *connection) = nullptr;
	void (*changed)(void *context, bt_conn *connection) = nullptr;
};

class zephyr_gatt_server
{
public:
	zephyr_gatt_server() = default;
	~zephyr_gatt_server();
	zephyr_gatt_server(const zephyr_gatt_server &) = delete;
	zephyr_gatt_server &operator=(const zephyr_gatt_server &) = delete;
	zephyr_gatt_server(zephyr_gatt_server &&) = delete;
	zephyr_gatt_server &operator=(zephyr_gatt_server &&) = delete;

	status bind(gatt_callbacks callbacks);
	void unbind();

	status notify(bt_conn *connection, data_view data);
	bool is_subscribed(bt_conn *connection) const;
	static std::uint16_t max_payload(bt_conn *connection);

	status update_measurement(const bluetooth_measurement::snapshot &value);
	void reset_measurement();
	void notify_measurements(bt_conn *connection);
	status update_battery(const bluetooth_battery::snapshot &value);
	void reset_battery();
	void notify_battery(bt_conn *connection);
	static ssize_t on_battery_read(bt_conn *connection, const bt_gatt_attr *attribute,
		void *buffer, std::uint16_t length, std::uint16_t offset);

	static ssize_t on_measurement_read(bt_conn *connection, const bt_gatt_attr *attribute,
		void *buffer, std::uint16_t length, std::uint16_t offset);
	static ssize_t on_rx_write(
		bt_conn *connection,
		const bt_gatt_attr *attribute,
		const void *buffer,
		std::uint16_t length,
		std::uint16_t offset,
		std::uint8_t flags
	);
	static void on_subscription_changed(const bt_gatt_attr *attribute, std::uint16_t value);
	static void on_mtu_updated(bt_conn *connection, std::uint16_t tx, std::uint16_t rx);

private:
	static constexpr std::uint8_t all_measurements = 0x0f;
	static constexpr std::uint8_t measurement_metadata = 0x0c;

	gatt_callbacks callbacks_{};

	measurement_state measurements_;
	std::uint32_t revision_ = 0;
	std::uint8_t pending_ = all_measurements;
	std::uint8_t last_flags_ = 0;
	bluetooth_battery::snapshot battery_{};
	std::int64_t battery_received_at_ = 0;
	std::uint32_t battery_revision_ = 0;
	std::uint8_t battery_pending_ = 3;
	std::uint8_t battery_last_flags_ = 0;
};
}
