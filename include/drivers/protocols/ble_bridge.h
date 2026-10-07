#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ble_bridge
{
constexpr std::uint8_t version = 1;
constexpr std::uint32_t baud_rate = 115200;
constexpr std::size_t payload_capacity = 128;

using sequence_type = std::uint16_t;
using session_type = std::uint32_t;
using length_type = std::uint16_t;
using checksum_type = std::uint16_t;

enum class ack_code : std::uint8_t
{
	ok = 0,
	invalid_request = 1,
	not_ready = 2,
	busy = 3,
	unsupported = 4,
	io = 5,
	overflow = 6
};

enum class frame_type : std::uint8_t
{
	hello = 1,
	send = 2,
	ready = 0x81,
	link = 0x82,
	received = 0x83,
	ack = 0x84,
	advertise = 0x85,
	stop_advertise = 0x86,
};

constexpr std::size_t header_size = sizeof(version) + sizeof(frame_type) +
	sizeof(sequence_type) + sizeof(session_type) + sizeof(length_type);
constexpr std::size_t checksum_size = sizeof(checksum_type);
constexpr std::size_t raw_frame_capacity = header_size + payload_capacity + checksum_size;
constexpr std::size_t cobs_block_capacity = 254;
constexpr std::size_t cobs_code_size = 1;
constexpr std::size_t delimiter_size = 1;
constexpr std::size_t wire_frame_capacity = raw_frame_capacity +
	raw_frame_capacity / cobs_block_capacity + cobs_code_size + delimiter_size;

constexpr checksum_type checksum_polynomial = 0x1021;
constexpr checksum_type checksum_initial = 0xffff;
constexpr checksum_type checksum_xor_out = 0;
constexpr bool checksum_reflected = false;

struct frame
{
	frame_type type = frame_type::hello;
	sequence_type sequence = 0;
	session_type session = 0;
	length_type size = 0;
	std::array<std::uint8_t, payload_capacity> payload{};
};

struct link_payload
{
	std::uint8_t connected = 0;
	std::uint8_t subscribed = 0;
	length_type max_payload = 0;
};

constexpr std::size_t link_payload_size = sizeof(link_payload::connected) +
	sizeof(link_payload::subscribed) + sizeof(link_payload::max_payload);

inline void encode_link(frame &message, link_payload link)
{
	message.size = link_payload_size;
	message.payload[0] = link.connected;
	message.payload[1] = link.subscribed;
	message.payload[2] = static_cast<std::uint8_t>(link.max_payload);
	message.payload[3] = static_cast<std::uint8_t>(link.max_payload >> 8);
}

inline std::optional<link_payload> decode_link(const frame &message)
{
	if (message.size != link_payload_size || message.payload[0] > 1 || message.payload[1] > 1)
	{
		return std::nullopt;
	}

	return link_payload{
		.connected = message.payload[0],
		.subscribed = message.payload[1],
		.max_payload = static_cast<length_type>(message.payload[2] | message.payload[3] << 8)
	};
}
}
