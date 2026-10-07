#include "drivers/devices/zephyr_gatt_server.h"
#include "drivers/protocols/gatt_uuid.h"

namespace ble
{
namespace
{
BT_GATT_SERVICE_DEFINE(
	tracknode_service,
	BT_GATT_PRIMARY_SERVICE(&gatt_uuid::service.uuid),
	BT_GATT_CHARACTERISTIC(&gatt_uuid::rx.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, nullptr, zephyr_gatt_server::on_rx_write, nullptr),
	BT_GATT_CHARACTERISTIC(&gatt_uuid::tx.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, nullptr, nullptr, nullptr),
	BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);
}

status zephyr_gatt_server::bind(gatt_callbacks callbacks)
{
	callbacks_ = callbacks;

	return {};
}

void zephyr_gatt_server::unbind()
{
	callbacks_ = {};
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
