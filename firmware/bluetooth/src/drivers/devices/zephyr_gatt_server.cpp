#include "drivers/devices/zephyr_gatt_server.h"

namespace ble
{
zephyr_gatt_server::zephyr_gatt_server(gatt_config config) : config_(config)
{
}

status zephyr_gatt_server::register_service(gatt_callbacks callbacks)
{
	(void)callbacks;

	return fail(component::bluetooth, error_code::not_implemented);
}

void zephyr_gatt_server::unregister_service()
{
}

status zephyr_gatt_server::notify(bt_conn *connection, data_view data)
{
	(void)connection;
	(void)data;

	return fail(component::bluetooth, error_code::not_implemented);
}

bool zephyr_gatt_server::is_subscribed(bt_conn *connection) const
{
	(void)connection;

	return false;
}

ssize_t zephyr_gatt_server::on_rx_write(
	bt_conn *connection,
	const bt_gatt_attr *attribute,
	const void *buffer,
	std::uint16_t length,
	std::uint16_t offset,
	std::uint8_t flags
)
{
	(void)connection;
	(void)attribute;
	(void)buffer;
	(void)length;
	(void)offset;
	(void)flags;

	return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
}
}
