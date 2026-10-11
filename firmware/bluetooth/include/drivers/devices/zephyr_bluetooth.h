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
};

class zephyr_bluetooth final : public bluetooth_backend
{
public:
	explicit zephyr_bluetooth(bluetooth_config config);
	~zephyr_bluetooth() override;
	status init() override;
	void stop() override;
	status send(data_view data, session_t session) override;
	void set_receive_session(session_t session) override;
	result<bluetooth_event> poll(std::uint32_t timeout_ms) override;

	status start_advertise() override;
	status stop_advertise() override;

private: // Zephyr callbacks
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
	static status on_rx_received(void *context, bt_conn *connection, data_view data);
	static void on_gatt_changed(void *context, bt_conn *connection);

	[[nodiscard]] status check_state() const;
	status push_event(raw_event::type kind, connection_ref &connection, std::uint8_t reason, data_view data = {});
	result<queued_event> receive_event(std::uint32_t timeout_ms);

	[[nodiscard]] bluetooth_event from_raw_event(const raw_event &event) const;

private:
	bluetooth_config config_;
	zephyr_gatt_server gatt_;

	k_msgq event_queue_{};
	bt_conn_cb connection_callback_{};
	char event_queue_buffer[event_queue_depth * sizeof(raw_event)];
	bt_data advertise_data_[2]{};

	connection_ref active_connection_;
	bluetooth_link link_{};
	session_t last_session_ = 0;
	session_t receive_session_ = 0;
	bool initialized_{false};
	bool overflow_ = false;
};
}
