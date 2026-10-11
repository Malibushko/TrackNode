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
	environmental_service,
	BT_GATT_PRIMARY_SERVICE(&gatt_uuid::environmental_sensing.uuid),
	BT_GATT_CHARACTERISTIC(&gatt_uuid::temperature.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, zephyr_gatt_server::on_measurement_read, nullptr, nullptr),
	BT_GATT_CCC(zephyr_gatt_server::on_subscription_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&gatt_uuid::pressure.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, zephyr_gatt_server::on_measurement_read, nullptr, nullptr),
	BT_GATT_CCC(zephyr_gatt_server::on_subscription_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

BT_GATT_SERVICE_DEFINE(
	tracknode_service,
	BT_GATT_PRIMARY_SERVICE(&gatt_uuid::service.uuid),
	BT_GATT_CHARACTERISTIC(&gatt_uuid::gnss.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, zephyr_gatt_server::on_measurement_read, nullptr, nullptr),
	BT_GATT_CCC(zephyr_gatt_server::on_subscription_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&gatt_uuid::measurement_status.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, zephyr_gatt_server::on_measurement_read, nullptr, nullptr),
	BT_GATT_CCC(zephyr_gatt_server::on_subscription_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
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

//////////////////////////////////////////////////////////////////////////

const bt_gatt_attr *measurement_attribute(std::size_t index)
{
	const bt_uuid *uuids[]{&gatt_uuid::temperature.uuid, &gatt_uuid::pressure.uuid,
		&gatt_uuid::gnss.uuid, &gatt_uuid::measurement_status.uuid};
	const auto &service = index < 2 ? environmental_service : tracknode_service;
	return bt_gatt_find_by_uuid(service.attrs, service.attr_count, uuids[index]);
}

struct measurement_bytes
{
	std::array<std::uint8_t, 20> data{};
	std::uint16_t size = 0;
};

measurement_bytes encode_characteristic(std::size_t index, const bluetooth_measurement::snapshot &value)
{
	using namespace bluetooth_measurement;
	measurement_bytes bytes;
	if (index < 2 && !(value.flags & sensor_valid))
	{
		return bytes;
	}
	switch (index)
	{
		case 0:
			put(bytes.data, 0, value.temperature);
			bytes.size = 2;
			break;
		case 1:
			put(bytes.data, 0, value.pressure);
			bytes.size = 4;
			break;
		case 2:
		{
			const auto data = measurement_state::gnss_value(value);
			std::copy(data.begin(), data.end(), bytes.data.begin());
			bytes.size = data.size();
			break;
		}
		case 3:
		{
			const auto data = measurement_state::status_value(value);
			std::copy(data.begin(), data.end(), bytes.data.begin());
			bytes.size = data.size();
			break;
		}
	}
	return bytes;
}

//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

zephyr_gatt_server::~zephyr_gatt_server()
{
	unbind();
}

//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

void zephyr_gatt_server::unbind()
{
	mutex_lock_guard guard{binding_mutex};
	if (instance == this)
	{
		instance = nullptr;
		callbacks_ = {};
		measurements_.reset();
	}
}

//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

bool zephyr_gatt_server::is_subscribed(bt_conn *connection) const
{
	const auto *attribute = tx_attribute();

	return connection != nullptr && attribute != nullptr && bt_gatt_is_subscribed(connection, attribute, BT_GATT_CCC_NOTIFY);
}

//////////////////////////////////////////////////////////////////////////

status zephyr_gatt_server::update_measurement(const bluetooth_measurement::snapshot &value)
{
	if (!bluetooth_measurement::valid(value))
	{
		return fail(component::bluetooth, error_code::invalid_config);
	}
	mutex_lock_guard guard{binding_mutex};
	if (instance != this)
	{
		return fail(component::bluetooth, error_code::not_initialized);
	}
	measurements_.update(value, k_uptime_get());
	++revision_;
	pending_ = all_measurements;
	return {};
}

//////////////////////////////////////////////////////////////////////////

void zephyr_gatt_server::reset_measurement()
{
	mutex_lock_guard guard{binding_mutex};
	measurements_.reset();
	++revision_;
	pending_ = all_measurements;
	last_flags_ = 0;
}

//////////////////////////////////////////////////////////////////////////

void zephyr_gatt_server::notify_measurements(bt_conn *connection)
{
	bluetooth_measurement::snapshot value;
	std::uint32_t revision;
	std::uint8_t pending;
	{
		mutex_lock_guard guard{binding_mutex};
		if (instance != this)
		{
			return;
		}
		value = measurements_.current(k_uptime_get());
		if (value.flags != last_flags_)
		{
			++revision_;
			pending_ |= measurement_metadata;
			last_flags_ = value.flags;
		}
		revision = revision_;
		pending = pending_;
	}
	if (!connection)
	{
		return;
	}
	for (std::size_t index = 0; index < 4; ++index)
	{
		const auto mask = static_cast<std::uint8_t>(1 << index);
		const auto *attribute = measurement_attribute(index);
		if (!(pending & mask) || !bt_gatt_is_subscribed(connection, attribute, BT_GATT_CCC_NOTIFY))
		{
			continue;
		}
		const auto bytes = encode_characteristic(index, value);
		if (bytes.size != 0 && bt_gatt_notify(connection, attribute, bytes.data.data(), bytes.size) != 0)
		{
			continue;
		}
		mutex_lock_guard guard{binding_mutex};
		if (revision_ == revision)
		{
			pending_ &= ~mask;
		}
	}
}

//////////////////////////////////////////////////////////////////////////

ssize_t zephyr_gatt_server::on_measurement_read(bt_conn *connection, const bt_gatt_attr *attribute,
	void *buffer, std::uint16_t length, std::uint16_t offset)
{
	measurement_bytes bytes;
	{
		mutex_lock_guard guard{binding_mutex};
		if (!instance)
		{
			return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
		}
		const auto value = instance->measurements_.current(k_uptime_get());
		for (std::size_t index = 0; index < 4; ++index)
		{
			if (attribute == measurement_attribute(index))
			{
				bytes = encode_characteristic(index, value);
				break;
			}
		}
	}
	if (bytes.size == 0)
	{
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}
	return bt_gatt_attr_read(connection, attribute, buffer, length, offset, bytes.data.data(), bytes.size);
}

//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

void zephyr_gatt_server::on_subscription_changed(const bt_gatt_attr *attribute, std::uint16_t value)
{
	(void)attribute;
	(void)value;

	mutex_lock_guard guard{binding_mutex};
	if (instance != nullptr)
	{
		instance->pending_ = all_measurements;
		++instance->revision_;
	}
	if (instance != nullptr && instance->callbacks_.changed != nullptr)
	{
		instance->callbacks_.changed(instance->callbacks_.context, nullptr);
	}
}

//////////////////////////////////////////////////////////////////////////

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
