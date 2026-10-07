#pragma once

#include "core/zephyr_raii.h"

#include "drivers/bluetooth_backend.h"
#include "zephyr/kernel.h"
#include "zephyr/bluetooth/conn.h"

struct bt_conn;

namespace ble
{
struct bluetooth_config
{
	const char *name = nullptr;
	const char *service_uuid = nullptr;
	const char *rx_uuid = nullptr;
	const char *tx_uuid = nullptr;
};

class zephyr_bluetooth final : public bluetooth_backend
{
public:
	explicit zephyr_bluetooth(bluetooth_config config);
	status init() override;
	void stop() override;
	status send(data_view data, session_t session) override;
	result<bluetooth_event> poll(std::uint32_t timeout_ms) override;

	status start_advertise() override;
	status stop_advertise() override;

private: // Zephyr callbacks
	static constexpr std::size_t event_queue_depth = 8;

	struct raw_event
	{
		enum class type : std::uint8_t { connected, disconnected, rx } kind;
		bt_conn* connection;
		std::uint8_t reason;
	};

	struct queued_event
	{
		raw_event data;
		connection_ref connection;
	};

	static void on_connected(bt_conn* conn, std::uint8_t err);
	static void on_disconnected(bt_conn* conn, std::uint8_t reason);
	static void on_recycled();

	status check_state() const;
	void push_event(raw_event::type kind, connection_ref &connection, std::uint8_t reason);
	result<queued_event> receive_event(std::uint32_t timeout_ms);

	bluetooth_event from_raw_event(const raw_event &event) const;

private:
	bluetooth_config config_;
	k_msgq event_queue_{};
	bt_conn_cb connection_callback_{};
	char event_queue_buffer[event_queue_depth * sizeof(raw_event)];
	bt_data advertise_data_[2]{};

	connection_ref active_connection_;
	bool initialized_{false};
	bool overflow_ = false;
};
}
