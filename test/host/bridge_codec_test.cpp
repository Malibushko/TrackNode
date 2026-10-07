#include "drivers/protocols/bridge_codec.h"
#include <algorithm>
#include <cassert>
#include <limits>

namespace
{
constexpr std::array<std::uint8_t, 18> golden{
	0x0a, 0x01, 0x02, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
	0x04, 0x01, 0x02, 0x01, 0x04, 0xff, 0x4a, 0x0b, 0x00
};
constexpr std::array<std::uint8_t, 18> wrong_version{
	0x0a, 0x02, 0x02, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
	0x04, 0x01, 0x02, 0x01, 0x04, 0xff, 0xe9, 0x86, 0x00
};
constexpr std::array<std::uint8_t, 18> wrong_length{
	0x0a, 0x01, 0x02, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
	0x03, 0x01, 0x02, 0x01, 0x04, 0xff, 0x0b, 0xc3, 0x00
};
constexpr std::array<std::uint8_t, 14> oversized_payload{
	0x03, 0x01, 0x02, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x02, 0x81, 0x03, 0xb2, 0x60, 0x00
};

void assert_equal(const ble_bridge::frame &left, const ble_bridge::frame &right)
{
	assert(left.type == right.type);
	assert(left.sequence == right.sequence);
	assert(left.session == right.session);
	assert(left.size == right.size);
	assert(std::equal(left.payload.begin(), left.payload.begin() + left.size, right.payload.begin()));
}

void assert_error(const ble_bridge::bridge_codec &codec, std::span<const std::uint8_t> input,
	ble_bridge::codec_error error)
{
	const auto decoded = codec.decode(input);
	assert(!decoded);
	assert(decoded.error() == error);
}
}

int main()
{
	const ble_bridge::bridge_codec codec;
	std::array<std::uint8_t, ble_bridge::wire_frame_capacity> output{};

	ble_bridge::frame example{};
	example.type = ble_bridge::frame_type::send;
	example.sequence = 0x1234;
	example.session = 0x12345678;
	example.size = 4;
	example.payload[0] = 0;
	example.payload[1] = 1;
	example.payload[2] = 0;
	example.payload[3] = 0xff;

	const auto encoded = codec.encode(example, output);
	assert(encoded && *encoded == golden.size());
	assert(std::equal(golden.begin(), golden.end(), output.begin()));
	const auto decoded = codec.decode(golden);
	assert(decoded);
	assert_equal(example, *decoded);

	constexpr std::array types{
		ble_bridge::frame_type::hello,
		ble_bridge::frame_type::send,
		ble_bridge::frame_type::ready,
		ble_bridge::frame_type::link,
		ble_bridge::frame_type::received,
		ble_bridge::frame_type::ack,
		static_cast<ble_bridge::frame_type>(0x7f)
	};

	for (const auto type : types)
	{
		for (std::size_t size = 0; size <= ble_bridge::payload_capacity; ++size)
		{
			for (unsigned pattern = 0; pattern < 4; ++pattern)
			{
				ble_bridge::frame message{};
				message.type = type;
				message.sequence = std::numeric_limits<ble_bridge::sequence_type>::max();
				message.session = std::numeric_limits<ble_bridge::session_type>::max();
				message.size = static_cast<ble_bridge::length_type>(size);

				for (std::size_t index = 0; index < size; ++index)
				{
					message.payload[index] = pattern == 0 ? 0 : pattern == 1 ? 0xff :
						static_cast<std::uint8_t>(pattern == 2 ? index : index * 37 + size);
				}

				const auto written = codec.encode(message, output);
				assert(written && *written <= output.size());
				assert(output[*written - 1] == 0);
				assert(std::find(output.begin(), output.begin() + *written - 1, 0) ==
					output.begin() + *written - 1);
				const auto restored = codec.decode(std::span{output}.first(*written));
				assert(restored);
				assert_equal(message, *restored);

				std::array<std::uint8_t, ble_bridge::wire_frame_capacity + 2> guarded{};
				guarded.fill(0xa5);
				const auto exact = codec.encode(message, std::span{guarded}.subspan(1, *written));
				assert(exact && *exact == *written);
				assert(guarded.front() == 0xa5 && guarded[*written + 1] == 0xa5);

				guarded.fill(0xa5);
				const auto short_output = codec.encode(message, std::span{guarded}.subspan(1, *written - 1));
				assert(!short_output && short_output.error() == ble_bridge::codec_error::output_too_small);
				assert(std::all_of(guarded.begin(), guarded.end(), [](auto byte){ return byte == 0xa5; }));
			}
		}
	}

	assert_error(codec, wrong_version, ble_bridge::codec_error::unsupported_version);
	assert_error(codec, wrong_length, ble_bridge::codec_error::length_mismatch);
	assert_error(codec, oversized_payload, ble_bridge::codec_error::payload_too_large);
	assert_error(codec, {}, ble_bridge::codec_error::missing_delimiter);

	auto damaged = golden;
	damaged[2] ^= 1;
	assert_error(codec, damaged, ble_bridge::codec_error::checksum_mismatch);
	damaged = golden;
	damaged[3] = 0;
	assert_error(codec, damaged, ble_bridge::codec_error::invalid_cobs);
	constexpr std::array<std::uint8_t, 2> invalid_block{3, 0};
	assert_error(codec, invalid_block, ble_bridge::codec_error::invalid_cobs);
	constexpr std::array<std::uint8_t, 2> empty_block{1, 0};
	assert_error(codec, empty_block, ble_bridge::codec_error::frame_too_short);

	for (std::size_t size = 0; size < golden.size(); ++size)
	{
		assert(!codec.decode(std::span{golden}.first(size)));
	}

	example.size = ble_bridge::payload_capacity + 1;
	const auto invalid_message = codec.encode(example, output);
	assert(!invalid_message && invalid_message.error() == ble_bridge::codec_error::payload_too_large);

	std::array<std::uint8_t, ble_bridge::wire_frame_capacity + 1> too_large{};
	assert_error(codec, too_large, ble_bridge::codec_error::frame_too_large);
	std::array<std::uint8_t, golden.size() * 2> concatenated{};
	std::copy(golden.begin(), golden.end(), concatenated.begin());
	std::copy(golden.begin(), golden.end(), concatenated.begin() + golden.size());
	assert_error(codec, concatenated, ble_bridge::codec_error::invalid_cobs);

	std::uint32_t random = 0x31415926;
	for (std::size_t size = 0; size <= output.size(); ++size)
	{
		for (unsigned iteration = 0; iteration < 32; ++iteration)
		{
			for (auto &byte : output)
			{
				random ^= random << 13;
				random ^= random >> 17;
				random ^= random << 5;
				byte = static_cast<std::uint8_t>(random);
			}

			if (size > 0)
			{
				output[size - 1] = 0;
			}

			const auto restored = codec.decode(std::span{output}.first(size));
			if (restored)
			{
				std::array<std::uint8_t, ble_bridge::wire_frame_capacity> reencoded{};
				const auto written = codec.encode(*restored, reencoded);
				assert(written);
				const auto reparsed = codec.decode(std::span{reencoded}.first(*written));
				assert(reparsed);
				assert_equal(*restored, *reparsed);
			}
		}
	}
}
