#include "drivers/devices/zephyr_bluetooth.h"

#include <cstring>
#include <type_traits>
#include <utility>
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
	{
		spin_lock_guard guard{callback_lock};
		if (initialized_ || instance != nullptr)
		{
			return fail(component::bluetooth, error_code::already_initialized);
		}
	}

	if (config_.name == nullptr || config_.service_uuid == nullptr)
	{
		return fail(component::bluetooth, error_code::invalid_config);
	}

	static_assert(std::is_trivially_copyable_v<raw_event>);
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

	bool published = false;
	{
		spin_lock_guard guard{callback_lock};
		if (instance == nullptr)
		{
			overflow_ = false;
			initialized_ = true;
			instance = this;
			published = true;
		}
	}

	if (!published)
	{
		bt_conn_cb_unregister(&connection_callback_);
		return fail(component::bluetooth, error_code::already_initialized);
	}

	return {};
}

void zephyr_bluetooth::stop()
{
	connection_ref connection;
	{
		spin_lock_guard guard{callback_lock};
		if (!initialized_)
		{
			return;
		}

		instance = nullptr;
		initialized_ = false;
		connection = std::move(active_connection_);
	}

	bt_le_adv_stop();
	bt_conn_cb_unregister(&connection_callback_);
	if (connection)
	{
		bt_conn_disconnect(connection.get(), BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}

	while (receive_event(0))
	{
	}

	k_msgq_purge(&event_queue_);
}

status zephyr_bluetooth::send(data_view data, session_t session)
{
	return fail(component::bluetooth, error_code::not_implemented);
}

result<bluetooth_event> zephyr_bluetooth::poll(std::uint32_t timeout_ms)
{
	if (const auto state = check_state(); !state)
	{
		return tl::unexpected(state.error());
	}

	auto event = receive_event(timeout_ms);
	if (const auto state = check_state(); !state)
	{
		return tl::unexpected(state.error());
	}

	if (!event)
	{
		if (event.error().code == error_code::timeout)
		{
			return bluetooth_event{};
		}

		return tl::unexpected(event.error());
	}

	return from_raw_event(event->data);
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

status zephyr_bluetooth::check_state() const
{
	spin_lock_guard guard{callback_lock};
	if (!initialized_)
	{
		return fail(component::bluetooth, error_code::not_initialized);
	}

	if (overflow_)
	{
		return fail(component::bluetooth, error_code::overflow);
	}

	return {};
}

result<zephyr_bluetooth::queued_event> zephyr_bluetooth::receive_event(std::uint32_t timeout_ms)
{
	raw_event event{};
	const int err = k_msgq_get(&event_queue_, &event, K_MSEC(timeout_ms));
	if (err == -ENOMSG || err == -EAGAIN)
	{
		return fail(component::bluetooth, error_code::timeout);
	}

	if (err != 0)
	{
		return fail(component::bluetooth, from_zephyr_error(err));
	}

	return queued_event{
		event,
		connection_ref{event.connection}
	};
}

void zephyr_bluetooth::push_event(raw_event::type kind, connection_ref &connection, std::uint8_t reason)
{
	const raw_event event{
		kind,
		connection.get(),
		reason
	};
	if (overflow_ || k_msgq_put(&event_queue_, &event, K_NO_WAIT) != 0)
	{
		overflow_ = true;
		return;
	}

	connection.release();
}

bluetooth_event zephyr_bluetooth::from_raw_event(const raw_event &event) const
{
	bluetooth_event ble_event;

	switch (event.kind)
	{
		case raw_event::type::connected:
		{
			ble_event.type = bluetooth_event::kind::link_changed;
			ble_event.link.connected = true;
			break;
		}
		case raw_event::type::disconnected:
		{
			ble_event.type = bluetooth_event::kind::link_changed;
			ble_event.link.connected = false;
			break;
		}
		case raw_event::type::rx:
			ble_event.type = bluetooth_event::kind::received;
			break;
	}

	return ble_event;
}

void zephyr_bluetooth::on_connected(bt_conn *conn, std::uint8_t err)
{
	connection_ref previous;
	connection_ref pending;
	{
		spin_lock_guard guard{callback_lock};
		if (err != 0 || instance == nullptr)
		{
			return;
		}

		previous = std::move(instance->active_connection_);
		instance->active_connection_ = retain_connection(conn);
		pending = retain_connection(conn);
		instance->push_event(raw_event::type::connected, pending, 0);
	}
}

void zephyr_bluetooth::on_disconnected(bt_conn *conn, std::uint8_t reason)
{
	connection_ref previous;
	connection_ref pending;
	{
		spin_lock_guard guard{callback_lock};
		if (instance == nullptr)
		{
			return;
		}

		if (instance->active_connection_.get() == conn)
		{
			previous = std::move(instance->active_connection_);
		}

		pending = retain_connection(conn);
		instance->push_event(raw_event::type::disconnected, pending, reason);
	}
}

void zephyr_bluetooth::on_recycled()
{
	// Empty
}
}
