#pragma once

#include <cstdint>
#include <optional>
#include "core/result.h"
#include "common/bluetooth_types.h"
#include "drivers/protocols/bridge_stream_parser.h"

namespace ble_bridge
{
class bridge_codec;
}

namespace ble
{
class uart;
class bluetooth_backend;

struct bridge_config
{
	uart &transport;
	bluetooth_backend &bluetooth;
	ble_bridge::bridge_codec &codec;
	std::uint32_t (*uptime_ms)() = nullptr;

	std::uint32_t write_timeout_ms = 10;
	std::uint32_t handshake_retry_ms = 1000;
	std::size_t max_poll_rounds = 32;
};

class bridge_service
{
public:
	bridge_service(bridge_config &config);

	status start();
	void stop();

	status poll();
	result<bool> drain();
	[[nodiscard]] std::optional<std::uint32_t> next_wakeup_ms() const;

private:
	status poll_transport();
	status poll_bluetooth();
	status drain_bluetooth();

	status update_handshake();
	status update_state();
	status update_pairing_state();

	status handle_frame(const ble_bridge::frame &frame);
	status handle_link_changed(const bluetooth_event &event);
	status handle_received(const bluetooth_event &event);
	status handle_hello(const ble_bridge::frame &frame);
	status handle_send(const ble_bridge::frame &frame);
	status handle_measurement(const ble_bridge::frame &frame);
	status handle_battery(const ble_bridge::frame &frame);
	status handle_advertise(const ble_bridge::frame &frame);
	status handle_stop_advertise(const ble_bridge::frame &frame);
	status handle_enter_pairing(const ble_bridge::frame &frame);

	status write_frame(const ble_bridge::frame &frame);
	status write_link(ble_bridge::frame_type type, ble_bridge::sequence_type sequence = 0);
	status write_ack(const ble_bridge::frame &request, const status &outcome);
	static ble_bridge::ack_code status_to_ack(const status &outcome);

private:
	bridge_config &config_;
	ble_bridge::bridge_stream_parser parser_;
	std::array<std::uint8_t, ble_bridge::wire_frame_capacity> buffer_{};

	ble::bluetooth_link link_{};
	ble_bridge::session_type bridge_session_ = 0;

	std::uint32_t last_handshake_ms_ = 0;
	bool handshake_requested_ = false;

	bool reported_pairing_ = false;
	bool poll_found_work_ = false;

	bool initialized_{ false };
};
}
