#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ble_bridge
{
constexpr std::uint8_t version = 1;
constexpr std::uint32_t baud_rate = 115200;
constexpr std::size_t payload_capacity = 128;

using sequence_type = std::uint16_t;
using session_type = std::uint32_t;
using length_type = std::uint16_t;
using checksum_type = std::uint16_t;

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
}
