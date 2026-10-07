#pragma once

#include "common/buffer.h"
#include "core/result.h"

#include <zephyr/bluetooth/gatt.h>

namespace ble
{
struct gatt_callbacks
{
	void *context = nullptr;
	status (*received)(void *context, bt_conn *connection, data_view data) = nullptr;
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
	gatt_callbacks callbacks_{};
};
}
