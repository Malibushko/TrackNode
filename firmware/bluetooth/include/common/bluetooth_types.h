#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ble
{
using session_type = std::uint32_t;
constexpr std::size_t event_payload_capacity = 128;

struct bluetooth_link
{
	bool connected = false;
	bool subscribed = false;
	std::uint16_t max_payload = 0;
	session_type session = 0;
};

struct bluetooth_event
{
	enum class kind
	{
		idle,
		link_changed,
		received
	} type = kind::idle;

	bluetooth_link link;
	std::array<std::uint8_t, event_payload_capacity> payload{};
	std::size_t size = 0;
};
}
