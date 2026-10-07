#include "drivers/devices/zephyr_gatt_server.h"
#include "drivers/protocols/gatt_uuid.h"
#include "common/bluetooth_types.h"
#include "core/zephyr_error.h"
#include "core/zephyr_raii.h"

#include <algorithm>

namespace ble
{
namespace
{
K_MUTEX_DEFINE(binding_mutex);
zephyr_gatt_server *instance = nullptr;
bool mtu_callback_registered = false;
constexpr std::size_t att_value_header_size = sizeof(std::uint8_t) + sizeof(std::uint16_t);
bt_gatt_cb mtu_callback{
	.att_mtu_updated = zephyr_gatt_server::on_mtu_updated,
	.node = {}
};

BT_GATT_SERVICE_DEFINE(
	tracknode_service,
	BT_GATT_PRIMARY_SERVICE(&gatt_uuid::service.uuid),
	BT_GATT_CHARACTERISTIC(
		&gatt_uuid::rx.uuid,
		BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_WRITE,
		nullptr,
		zephyr_gatt_server::on_rx_write,
		nullptr
	),
	BT_GATT_CHARACTERISTIC(
		&gatt_uuid::tx.uuid,
		BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_NONE,
		nullptr,
		nullptr,
		nullptr
	),
	BT_GATT_CCC(zephyr_gatt_server::on_subscription_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

const bt_gatt_attr *tx_attribute()
{
	return bt_gatt_find_by_uuid(tracknode_service.attrs, tracknode_service.attr_count, &gatt_uuid::tx.uuid);
}

int from_receive_error(error_code code)
{
	switch (code)
	{
	case error_code::overflow:
	case error_code::no_resources:
	case error_code::busy:
		return BT_ATT_ERR_INSUFFICIENT_RESOURCES;
	case error_code::invalid_config:
		return BT_ATT_ERR_INVALID_ATTRIBUTE_LEN;
	case error_code::access_denied:
		return BT_ATT_ERR_AUTHORIZATION;
	case error_code::not_supported:
	case error_code::not_implemented:
		return BT_ATT_ERR_NOT_SUPPORTED;
	default:
		return BT_ATT_ERR_UNLIKELY;
	}
}
}

zephyr_gatt_server::~zephyr_gatt_server()
{
	unbind();
}

status zephyr_gatt_server::bind(gatt_callbacks callbacks)
{
	if (callbacks.context == nullptr || callbacks.received == nullptr)
	{
		return fail(component::bluetooth, error_code::invalid_config);
	}

	mutex_lock_guard guard{binding_mutex};
	if (instance != nullptr)
	{
		return fail(component::bluetooth, error_code::already_initialized);
	}

	callbacks_ = callbacks;
	instance = this;
	if (!mtu_callback_registered)
	{
		bt_gatt_cb_register(&mtu_callback);
		mtu_callback_registered = true;
	}

	return {};
}

void zephyr_gatt_server::unbind()
{
	mutex_lock_guard guard{binding_mutex};
	if (instance == this)
	{
		instance = nullptr;
		callbacks_ = {};
	}
}

status zephyr_gatt_server::notify(bt_conn *connection, data_view data)
{
	{
		mutex_lock_guard guard{binding_mutex};
		if (instance != this)
		{
			return fail(component::bluetooth, error_code::not_initialized);
		}
	}

	if (connection == nullptr)
	{
		return fail(component::bluetooth, error_code::not_connected);
	}

	const auto capacity = max_payload(connection);
	if (capacity == 0)
	{
		return fail(component::bluetooth, error_code::not_connected);
	}

	if (data.size > capacity || (data.size != 0 && data.data == nullptr))
	{
		return fail(component::bluetooth, error_code::invalid_config);
	}

	const auto *attribute = tx_attribute();
	if (attribute == nullptr)
	{
		return fail(component::bluetooth, error_code::not_ready);
	}

	if (!bt_gatt_is_subscribed(connection, attribute, BT_GATT_CCC_NOTIFY))
	{
		return fail(component::bluetooth, error_code::not_ready);
	}

	const std::uint8_t empty_payload = 0;
	const auto *payload = data.size != 0 ? data.data : &empty_payload;

	const int err = bt_gatt_notify(connection, attribute, payload, static_cast<std::uint16_t>(data.size));
	if (err != 0)
	{
		return fail(component::bluetooth, from_zephyr_error(err));
	}

	return {};
}

bool zephyr_gatt_server::is_subscribed(bt_conn *connection) const
{
	const auto *attribute = tx_attribute();

	return connection != nullptr && attribute != nullptr && bt_gatt_is_subscribed(connection, attribute, BT_GATT_CCC_NOTIFY);
}

std::uint16_t zephyr_gatt_server::max_payload(bt_conn *connection)
{
	if (connection == nullptr)
	{
		return 0;
	}

	const auto mtu = bt_gatt_get_mtu(connection);

	return mtu > att_value_header_size
		? static_cast<std::uint16_t>(std::min<std::size_t>(event_payload_capacity, mtu - att_value_header_size))
		: 0;
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
	(void)attribute;
	if (flags != 0)
	{
		return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
	}

	if (offset != 0)
	{
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (length > event_payload_capacity || (length != 0 && buffer == nullptr))
	{
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	if (connection == nullptr)
	{
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
	}

	mutex_lock_guard guard{binding_mutex};
	if (instance == nullptr)
	{
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
	}

	const auto received = instance->callbacks_.received(instance->callbacks_.context, connection, data_view{
		.data = static_cast<const std::uint8_t *>(buffer),
		.size = length
	});
	if (!received)
	{
		return BT_GATT_ERR(from_receive_error(received.error().code));
	}

	return length;
}

void zephyr_gatt_server::on_subscription_changed(const bt_gatt_attr *attribute, std::uint16_t value)
{
	(void)attribute;
	(void)value;

	mutex_lock_guard guard{binding_mutex};
	if (instance != nullptr && instance->callbacks_.changed != nullptr)
	{
		instance->callbacks_.changed(instance->callbacks_.context, nullptr);
	}
}

void zephyr_gatt_server::on_mtu_updated(bt_conn *connection, std::uint16_t tx, std::uint16_t rx)
{
	(void)tx;
	(void)rx;

	mutex_lock_guard guard{binding_mutex};
	if (instance != nullptr && instance->callbacks_.changed != nullptr)
	{
		instance->callbacks_.changed(instance->callbacks_.context, connection);
	}
}
}
