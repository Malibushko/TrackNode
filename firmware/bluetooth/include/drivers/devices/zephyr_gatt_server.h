#pragma once

#include "common/buffer.h"
#include "core/result.h"

#include <array>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

namespace ble
{
struct gatt_config
{
	const char *service_uuid = nullptr;
	const char *rx_uuid = nullptr;
	const char *tx_uuid = nullptr;
};

struct gatt_callbacks
{
	void *context = nullptr;
	status (*received)(void *context, bt_conn *connection, data_view data) = nullptr;
};

class zephyr_gatt_server
{
public:
	explicit zephyr_gatt_server(gatt_config config);
	zephyr_gatt_server(const zephyr_gatt_server &) = delete;
	zephyr_gatt_server &operator=(const zephyr_gatt_server &) = delete;
	zephyr_gatt_server(zephyr_gatt_server &&) = delete;
	zephyr_gatt_server &operator=(zephyr_gatt_server &&) = delete;

	status register_service(gatt_callbacks callbacks);
	void unregister_service();
	status notify(bt_conn *connection, data_view data);
	bool is_subscribed(bt_conn *connection) const;

private:
	enum class attribute_index : std::size_t
	{
		service,
		rx_declaration,
		rx_value,
		tx_declaration,
		tx_value,
		tx_ccc,
		count
	};

	static ssize_t on_rx_write(
		bt_conn *connection,
		const bt_gatt_attr *attribute,
		const void *buffer,
		std::uint16_t length,
		std::uint16_t offset,
		std::uint8_t flags
	);

	gatt_config config_;
	gatt_callbacks callbacks_{};

	bt_uuid_128 service_uuid_{};
	bt_uuid_128 rx_uuid_{};
	bt_uuid_128 tx_uuid_{};

	bt_gatt_chrc rx_characteristic_{};
	bt_gatt_chrc tx_characteristic_{};
	bt_gatt_ccc_managed_user_data tx_ccc_{};

	std::array<bt_gatt_attr, static_cast<std::size_t>(attribute_index::count)> attributes_{};
	bt_gatt_service service_{};
	bool registered_ = false;
};
}
