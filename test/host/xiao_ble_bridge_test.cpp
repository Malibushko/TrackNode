#include "drivers/devices/xiao_ble_bridge.h"
#include "drivers/uart.h"
#include <iostream>
#include <stdexcept>

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("STM32 bridge test failed");
	}
}

int main()
{
	uart transport;
	xiao_ble_bridge backend{transport};
	ble_bridge::bridge_codec codec;
	const auto push = [&](const ble_bridge::frame &frame)
	{
		std::array<std::uint8_t, ble_bridge::wire_frame_capacity> output{};
		const auto size = codec.encode(frame, output).value();
		transport.input.insert(transport.input.end(), output.begin(), output.begin() + size);
	};
	ble_bridge::frame link{
		.type = ble_bridge::frame_type::link,
		.session = 7,
		.size = 4,
		.payload = {1, 1, 20, 0}
	};
	bool answer = true;
	bool inject_rx = false;
	bool reboot_during_send = false;
	unsigned burst = 0;
	auto result_code = ble_bridge::ack_code::ok;
	ble_bridge::frame last_send{};
	transport.on_write = [&](const std::uint8_t *bytes, std::size_t size) -> status
	{
		if (size == 1 && bytes[0] == 0)
		{
			return {};
		}

		const auto request = codec.decode({bytes, size}).value();
		if (!answer)
		{
			return {};
		}
		if (request.type == ble_bridge::frame_type::hello)
		{
			require(request.session != 0);
			link.session = request.session;
			auto ready = link;
			ready.type = ble_bridge::frame_type::ready;
			ready.sequence = request.sequence;
			auto stale = ready;
			stale.session = request.session - 1;
			push(stale);
			push(ble_bridge::frame{});
			push(ready);
		}
		else
		{
			last_send = request;
			if (reboot_during_send)
			{
				push(ble_bridge::frame{});
				return {};
			}
			if (inject_rx)
			{
				push(ble_bridge::frame{
					.type = ble_bridge::frame_type::received,
					.session = link.session,
					.size = 4,
					.payload = {'P', 'I', 'N', 'G'}
				});
			}

			for (unsigned index = 0; index < burst; ++index)
			{
				push(ble_bridge::frame{
					.type = ble_bridge::frame_type::received,
					.session = link.session,
					.size = 1,
					.payload = {'X'}
				});
			}

			push(ble_bridge::frame{
				.type = ble_bridge::frame_type::ack,
				.sequence = static_cast<ble_bridge::sequence_type>(request.sequence + 1),
				.session = request.session,
				.size = 1
			});
			push(ble_bridge::frame{
				.type = ble_bridge::frame_type::ack,
				.sequence = request.sequence,
				.session = request.session + 1,
				.size = 1,
				.payload = {static_cast<std::uint8_t>(ble_bridge::ack_code::io)}
			});
			push(ble_bridge::frame{
				.type = ble_bridge::frame_type::ack,
				.sequence = request.sequence,
				.session = request.session,
				.size = 1,
				.payload = {static_cast<std::uint8_t>(result_code)}
			});
		}

		return {};
	};
	require(!backend.poll(0));
	require(backend.init().has_value());
	bluetooth_event event{};
	do
	{
		event = backend.poll(0).value();
	}
	while (event.type == bluetooth_event::kind::idle);
	require(event.link.connected && event.link.max_payload == 20);
	const auto session = event.link.session;
	const std::array<std::uint8_t, 4> payload{'P', 'O', 'N', 'G'};
	require(!backend.send(payload, session + 1));
	inject_rx = true;
	require(backend.send(payload, session).has_value());
	require(last_send.session == session && last_send.size == 4 && last_send.payload[0] == 'P');
	require(backend.poll(0)->type == bluetooth_event::kind::received);
	inject_rx = false;
	result_code = ble_bridge::ack_code::busy;
	require(backend.send(payload, session).error().code == error_code::queue_full);
	result_code = ble_bridge::ack_code::ok;
	require(backend.start_advertise().has_value());
	require(last_send.type == ble_bridge::frame_type::advertise && last_send.session == session);
	require(backend.stop_advertise().has_value());
	auto disconnected = link;
	transport.chunk_size = ble_bridge::wire_frame_capacity;
	disconnected.payload = {0, 0, 20, 0};
	push(disconnected);
	require(!backend.poll(0)->link.connected);
	std::array<std::uint8_t, 46> measurement{};
	measurement[0] = 1;
	require(backend.publish_measurement(measurement).has_value());
	require(last_send.type == ble_bridge::frame_type::measurement && last_send.session == session && last_send.size == 46);
	std::array<std::uint8_t, 20> battery{};
	battery[0] = 1;
	require(backend.publish_battery(battery).has_value());
	require(last_send.type == ble_bridge::frame_type::battery && last_send.session == session && last_send.size == 20);
	push(link);
	require(backend.poll(0)->link.connected);
	transport.chunk_size = ble_bridge::wire_frame_capacity;
	push(ble_bridge::frame{});
	require(backend.poll(0).error().code == error_code::not_ready);
	require(!backend.send(payload, session));
	require(backend.init().has_value());
	auto restarted = backend.poll(0)->link.session;
	require(restarted != session);
	push(ble_bridge::frame{
		.type = ble_bridge::frame_type::received,
		.session = session,
		.size = 1,
		.payload = {'X'}
	});
	require(backend.poll(0)->type == bluetooth_event::kind::idle);
	reboot_during_send = true;
	require(backend.send(payload, restarted).error().code == error_code::not_ready);
	reboot_during_send = false;
	require(backend.init().has_value());
	require(backend.poll(0)->link.session != restarted);

	backend.stop();
	answer = false;
	require(backend.init().error().code == error_code::timeout_error);
	answer = true;
	require(backend.init().has_value());
	event = backend.poll(0).value();
	require(event.link.session != session);
	require(!backend.send(payload, session));
	answer = false;
	require(backend.send(payload, event.link.session).error().code == error_code::timeout_error);
	require(!backend.poll(0));
	answer = true;
	require(backend.init().has_value());
	(void)backend.poll(0);
	++transport.drops;
	require(backend.poll(0).error().code == error_code::uart_error);
	backend.stop();
	require(backend.init().has_value());
	(void)backend.poll(0);
	transport.chunk_size = ble_bridge::wire_frame_capacity;
	for (unsigned index = 0; index < 10; ++index)
	{
		push(link);
	}
	bool overflow = false;
	for (unsigned index = 0; index < 12; ++index)
	{
		const auto received = backend.poll(0);
		if (!received)
		{
			overflow = true;
			break;
		}
	}
	require(!overflow);
	burst = 9;
	backend.stop();
	require(backend.init().has_value());
	const auto current = backend.poll(0)->link.session;
	require(backend.send(payload, current).error().code == error_code::queue_full);
	require(!backend.poll(0));
	burst = 0;
	backend.stop();
	require(backend.init().has_value());
	(void)backend.poll(0);
	require(backend.open_pairing_window().has_value());
	require(last_send.type == ble_bridge::frame_type::enter_pairing && last_send.size == 0);
	push(ble_bridge::frame{
		.type = ble_bridge::frame_type::pairing_state,
		.session = last_send.session,
		.size = 1,
		.payload = {1}
	});
	const auto pairing = backend.poll(0);
	require(pairing && pairing->type == bluetooth_event::kind::pairing_changed && pairing->pairing);
	require(!backend.send(payload, last_send.session));

	unsigned wake_preambles = 0;
	const auto previous_write = transport.on_write;
	transport.on_write = [&](const std::uint8_t *bytes, std::size_t size) -> status
	{
		if (size == 1 && bytes[0] == 0)
		{
			++wake_preambles;
		}
		return previous_write(bytes, size);
	};
	transport.idle = ble_bridge::peer_sleep_after_ms;
	const auto ticks_before_wake = fake_ticks;
	(void)backend.open_pairing_window();
	const auto idle_peer_woken_first = wake_preambles == 1 && fake_ticks - ticks_before_wake >= ble_bridge::peer_wake_guard_ms;
	require(idle_peer_woken_first);
	transport.idle = 0;
	(void)backend.open_pairing_window();
	require(wake_preambles == 1);
	std::cout << "Handshake, fragmented UART, ACK matching, interleaved RX, stale sessions and timeout passed\n";
}
