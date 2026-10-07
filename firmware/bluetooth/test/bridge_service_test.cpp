#include "infrastructure/bridge_service.h"
#include "drivers/uart.h"
#include "drivers/bluetooth_backend.h"
#include <algorithm>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("Bridge service test failed");
	}
}

class fake_uart final : public ble::uart
{
public:
	std::deque<std::vector<std::uint8_t>> chunks;
	bool io_error = false;
	bool running = false;

	ble::status init() override
	{
		running = true;
		return {};
	}

	void stop() override
	{
		running = false;
	}

	ble::status write(ble::data_view, std::uint32_t) override
	{
		return {};
	}

	ble::result<std::size_t> read(ble::mutable_data_view output, std::uint32_t) override
	{
		if (io_error)
		{
			io_error = false;
			return ble::fail(ble::component::uart, ble::error_code::io);
		}

		if (chunks.empty())
		{
			return ble::fail(ble::component::uart, ble::error_code::timeout);
		}

		auto &chunk = chunks.front();
		const auto size = std::min(output.capacity, chunk.size());
		std::copy_n(chunk.begin(), size, output.data);
		chunk.erase(chunk.begin(), chunk.begin() + size);
		if (chunk.empty())
		{
			chunks.pop_front();
		}

		return size;
	}
};

class fake_bluetooth final : public ble::bluetooth_backend
{
public:
	unsigned advertisements = 0;
	unsigned stops = 0;
	bool fail_init = false;
	bool overflow = false;

	ble::status init() override
	{
		if (fail_init)
		{
			return ble::fail(ble::component::bluetooth, ble::error_code::io);
		}

		return {};
	}

	void stop() override {}

	ble::status start_advertise() override
	{
		++advertisements;
		return {};
	}

	ble::status stop_advertise() override
	{
		++stops;
		return {};
	}

	ble::status send(ble::data_view, ble::session_t) override
	{
		return {};
	}

	ble::result<ble::bluetooth_event> poll(std::uint32_t) override
	{
		if (overflow)
		{
			return ble::fail(ble::component::bluetooth, ble::error_code::overflow);
		}

		return ble::bluetooth_event{};
	}
};

int main()
{
	fake_uart transport;
	fake_bluetooth bluetooth;
	ble_bridge::bridge_codec codec;
	ble::bridge_config config{
		.transport = transport,
		.bluetooth = bluetooth,
		.codec = codec,
		.poll_transport_timeout_ms = 0,
		.poll_bluetooth_timeout_ms = 0
	};
	ble::bridge_service bridge{config};
	const auto encode = [&](ble_bridge::frame_type type)
	{
		std::vector<std::uint8_t> bytes(ble_bridge::wire_frame_capacity);
		const auto size = codec.encode(ble_bridge::frame{
			.type = type
		}, bytes).value();
		bytes.resize(size);
		return bytes;
	};
	const auto advertise = encode(ble_bridge::frame_type::advertise);
	const auto stop = encode(ble_bridge::frame_type::stop_advertise);

	require(bridge.poll().error().code == ble::error_code::not_initialized);
	require(bridge.start().has_value());
	transport.chunks.emplace_back(advertise.begin(), advertise.begin() + 5);
	require(bridge.poll().has_value());
	require(bluetooth.advertisements == 0);
	require(bridge.poll().has_value());
	transport.chunks.emplace_back(advertise.begin() + 5, advertise.end());
	require(bridge.poll().has_value());
	require(bluetooth.advertisements == 1);

	auto combined = advertise;
	combined.insert(combined.end(), stop.begin(), stop.end());
	transport.chunks.push_back(combined);
	require(bridge.poll().has_value());
	require(bluetooth.advertisements == 2 && bluetooth.stops == 1);

	transport.chunks.emplace_back(ble_bridge::wire_frame_capacity + 20, 1);
	transport.chunks.back().push_back(0);
	transport.chunks.back().insert(transport.chunks.back().end(), advertise.begin(), advertise.end());
	while (!transport.chunks.empty())
	{
		require(bridge.poll().has_value());
	}
	require(bluetooth.advertisements == 3);

	transport.chunks.emplace_back(advertise.begin(), advertise.begin() + 4);
	require(bridge.poll().has_value());
	transport.io_error = true;
	require(bridge.poll().error().code == ble::error_code::io);
	transport.chunks.push_back(advertise);
	require(bridge.poll().has_value());
	require(bluetooth.advertisements == 4);

	transport.chunks.emplace_back(advertise.begin(), advertise.begin() + 4);
	require(bridge.poll().has_value());
	bridge.stop();
	require(bridge.start().has_value());
	transport.chunks.push_back(advertise);
	require(bridge.poll().has_value());
	require(bluetooth.advertisements == 5);
	bluetooth.overflow = true;
	require(bridge.poll().error().code == ble::error_code::overflow);
	bridge.stop();
	bluetooth.fail_init = true;
	require(!bridge.start());
	require(!transport.running);

	std::cout << "Bridge UART fragments, multiple commands, errors and restart passed\n";
	return 0;
}
