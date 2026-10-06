#pragma once

#include "common/bluetooth_types.h"

namespace ble
{
namespace protocol
{
constexpr std::uint8_t version = 1;
constexpr std::size_t payload_capacity = event_payload_capacity;

using sequence_type = std::uint16_t;
using session_type = ble::session_type;
using length_type = std::uint16_t;
using checksum_type = std::uint16_t;

enum class frame_type : std::uint8_t
{
	hello = 1,
	send = 2,
	ready = 0x81,
	link = 0x82,
	received = 0x83,
	ack = 0x84
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
}

struct bridge_frame
{
	protocol::frame_type type = protocol::frame_type::hello;
	protocol::sequence_type sequence = 0;
	protocol::session_type session = 0;
	protocol::length_type size = 0;
	std::array<std::uint8_t, protocol::payload_capacity> payload{};
};
}
