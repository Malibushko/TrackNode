#pragma once

#include <cstdint>
#include "core/result.h"
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

	std::uint32_t poll_frequency_ms = 500;
	std::uint32_t poll_transport_timeout_ms = 100;
	std::uint32_t poll_bluetooth_timeout_ms = 100;
};

class bridge_service
{
public:
	bridge_service(bridge_config &config);

	status start();
	void stop();
	status poll();

private:

	status handle_frame(ble_bridge::frame &frame);

private:
	bridge_config &config_;
	ble_bridge::bridge_stream_parser parser_;
	std::array<std::uint8_t, ble_bridge::wire_frame_capacity> buffer_{};

	bool initialized_{ false };
};
}
