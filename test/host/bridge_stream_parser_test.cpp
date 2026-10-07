#include "drivers/protocols/bridge_stream_parser.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("Bridge stream parser test failed");
	}
}

int main()
{
	ble_bridge::bridge_codec codec;
	ble_bridge::bridge_stream_parser parser{codec};
	ble_bridge::frame message{
		.type = ble_bridge::frame_type::send,
		.sequence = 42,
		.session = 17,
		.size = ble_bridge::payload_capacity
	};
	for (std::size_t i = 0; i < message.payload.size(); ++i)
	{
		message.payload[i] = static_cast<std::uint8_t>(i);
	}

	std::array<std::uint8_t, ble_bridge::wire_frame_capacity> encoded{};
	const auto size = codec.encode(message, encoded).value();
	for (std::size_t split = 0; split <= size; ++split)
	{
		parser.reset();
		unsigned frames = 0;
		const auto consume = [&](std::size_t begin, std::size_t end)
		{
			for (std::size_t i = begin; i < end; ++i)
			{
				const auto event = parser.feed(encoded[i]);
				if (i + 1 != size)
				{
					require(event.type == ble_bridge::stream_event_type::none);
				}
				else
				{
					require(event.type == ble_bridge::stream_event_type::frame);
					require(event.value.sequence == message.sequence);
					require(event.value.session == message.session);
					require(event.value.size == message.size);
					require(event.value.payload == message.payload);
					++frames;
				}
			}
		};
		consume(0, split);
		consume(split, size);
		require(frames == 1);
	}

	parser.reset();
	unsigned frames = 0;
	for (unsigned repetition = 0; repetition < 5; ++repetition)
	{
		for (std::size_t i = 0; i < size; ++i)
		{
			frames += parser.feed(encoded[i]).type == ble_bridge::stream_event_type::frame;
		}
	}
	require(frames == 5);
	require(parser.feed(0).type == ble_bridge::stream_event_type::none);
	require(parser.feed(0).type == ble_bridge::stream_event_type::none);

	parser.reset();
	unsigned overflows = 0;
	for (std::size_t i = 0; i < ble_bridge::wire_frame_capacity + 100; ++i)
	{
		overflows += parser.feed(1).type == ble_bridge::stream_event_type::frame_too_long;
	}
	require(overflows == 1);
	require(parser.feed(0).type == ble_bridge::stream_event_type::none);
	for (std::size_t i = 0; i < size; ++i)
	{
		const auto event = parser.feed(encoded[i]);
		if (i + 1 == size)
		{
			require(event.type == ble_bridge::stream_event_type::frame);
		}
	}

	parser.reset();
	for (std::size_t i = 0; i < ble_bridge::wire_frame_capacity; ++i)
	{
		require(parser.feed(1).type == ble_bridge::stream_event_type::none);
	}
	require(parser.feed(0).type == ble_bridge::stream_event_type::frame_too_long);
	for (std::size_t i = 0; i < size; ++i)
	{
		const auto event = parser.feed(encoded[i]);
		if (i + 1 == size)
		{
			require(event.type == ble_bridge::stream_event_type::frame);
		}
	}

	auto damaged = encoded;
	damaged[1] ^= 0x20;
	for (std::size_t i = 0; i < size; ++i)
	{
		const auto event = parser.feed(damaged[i]);
		if (i + 1 == size)
		{
			require(event.type == ble_bridge::stream_event_type::parse_error);
		}
	}
	for (std::size_t i = 0; i < size / 2; ++i)
	{
		parser.feed(encoded[i]);
	}
	parser.reset();
	for (std::size_t i = 0; i < size; ++i)
	{
		const auto event = parser.feed(encoded[i]);
		if (i + 1 == size)
		{
			require(event.type == ble_bridge::stream_event_type::frame);
		}
	}

	std::cout << "Stream splits, coalescing, bounds, corruption, reset and resync passed\n";
	return 0;
}
