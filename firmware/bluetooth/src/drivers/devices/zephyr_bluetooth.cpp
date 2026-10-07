#include "drivers/devices/zephyr_bluetooth.h"

#include <cstring>
#include <cerrno>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci_types.h>





namespace ble
{
namespace
{
zephyr_bluetooth *instance = nullptr;
k_spinlock callback_lock{};

error_code from_zephyr_error(int code)
{
	switch (code)
	{
	case -EALREADY:
	case -EEXIST:
		return error_code::already_initialized;
	case -EINVAL:
	case -EMSGSIZE:
		return error_code::invalid_config;
	case -ENOSYS:
	case -ENOTSUP:
		return error_code::not_supported;
	case -ENODEV:
	case -EHOSTDOWN:
		return error_code::not_ready;
	case -ETIMEDOUT:
		return error_code::timeout;
	case -EAGAIN:
	case -EBUSY:
		return error_code::busy;
	case -ENOMEM:
	case -ENOBUFS:
		return error_code::no_resources;
	case -ENOTCONN:
	case -ECONNRESET:
		return error_code::not_connected;
	case -EACCES:
	case -EPERM:
		return error_code::access_denied;
	default:
		return error_code::io;
	}
}
}

zephyr_bluetooth::zephyr_bluetooth(bluetooth_config config) : config_(config), event_queue_buffer{}
{

}

status zephyr_bluetooth::init()
{
	const auto initial_key = k_spin_lock(&callback_lock);
	const bool occupied = initialized_ || instance != nullptr;
	k_spin_unlock(&callback_lock, initial_key);
	if (occupied)
	{
		return fail(component::bluetooth, error_code::already_initialized);
	}

	if (config_.name == nullptr || config_.service_uuid == nullptr)
	{
		return fail(component::bluetooth, error_code::invalid_config);
	}

	k_msgq_init(&event_queue_, event_queue_buffer, sizeof(raw_event), event_queue_depth);

	connection_callback_.connected = &zephyr_bluetooth::on_connected;
	connection_callback_.disconnected = &zephyr_bluetooth::on_disconnected;
	connection_callback_.recycled = &zephyr_bluetooth::on_recycled;
	connection_callback_.le_param_req = nullptr;
	connection_callback_.le_param_updated = nullptr;
	connection_callback_._node = {};

	if (const int err = bt_conn_cb_register(&connection_callback_); err != 0)
	{
		return fail(component::bluetooth, from_zephyr_error(err));
	}

	if (const int err = bt_enable(nullptr); err != 0 && err != -EALREADY)
	{
		bt_conn_cb_unregister(&connection_callback_);
		return fail(component::bluetooth, from_zephyr_error(err));
	}

	if (const int err = bt_set_name(config_.name); err != 0)
	{
		bt_conn_cb_unregister(&connection_callback_);
		return fail(component::bluetooth, from_zephyr_error(err));
	}

	static const std::uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;

	advertise_data_[0] = BT_DATA(BT_DATA_FLAGS, &flags, sizeof(flags));
	advertise_data_[1] = BT_DATA(BT_DATA_NAME_COMPLETE, config_.name, static_cast<uint8_t>(std::strlen(config_.name)));

	// TODO: add GATT server setup

	const auto key = k_spin_lock(&callback_lock);
	if (instance != nullptr)
	{
		k_spin_unlock(&callback_lock, key);
		bt_conn_cb_unregister(&connection_callback_);
		return fail(component::bluetooth, error_code::already_initialized);
	}

	overflow_ = false;
	initialized_ = true;
	instance = this;
	k_spin_unlock(&callback_lock, key);

	return {};
}

void zephyr_bluetooth::stop()
{
	const auto key = k_spin_lock(&callback_lock);
	if (!initialized_)
	{
		k_spin_unlock(&callback_lock, key);
		return;
	}

	instance = nullptr;
	initialized_ = false;
	bt_conn *connection = active_connection_;
	active_connection_ = nullptr;
	k_spin_unlock(&callback_lock, key);

	bt_le_adv_stop();
	bt_conn_cb_unregister(&connection_callback_);
	if (connection != nullptr)
	{
		bt_conn_disconnect(connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(connection);
	}

	raw_event event{};
	while (k_msgq_get(&event_queue_, &event, K_NO_WAIT) == 0)
	{
		if (event.connection != nullptr)
		{
			bt_conn_unref(event.connection);
		}
	}

	k_msgq_purge(&event_queue_);
}

status zephyr_bluetooth::send(data_view data, session_t session)
{
	return fail(component::bluetooth, error_code::not_implemented);
}

result<bluetooth_event> zephyr_bluetooth::poll(std::uint32_t timeout_ms)
{
	const auto key = k_spin_lock(&callback_lock);
	const bool ready = initialized_;
	const bool overflow = overflow_;
	k_spin_unlock(&callback_lock, key);

	if (!ready)
	{
		return fail(component::bluetooth, error_code::not_initialized);
	}

	if (overflow)
	{
		return fail(component::bluetooth, error_code::overflow);
	}

	return fail(component::bluetooth, error_code::not_implemented);
}

status zephyr_bluetooth::start_advertising()
{
	const int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertise_data_, std::size(advertise_data_), nullptr, 0);
	if (!err || err == -EALREADY)
	{
		return {};
	}

	return fail(component::bluetooth, from_zephyr_error(err));
}

status zephyr_bluetooth::stop_advertising()
{
	const int err = bt_le_adv_stop();
	if (!err || err == -EALREADY)
	{
		return {};
	}

	return fail(component::bluetooth, from_zephyr_error(err));
}

bool zephyr_bluetooth::push_event(const raw_event &event)
{
	if (overflow_ || k_msgq_put(&event_queue_, &event, K_NO_WAIT) != 0)
	{
		overflow_ = true;
		return false;
	}

	return true;
}

void zephyr_bluetooth::on_connected(bt_conn *conn, std::uint8_t err)
{
	const auto key = k_spin_lock(&callback_lock);
	if (err != 0 || instance == nullptr)
	{
		k_spin_unlock(&callback_lock, key);
		return;
	}

	bt_conn *previous = instance->active_connection_;
	instance->active_connection_ = bt_conn_ref(conn);
	const raw_event event{
		raw_event::type::connected,
		bt_conn_ref(conn),
		0
	};
	const bool queued = instance->push_event(event);
	k_spin_unlock(&callback_lock, key);

	if (previous != nullptr)
	{
		bt_conn_unref(previous);
	}

	if (!queued)
	{
		bt_conn_unref(event.connection);
	}
}

void zephyr_bluetooth::on_disconnected(bt_conn *conn, std::uint8_t reason)
{
	const auto key = k_spin_lock(&callback_lock);
	if (instance == nullptr)
	{
		k_spin_unlock(&callback_lock, key);
		return;
	}

	bt_conn *previous = nullptr;
	if (instance->active_connection_ == conn)
	{
		previous = instance->active_connection_;
		instance->active_connection_ = nullptr;
	}

	const raw_event event{
		raw_event::type::disconnected,
		bt_conn_ref(conn),
		reason
	};
	const bool queued = instance->push_event(event);
	k_spin_unlock(&callback_lock, key);

	if (previous != nullptr)
	{
		bt_conn_unref(previous);
	}

	if (!queued)
	{
		bt_conn_unref(event.connection);
	}
}

void zephyr_bluetooth::on_recycled()
{
	// Empty
}
}
