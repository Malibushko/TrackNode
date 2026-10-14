#include "drivers/devices/zephyr_bluetooth.h"
#include "core/zephyr_error.h"

#include <cstring>
#include <type_traits>
#include <utility>
#include <cerrno>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/settings/settings.h>

namespace ble
{
namespace
{
zephyr_bluetooth *instance = nullptr;
k_spinlock callback_lock{};

struct bond_lookup
{
	const bt_addr_le_t *peer;
	bool found = false;
};

void find_bond(const bt_bond_info *bond, void *context)
{
	auto &lookup = *static_cast<bond_lookup *>(context);
	lookup.found |= bt_addr_le_cmp(&bond->addr, lookup.peer) == 0;
}

//////////////////////////////////////////////////////////////////////////

bond_lookup lookup_bond(bt_conn *connection)
{
	bond_lookup lookup{bt_conn_get_dst(connection)};
	bt_foreach_bond(BT_ID_DEFAULT, find_bond, &lookup);
	return lookup;
}


}

//////////////////////////////////////////////////////////////////////////

zephyr_bluetooth::zephyr_bluetooth(bluetooth_config config) : config_(config), event_queue_buffer{}
{

}

//////////////////////////////////////////////////////////////////////////

zephyr_bluetooth::~zephyr_bluetooth()
{
	stop();
}

//////////////////////////////////////////////////////////////////////////


status zephyr_bluetooth::init()
{
	{
		spin_lock_guard guard{callback_lock};
		if (initialized_ || instance != nullptr)
		{
			return fail(component::bluetooth, error_code::already_initialized);
		}
	}

	if (config_.name == nullptr || config_.pairing_window_ms == 0 || config_.security_timeout_ms == 0)
	{
		return fail(component::bluetooth, error_code::invalid_config);
	}

	static_assert(std::is_trivially_copyable_v<raw_event>);
	k_msgq_init(&event_queue_, event_queue_buffer, sizeof(raw_event), event_queue_depth);

	connection_callback_.connected = &zephyr_bluetooth::on_connected;
	connection_callback_.disconnected = &zephyr_bluetooth::on_disconnected;
	connection_callback_.security_changed = on_security_changed;
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

	if (const int err = settings_load(); err != 0)
	{
		bt_conn_cb_unregister(&connection_callback_);
		return fail(component::bluetooth, from_zephyr_error(err));
	}
	bt_set_bondable(true);
	if (const int err = bt_set_name(config_.name); err != 0)
	{
		bt_conn_cb_unregister(&connection_callback_);
		return fail(component::bluetooth, from_zephyr_error(err));
	}

	auth_callback_.pairing_accept = on_pairing_accept;
	auth_info_callback_.pairing_complete = on_pairing_complete;
	auth_info_callback_.pairing_failed = on_pairing_failed;
	if (const int err = bt_conn_auth_cb_register(&auth_callback_); err != 0)
	{
		bt_conn_cb_unregister(&connection_callback_);
		return fail(component::bluetooth, from_zephyr_error(err));
	}
	auth_registered_ = true;
	if (const int err = bt_conn_auth_info_cb_register(&auth_info_callback_); err != 0)
	{
		bt_conn_auth_cb_register(nullptr);
		auth_registered_ = false;
		bt_conn_cb_unregister(&connection_callback_);
		return fail(component::bluetooth, from_zephyr_error(err));
	}
	auth_info_registered_ = true;

	static const std::uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;

	advertise_data_[0] = BT_DATA(BT_DATA_FLAGS, &flags, sizeof(flags));
	advertise_data_[1] = BT_DATA(BT_DATA_NAME_COMPLETE, config_.name, static_cast<uint8_t>(std::strlen(config_.name)));

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

	if (const auto bound = gatt_.bind(gatt_callbacks{
		.context = this,
		.received = on_rx_received,
		.authorized = on_authorized,
		.changed = on_gatt_changed
	}); !bound)
	{
		stop();
		return bound;
	}

	if (const auto advertised = start_advertise(); !advertised)
	{
		stop();
		return advertised;
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

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
		close_pairing_window_locked();
		initialized_ = false;
		link_ = {};
		receive_session_ = 0;
		connection = std::move(active_connection_);
	}

	if (auth_info_registered_)
	{
		bt_conn_auth_info_cb_unregister(&auth_info_callback_);
		auth_info_registered_ = false;
	}
	if (auth_registered_)
	{
		bt_conn_auth_cb_register(nullptr);
		auth_registered_ = false;
	}
	bt_le_adv_stop();
	bt_conn_cb_unregister(&connection_callback_);
	gatt_.unbind();
	if (connection)
	{
		bt_conn_disconnect(connection.get(), BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}

	while (receive_event(0))
	{
	}

	k_msgq_purge(&event_queue_);
}

//////////////////////////////////////////////////////////////////////////


status zephyr_bluetooth::start_advertise()
{
	const int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertise_data_, std::size(advertise_data_), nullptr, 0);
	if (!err || err == -EALREADY)
	{
		return {};
	}

	return fail(component::bluetooth, from_zephyr_error(err));
}

//////////////////////////////////////////////////////////////////////////

status zephyr_bluetooth::stop_advertise()
{
	const int err = bt_le_adv_stop();
	if (!err || err == -EALREADY)
	{
		return {};
	}

	return fail(component::bluetooth, from_zephyr_error(err));
}

//////////////////////////////////////////////////////////////////////////


status zephyr_bluetooth::open_pairing_window()
{
	connection_ref connection;
	{
		spin_lock_guard guard{callback_lock};
		if (!initialized_)
		{
			return fail(component::bluetooth, error_code::not_initialized);
		}
		pairing_deadline_ = k_uptime_get() + config_.pairing_window_ms;
		connection = revoke_current_connection_locked();
		advertise_requested_ = true;
	}
	return disconnect_current_client(connection);
}

//////////////////////////////////////////////////////////////////////////

bool zephyr_bluetooth::pairing_active() const
{
	spin_lock_guard guard{callback_lock};
	return initialized_ && k_uptime_get() < pairing_deadline_;
}

//////////////////////////////////////////////////////////////////////////

bool zephyr_bluetooth::authorized(bt_conn *connection) const
{
	spin_lock_guard guard{callback_lock};
	return initialized_ && connection && active_connection_.get() == connection && link_.connected && !revoked_;
}

//////////////////////////////////////////////////////////////////////////


status zephyr_bluetooth::send(data_view data, session_t session)
{
	connection_ref connection;
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

		const auto send_from_inactive_session = !link_.connected || !active_connection_ || session == 0 || session != link_.session;
		if (send_from_inactive_session)
		{
			return fail(component::bluetooth, error_code::not_connected);
		}

		connection = retain_connection(active_connection_.get());
	}

	return gatt_.notify(connection.get(), data);
}

//////////////////////////////////////////////////////////////////////////

void zephyr_bluetooth::set_receive_session(session_t session)
{
	spin_lock_guard guard{callback_lock};
	receive_session_ = session;
}

//////////////////////////////////////////////////////////////////////////


status zephyr_bluetooth::update_measurement(const bluetooth_measurement::snapshot &value)
{
	return gatt_.update_measurement(value);
}

//////////////////////////////////////////////////////////////////////////

void zephyr_bluetooth::reset_measurement()
{
	gatt_.reset_measurement();
	gatt_.reset_battery();
}

//////////////////////////////////////////////////////////////////////////

status zephyr_bluetooth::update_battery(const bluetooth_battery::snapshot &value)
{
	return gatt_.update_battery(value);
}

//////////////////////////////////////////////////////////////////////////


result<bluetooth_event> zephyr_bluetooth::poll(std::uint32_t timeout_ms)
{
	if (const auto state = check_state(); !state)
	{
		return tl::unexpected(state.error());
	}

	process_security();
	auto event = receive_event(timeout_ms);
	connection_ref connection;
	{
		spin_lock_guard guard{callback_lock};
		connection = link_.connected ? retain_connection(active_connection_.get()) : connection_ref{};
	}
	gatt_.notify_measurements(connection.get());
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

//////////////////////////////////////////////////////////////////////////


void zephyr_bluetooth::on_connected(bt_conn *conn, std::uint8_t err)
{
	if (err != 0 || conn == nullptr)
	{
		return;
	}
	const auto bond = lookup_bond(conn);
	connection_ref previous;
	bool admitted = false;
	{
		spin_lock_guard guard{callback_lock};
		if (!instance)
		{
			return;
		}
		const auto pairing_window_open = k_uptime_get() < instance->pairing_deadline_;
		const auto bonded_peer_outside_pairing = !pairing_window_open && bond.found;
		admitted = pairing_window_open || bonded_peer_outside_pairing;
		if (admitted)
		{
			previous = std::move(instance->active_connection_);
			instance->active_connection_ = retain_connection(conn);
			instance->receive_session_ = 0;
			instance->known_peer_ = bond.found;
			instance->pairing_required_ = pairing_window_open;
			instance->revoked_ = false;
			instance->bonded_peer_ = false;
			instance->pairing_started_ = false;
			instance->security_deadline_ = k_uptime_get() + instance->config_.security_timeout_ms;
			if (++instance->last_session_ == 0)
			{
				++instance->last_session_;
			}
			instance->link_ = bluetooth_link{
				.session = instance->last_session_
			};
		}
	}
	const auto admission_or_encryption_failed = !admitted || bt_conn_set_security(conn, BT_SECURITY_L2) != 0;
	if (admission_or_encryption_failed)
	{
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

//////////////////////////////////////////////////////////////////////////

void zephyr_bluetooth::on_disconnected(bt_conn *conn, std::uint8_t reason)
{
	connection_ref previous;
	connection_ref pending;
	{
		spin_lock_guard guard{callback_lock};
		const auto disconnect_from_inactive_connection = instance == nullptr || instance->active_connection_.get() != conn;
		if (disconnect_from_inactive_connection)
		{
			return;
		}

		previous = std::move(instance->active_connection_);

		instance->link_.connected = false;
		instance->link_.subscribed = false;
		instance->link_.max_payload = 0;

		pending = retain_connection(conn);
		(void)instance->push_event(raw_event::type::disconnected, pending, reason);
	}
}

//////////////////////////////////////////////////////////////////////////

void zephyr_bluetooth::on_recycled()
{
	spin_lock_guard guard{callback_lock};
	if (instance)
	{
		instance->advertise_requested_ = true;
	}
}

//////////////////////////////////////////////////////////////////////////

void zephyr_bluetooth::on_security_changed(bt_conn *connection, bt_security_t level, bt_security_err error)
{
	const auto security_requirements_not_met = error != BT_SECURITY_ERR_SUCCESS || level < BT_SECURITY_L2 || bt_conn_enc_key_size(connection) != 16;
	if (security_requirements_not_met)
	{
		{
			spin_lock_guard guard{callback_lock};
			const auto affects_current_connection = instance && instance->active_connection_.get() == connection;
			if (affects_current_connection)
			{
				instance->revoked_ = true;
				instance->link_.connected = false;
				instance->receive_session_ = 0;
			}
		}
		bt_conn_disconnect(connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}
	zephyr_bluetooth *self;
	{
		spin_lock_guard guard{callback_lock};
		self = instance;
	}
	if (self && !self->reject_bonded_reconnect(connection))
	{
		self->update_security(connection);
	}
}

//////////////////////////////////////////////////////////////////////////

bt_security_err zephyr_bluetooth::on_pairing_accept(bt_conn *connection, const bt_conn_pairing_feat *features)
{
	constexpr std::uint8_t bonding_flag = 1;
	spin_lock_guard guard{callback_lock};
	const auto pairing_from_inactive_connection = !instance || instance->revoked_ ||
		instance->active_connection_.get() != connection;
	if (pairing_from_inactive_connection)
	{
		return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
	}

	const auto bonding_requested = features && (features->auth_req & bonding_flag);
	if (!bonding_requested)
	{
		return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
	}

	const auto pairing_window_expired = k_uptime_get() >= instance->pairing_deadline_;
	if (pairing_window_expired)
	{
		return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
	}
	instance->pairing_started_ = true;
	return BT_SECURITY_ERR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

void zephyr_bluetooth::on_pairing_complete(bt_conn *connection, bool bonded)
{
	bool accepted = false;
	{
		spin_lock_guard guard{callback_lock};
		const auto pairing_from_inactive_connection = !instance || instance->active_connection_.get() != connection;
		if (pairing_from_inactive_connection)
		{
			return;
		}
		const auto pairing_still_expected = !instance->revoked_ && instance->pairing_started_;
		accepted = bonded && pairing_still_expected &&
			k_uptime_get() < instance->pairing_deadline_ && k_uptime_get() < instance->security_deadline_;
		instance->bonded_peer_ = accepted;
	}
	if (!accepted)
	{
		if (bonded)
		{
			bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(connection));
		}
		bt_conn_disconnect(connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}
	on_security_changed(connection, bt_conn_get_security(connection), BT_SECURITY_ERR_SUCCESS);
}

//////////////////////////////////////////////////////////////////////////

void zephyr_bluetooth::on_pairing_failed(bt_conn *connection, bt_security_err)
{
	{
		spin_lock_guard guard{callback_lock};
		const auto affects_current_connection = instance && instance->active_connection_.get() == connection;
		if (affects_current_connection)
		{
			instance->revoked_ = true;
			instance->link_.connected = false;
			instance->receive_session_ = 0;
		}
	}
	bt_conn_disconnect(connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

//////////////////////////////////////////////////////////////////////////

bool zephyr_bluetooth::on_authorized(void *context, bt_conn *connection)
{
	return static_cast<zephyr_bluetooth *>(context)->authorized(connection);
}

//////////////////////////////////////////////////////////////////////////

status zephyr_bluetooth::on_rx_received(void *context, bt_conn *connection, data_view data)
{
	const auto invalid_received_payload = data.size > event_payload_capacity || (data.size != 0 && data.data == nullptr);
	if (invalid_received_payload)
	{
		return fail(component::bluetooth, error_code::invalid_config);
	}

	auto &self = *static_cast<zephyr_bluetooth *>(context);
	connection_ref pending;
	{
		spin_lock_guard guard{callback_lock};
		const auto backend_stopped_or_replaced = instance != &self || !self.initialized_;
		if (backend_stopped_or_replaced)
		{
			return fail(component::bluetooth, error_code::not_initialized);
		}

		const auto data_from_inactive_connection = !self.link_.connected || self.active_connection_.get() != connection || connection == nullptr;
		if (data_from_inactive_connection)
		{
			return fail(component::bluetooth, error_code::not_connected);
		}
		const auto bridge_handshake_incomplete = self.receive_session_ == 0 || self.receive_session_ != self.link_.session;
		if (bridge_handshake_incomplete)
		{
			return fail(component::bluetooth, error_code::busy);
		}

		pending = retain_connection(connection);
		return self.push_event(raw_event::type::rx, pending, 0, data);
	}
}

//////////////////////////////////////////////////////////////////////////

void zephyr_bluetooth::on_gatt_changed(void *context, bt_conn *changed_connection)
{
	auto &self = *static_cast<zephyr_bluetooth *>(context);
	connection_ref connection;
	bluetooth_link link;
	{
		spin_lock_guard guard{callback_lock};
		const auto gatt_event_from_inactive_connection = instance != &self || !self.link_.connected || !self.active_connection_ ||
			(changed_connection != nullptr && changed_connection != self.active_connection_.get());
		if (gatt_event_from_inactive_connection)
		{
			return;
		}

		connection = retain_connection(self.active_connection_.get());
		link = self.link_;
	}

	link.subscribed = self.gatt_.is_subscribed(connection.get());
	link.max_payload = zephyr_gatt_server::max_payload(connection.get());

	{
		spin_lock_guard guard{callback_lock};
		const auto connection_changed_while_reading_gatt = instance != &self || self.revoked_ || !self.link_.connected ||
			self.active_connection_.get() != connection.get() || self.link_.session != link.session;
		if (connection_changed_while_reading_gatt)
		{
			return;
		}

		self.link_ = link;
		(void)self.push_event(raw_event::type::link_updated, connection, 0);
	}
}

//////////////////////////////////////////////////////////////////////////


void zephyr_bluetooth::update_security(bt_conn *connection)
{
	const auto max_payload = zephyr_gatt_server::max_payload(connection);
	const bool subscribed = gatt_.is_subscribed(connection);
	connection_ref pending;
	{
		spin_lock_guard guard{callback_lock};
		const auto security_result_no_longer_applicable = instance != this || revoked_ ||
			k_uptime_get() >= security_deadline_ || active_connection_.get() != connection || link_.connected;
		const auto required_pairing_incomplete = pairing_required_ && !bonded_peer_;
		if (security_result_no_longer_applicable || required_pairing_incomplete)
		{
			return;
		}
		link_.connected = true;
		link_.max_payload = max_payload;
		link_.subscribed = subscribed;
		if (bonded_peer_)
		{
			close_pairing_window_locked();
		}
		pending = retain_connection(connection);
		(void)push_event(raw_event::type::connected, pending, 0);
	}
}

//////////////////////////////////////////////////////////////////////////

bool zephyr_bluetooth::reject_bonded_reconnect(bt_conn *connection)
{
	{
		spin_lock_guard guard{callback_lock};
		const auto resumed_existing_bond_in_pairing_window = instance == this && !revoked_ &&
			active_connection_.get() == connection && known_peer_ && pairing_required_ && !pairing_started_;
		if (!resumed_existing_bond_in_pairing_window)
		{
			return false;
		}
		revoked_ = true;
		receive_session_ = 0;
	}
	bt_conn_disconnect(connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	return true;
}

//////////////////////////////////////////////////////////////////////////

void zephyr_bluetooth::process_security()
{
	connection_ref expired;
	bool advertise = false;
	{
		spin_lock_guard guard{callback_lock};
		const auto now = k_uptime_get();
		const auto establishing_security = active_connection_ && !link_.connected;
		const auto security_timed_out = now >= security_deadline_;
		const auto required_pairing_missed_window = pairing_required_ && now >= pairing_deadline_;
		if (establishing_security && (security_timed_out || required_pairing_missed_window))
		{
			revoked_ = true;
			receive_session_ = 0;
			expired = retain_connection(active_connection_.get());
		}
		const auto pairing_window_expired = pairing_deadline_ != 0 && now >= pairing_deadline_;
		if (pairing_window_expired)
		{
			close_pairing_window_locked();
		}
		advertise = advertise_requested_ && !active_connection_;
	}
	if (expired)
	{
		bt_conn_auth_cancel(expired.get());
		bt_conn_disconnect(expired.get(), BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
	if (advertise && start_advertise())
	{
		spin_lock_guard guard{callback_lock};
		advertise_requested_ = false;
	}
}

//////////////////////////////////////////////////////////////////////////

connection_ref zephyr_bluetooth::revoke_current_connection_locked()
{
	revoked_ = true;
	pairing_started_ = false;
	receive_session_ = 0;
	link_.connected = false;
	link_.subscribed = false;
	return retain_connection(active_connection_.get());
}

//////////////////////////////////////////////////////////////////////////

status zephyr_bluetooth::disconnect_current_client(const connection_ref &connection)
{
	while (receive_event(0))
	{
	}
	if (connection)
	{
		const int err = bt_conn_disconnect(connection.get(), BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (err != 0 && err != -ENOTCONN)
		{
			return fail(component::bluetooth, from_zephyr_error(err));
		}
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

void zephyr_bluetooth::close_pairing_window_locked()
{
	pairing_deadline_ = 0;
}

//////////////////////////////////////////////////////////////////////////


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

//////////////////////////////////////////////////////////////////////////


status zephyr_bluetooth::push_event(raw_event::type kind, connection_ref &connection, std::uint8_t reason, data_view data)
{
	raw_event event{
		.kind = kind,
		.connection = connection.get(),
		.reason = reason,
		.link = link_,
		.payload = {},
		.size = data.size
	};
	if (data.size != 0)
	{
		std::memcpy(event.payload.data(), data.data, data.size);
	}

	const auto event_stream_overflowed = overflow_ || k_msgq_put(&event_queue_, &event, K_NO_WAIT) != 0;
	if (event_stream_overflowed)
	{
		overflow_ = true;
		return fail(component::bluetooth, error_code::overflow);
	}

	(void)connection.release();
	return {};
}

//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

bluetooth_event zephyr_bluetooth::from_raw_event(const raw_event &event) const
{
	bluetooth_event result;
	result.link = event.link;

	if (event.kind == raw_event::type::rx)
	{
		result.type = bluetooth_event::kind::received;
		result.payload = event.payload;
		result.size = event.size;
	}
	else
	{
		result.type = bluetooth_event::kind::link_changed;
	}

	return result;
}
}
