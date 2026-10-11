#pragma once

#include "core/zephyr_raii.h"

#include "drivers/bluetooth_backend.h"
#include "drivers/devices/zephyr_gatt_server.h"
#include "zephyr/kernel.h"
#include "zephyr/bluetooth/conn.h"

struct bt_conn;

namespace ble
{
struct bluetooth_config
{
	const char *name = nullptr;
	std::uint32_t pairing_window_ms = 60000;
	std::uint32_t security_timeout_ms = 15000;
};

class zephyr_bluetooth final : public bluetooth_backend
{
public:
	explicit zephyr_bluetooth(bluetooth_config config);
	~zephyr_bluetooth() override;

	status init() override;
	void stop() override;

	status start_advertise() override;
	status stop_advertise() override;

	status open_pairing_window() override;
	bool pairing_active() const override;
	bool authorized(bt_conn *connection) const;

	status send(data_view data, session_t session) override;
	void set_receive_session(session_t session) override;

	status update_measurement(const bluetooth_measurement::snapshot &value) override;
	void reset_measurement() override;

	result<bluetooth_event> poll(std::uint32_t timeout_ms) override;

private:
	static constexpr std::size_t event_queue_depth = 8;

	struct raw_event
	{
		enum class type : std::uint8_t { connected, disconnected, link_updated, rx } kind;
		bt_conn* connection;
		std::uint8_t reason;
		bluetooth_link link;
		std::array<std::uint8_t, event_payload_capacity> payload;
		std::size_t size;
	};

	struct queued_event
	{
		raw_event data;
		connection_ref connection;
	};

	static void on_connected(bt_conn* conn, std::uint8_t err);
	static void on_disconnected(bt_conn* conn, std::uint8_t reason);
	static void on_recycled();
	static void on_security_changed(bt_conn *connection, bt_security_t level, bt_security_err error);
	static bt_security_err on_pairing_accept(bt_conn *connection, const bt_conn_pairing_feat *features);
	static void on_pairing_complete(bt_conn *connection, bool bonded);
	static void on_pairing_failed(bt_conn *connection, bt_security_err error);
	static bool on_authorized(void *context, bt_conn *connection);
	static status on_rx_received(void *context, bt_conn *connection, data_view data);
	static void on_gatt_changed(void *context, bt_conn *connection);

	void update_security(bt_conn *connection);
	bool reject_bonded_reconnect(bt_conn *connection);
	void process_security();
	connection_ref revoke_current_connection_locked();
	status disconnect_current_client(const connection_ref &connection);
	void close_pairing_window_locked();

	[[nodiscard]] status check_state() const;

	status push_event(raw_event::type kind, connection_ref &connection, std::uint8_t reason, data_view data = {});
	result<queued_event> receive_event(std::uint32_t timeout_ms);
	[[nodiscard]] bluetooth_event from_raw_event(const raw_event &event) const;

private:
	bluetooth_config config_;
	zephyr_gatt_server gatt_;

	k_msgq event_queue_{};
	char event_queue_buffer[event_queue_depth * sizeof(raw_event)];

	bt_conn_cb connection_callback_{};
	bt_conn_auth_cb auth_callback_{};
	bt_conn_auth_info_cb auth_info_callback_{};
	bool auth_registered_ = false;
	bool auth_info_registered_ = false;

	connection_ref active_connection_;
	bluetooth_link link_{};
	session_t last_session_ = 0;
	session_t receive_session_ = 0;

	std::int64_t pairing_deadline_ = 0;
	std::int64_t security_deadline_ = 0;
	bool known_peer_ = false;
	bool pairing_required_ = false;
	bool revoked_ = false;
	bool pairing_started_ = false;
	bool bonded_peer_ = false;

	bt_data advertise_data_[2]{};
	bool advertise_requested_ = false;

	bool initialized_{false};
	bool overflow_ = false;
};
}
