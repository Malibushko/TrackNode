#pragma once

#include "drivers/bluetooth_backend.h"
#include "drivers/protocols/bridge_stream_parser.h"

class uart;

class xiao_ble_bridge final : public bluetooth_backend
{
public:
	explicit xiao_ble_bridge(uart &transport) : transport_(transport), parser_(codec_) {}

	status init() override;
	void stop() override;

	status start_advertise() override;
	status stop_advertise() override;
	status open_pairing_window() override;

	status send(std::span<const std::uint8_t> payload, std::uint32_t session) override;
	status publish_measurement(std::span<const std::uint8_t> payload) override;
	status publish_battery(std::span<const std::uint8_t> payload) override;

	result<bluetooth_event> poll(std::uint32_t timeout_ms) override;

private:
	status check_transport_overflow();
	status consume_bytes(std::span<const std::uint8_t> bytes);
	status wait_and_read_frames(std::uint32_t timeout_ms);
	status read_frames(std::uint32_t timeout_ms);
	bool should_handle_frame(const ble_bridge::frame &frame) const;

	status handle_frame(const ble_bridge::frame &frame);
	status handle_ack(const ble_bridge::frame &frame);
	status handle_hello(const ble_bridge::frame &frame);
	status handle_ready(const ble_bridge::frame &frame);
	status handle_link(const ble_bridge::frame &frame);
	status handle_received(const ble_bridge::frame &frame);

	status send_and_wait_for_response(ble_bridge::frame &frame, ble_bridge::frame_type response);
	status write_frame(const ble_bridge::frame &frame);
	status wait_for_response();

	result<bluetooth_link> decode_link(const ble_bridge::frame &frame) const;
	void update_link(bluetooth_link next);

	status enqueue(const bluetooth_event &event);

private:
	uart &transport_;
	ble_bridge::bridge_codec codec_;
	ble_bridge::bridge_stream_parser parser_;

	std::array<bluetooth_event, 8> events_{};
	std::size_t event_head_ = 0;
	std::size_t event_count_ = 0;

	bluetooth_link link_{};
	std::uint32_t session_counter_ = 0;
	ble_bridge::session_type remote_session_ = 0;

	ble_bridge::sequence_type sequence_ = 0;
	ble_bridge::frame_type expected_type_ = ble_bridge::frame_type::ready;
	ble_bridge::sequence_type pending_sequence_ = 0;
	ble_bridge::session_type pending_session_ = 0;
	ble_bridge::ack_code response_ = ble_bridge::ack_code::ok;
	bool answered_ = false;
	bool waiting_ = false;

	std::uint32_t dropped_ = 0;
	bool initialized_ = false;
};
