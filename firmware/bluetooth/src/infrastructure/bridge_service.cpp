#include "infrastructure/bridge_service.h"

#include "drivers/bluetooth_backend.h"
#include "drivers/uart.h"
#include "drivers/protocols/bridge_codec.h"
#include <algorithm>

namespace ble
{
bridge_service::bridge_service(bridge_config &config) : config_(config), parser_(config.codec)
{
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::start()
{
	if (initialized_)
	{
		return fail(component::service, error_code::already_initialized);
	}
	if (!config_.uptime_ms || config_.handshake_retry_ms == 0)
	{
		return fail(component::service, error_code::invalid_config);
	}

	const auto started = config_.transport.init()
		.and_then([&]{ return config_.bluetooth.init(); });
	if (!started)
	{
		stop();
		return started;
	}

	parser_.reset();
	link_ = {};
	bridge_session_ = 0;
	config_.bluetooth.set_receive_session(0);
	config_.bluetooth.reset_measurement();
	handshake_requested_ = false;
	initialized_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

void bridge_service::stop()
{
	config_.bluetooth.set_receive_session(0);
	initialized_ = false;
	parser_.reset();
	link_ = {};
	bridge_session_ = 0;
	handshake_requested_ = false;

	config_.transport.stop();
	config_.bluetooth.stop();
}

//////////////////////////////////////////////////////////////////////////


status bridge_service::poll()
{
	if (!initialized_)
	{
		return fail(component::service, error_code::not_initialized);
	}

	poll_found_work_ = false;
	return poll_bluetooth()
		.and_then([&]{ return poll_transport(); })
		.and_then([&]{ return update_handshake(); })
		.and_then([&]{ return update_state(); });
}

//////////////////////////////////////////////////////////////////////////

result<bool> bridge_service::drain()
{
	for (std::size_t round = 0; round < config_.max_poll_rounds; ++round)
	{
		if (const auto polled = poll(); !polled)
		{
			return tl::unexpected(polled.error());
		}
		if (!poll_found_work_)
		{
			return false;
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////

std::optional<std::uint32_t> bridge_service::next_wakeup_ms() const
{
	auto earliest = config_.bluetooth.next_deadline_ms();
	const auto handshake_pending = initialized_ && bridge_session_ == 0;
	if (handshake_pending)
	{
		const auto elapsed = config_.uptime_ms() - last_handshake_ms_;
		const auto retry_wait_running = handshake_requested_ && elapsed < config_.handshake_retry_ms;
		const std::uint32_t until_retry = retry_wait_running ? config_.handshake_retry_ms - elapsed : 0u;
		if (!earliest || until_retry < *earliest)
		{
			earliest = until_retry;
		}
	}

	return earliest;
}

//////////////////////////////////////////////////////////////////////////


status bridge_service::poll_transport()
{
	const auto received = config_.transport.read({buffer_.data(), buffer_.size()}, 0);
	if (!received)
	{
		if (received.error().code == error_code::timeout)
		{
			return {};
		}

		parser_.reset();
		return tl::unexpected(received.error());
	}

	poll_found_work_ = poll_found_work_ || *received != 0;
	for (std::size_t index = 0; index < *received; ++index)
	{
		const auto event = parser_.feed(buffer_[index]);
		if (event.type == ble_bridge::stream_event_type::frame)
		{
			if (const auto handled = handle_frame(event.value); !handled)
			{
				return handled;
			}
		}
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::poll_bluetooth()
{
	const auto event = config_.bluetooth.poll(0);
	if (!event)
	{
		return tl::unexpected(event.error());
	}

	poll_found_work_ = poll_found_work_ || event->type != bluetooth_event::kind::idle;
	switch (event->type)
	{
		case bluetooth_event::kind::link_changed:
			return handle_link_changed(*event);
		case bluetooth_event::kind::received:
			return handle_received(*event);
		default:
			return {};
	}
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::drain_bluetooth()
{
	for (;;)
	{
		const auto event = config_.bluetooth.poll(0);
		if (!event)
		{
			return tl::unexpected(event.error());
		}
		if (event->type == bluetooth_event::kind::idle)
		{
			return {};
		}
		if (event->type == bluetooth_event::kind::link_changed)
		{
			link_ = event->link;
		}
	}
}

//////////////////////////////////////////////////////////////////////////


status bridge_service::update_handshake()
{
	if (bridge_session_ != 0)
	{
		return {};
	}
	const auto now = config_.uptime_ms();
	const auto awaiting_handshake_retry = handshake_requested_ && now - last_handshake_ms_ < config_.handshake_retry_ms;
	if (awaiting_handshake_retry)
	{
		return {};
	}
	const std::uint8_t delimiter = 0;
	const auto sent = config_.transport.write({&delimiter, ble_bridge::delimiter_size}, config_.write_timeout_ms)
		.and_then([&]{ return write_frame(ble_bridge::frame{}); });
	if (sent)
	{
		last_handshake_ms_ = now;
		handshake_requested_ = true;
	}
	return sent;
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::update_state()
{
	return update_pairing_state();
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::update_pairing_state()
{
	const bool active = config_.bluetooth.pairing_active();
	const auto handshake_incomplete = bridge_session_ == 0;
	const auto pairing_state_already_reported = active == reported_pairing_;
	if (handshake_incomplete || pairing_state_already_reported)
	{
		return {};
	}
	ble_bridge::frame state{
		.type = ble_bridge::frame_type::pairing_state,
		.session = bridge_session_,
		.size = 1
	};
	state.payload[0] = active;
	const auto sent = write_frame(state);
	if (sent)
	{
		reported_pairing_ = active;
	}
	return sent;
}

//////////////////////////////////////////////////////////////////////////


status bridge_service::handle_frame(const ble_bridge::frame &frame)
{
	const auto request_outside_established_session = frame.type != ble_bridge::frame_type::hello &&
		(bridge_session_ == 0 || frame.session != bridge_session_);
	if (request_outside_established_session)
	{
		return write_ack(frame, fail(component::service, error_code::not_ready));
	}
	switch (frame.type)
	{
		case ble_bridge::frame_type::hello:
			return handle_hello(frame);
		case ble_bridge::frame_type::send:
			return handle_send(frame);
		case ble_bridge::frame_type::enter_pairing:
			return handle_enter_pairing(frame);
		case ble_bridge::frame_type::measurement:
			return handle_measurement(frame);
		case ble_bridge::frame_type::battery:
			return handle_battery(frame);
		case ble_bridge::frame_type::advertise:
			return handle_advertise(frame);
		case ble_bridge::frame_type::stop_advertise:
			return handle_stop_advertise(frame);
		default:
			return write_ack(frame, fail(component::protocol, error_code::not_supported));
	}
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_link_changed(const bluetooth_event &event)
{
	const auto new_client_requires_handshake = event.link.connected && (!link_.connected || event.link.session != link_.session);
	if (new_client_requires_handshake)
	{
		config_.bluetooth.set_receive_session(0);
		bridge_session_ = 0;
		handshake_requested_ = false;
	}
	link_ = event.link;
	if (bridge_session_ == 0)
	{
		return {};
	}
	return write_link(ble_bridge::frame_type::link);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_received(const bluetooth_event &event)
{
	const auto data_from_inactive_exchange = bridge_session_ == 0 || !link_.connected || event.link.session != link_.session;
	if (data_from_inactive_exchange)
	{
		return {};
	}
	if (event.size > ble_bridge::payload_capacity)
	{
		return fail(component::protocol, error_code::overflow);
	}

	ble_bridge::frame frame{
		.type = ble_bridge::frame_type::received,
		.session = bridge_session_,
		.size = static_cast<ble_bridge::length_type>(event.size)
	};
	std::copy_n(event.payload.begin(), event.size, frame.payload.begin());
	return write_frame(frame);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_hello(const ble_bridge::frame &frame)
{
	const auto invalid_handshake_request = frame.size != 0 || frame.session == 0 || frame.sequence == 0;
	if (invalid_handshake_request)
	{
		return write_ack(frame, fail(component::protocol, error_code::invalid_config));
	}

	config_.bluetooth.reset_measurement();
	if (frame.session != bridge_session_)
	{
		config_.bluetooth.set_receive_session(0);
		bridge_session_ = 0;
		if (const auto drained = drain_bluetooth(); !drained)
		{
			return drained;
		}
		bridge_session_ = frame.session;
	}
	const auto sent = write_link(ble_bridge::frame_type::ready, frame.sequence);
	if (sent)
	{
		config_.bluetooth.set_receive_session(link_.connected ? link_.session : 0);
		reported_pairing_ = !config_.bluetooth.pairing_active();
		return update_pairing_state();
	}
	return sent;
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_send(const ble_bridge::frame &frame)
{
	const auto notification_channel_unavailable = !link_.connected || !link_.subscribed;
	if (notification_channel_unavailable)
	{
		return write_ack(frame, fail(component::service, error_code::not_ready));
	}
	const auto sent = config_.bluetooth.send({frame.payload.data(), frame.size}, link_.session);
	return write_ack(frame, sent);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_measurement(const ble_bridge::frame &frame)
{
	const auto value = bluetooth_measurement::decode(std::span{frame.payload}.first(frame.size));
	if (!value)
	{
		return write_ack(frame, fail(component::protocol, error_code::invalid_config));
	}
	return write_ack(frame, config_.bluetooth.update_measurement(*value));
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_battery(const ble_bridge::frame &frame)
{
	const auto value = bluetooth_battery::decode(std::span{frame.payload}.first(frame.size));
	if (!value)
	{
		return write_ack(frame, fail(component::protocol, error_code::invalid_config));
	}
	return write_ack(frame, config_.bluetooth.update_battery(*value));
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_advertise(const ble_bridge::frame &frame)
{
	if (frame.size != 0)
	{
		return write_ack(frame, fail(component::protocol, error_code::invalid_config));
	}

	return write_ack(frame, config_.bluetooth.start_advertise());
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_stop_advertise(const ble_bridge::frame &frame)
{
	if (frame.size != 0)
	{
		return write_ack(frame, fail(component::protocol, error_code::invalid_config));
	}

	return write_ack(frame, config_.bluetooth.stop_advertise());
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_enter_pairing(const ble_bridge::frame &frame)
{
	if (frame.size != 0)
	{
		return write_ack(frame, fail(component::protocol, error_code::invalid_config));
	}
	config_.bluetooth.set_receive_session(0);
	const auto entered = config_.bluetooth.open_pairing_window();
	if (entered)
	{
		link_.connected = false;
		link_.subscribed = false;
	}
	return write_ack(frame, entered);
}

//////////////////////////////////////////////////////////////////////////


status bridge_service::write_frame(const ble_bridge::frame &frame)
{
	std::array<std::uint8_t, ble_bridge::wire_frame_capacity> output{};
	const auto encoded = config_.codec.encode(frame, output);
	if (!encoded)
	{
		return fail(component::protocol, error_code::invalid_config);
	}

	return config_.transport.write({output.data(), *encoded}, config_.write_timeout_ms);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::write_link(ble_bridge::frame_type type, ble_bridge::sequence_type sequence)
{
	ble_bridge::frame frame{
		.type = type,
		.sequence = sequence,
		.session = bridge_session_
	};
	ble_bridge::encode_link(frame, {
		.connected = static_cast<std::uint8_t>(link_.connected),
		.subscribed = static_cast<std::uint8_t>(link_.subscribed),
		.max_payload = link_.max_payload
	});
	return write_frame(frame);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::write_ack(const ble_bridge::frame &request, const status &outcome)
{
	ble_bridge::frame reply{
		.type = ble_bridge::frame_type::ack,
		.sequence = request.sequence,
		.session = request.session,
		.size = 1
	};
	reply.payload[0] = static_cast<std::uint8_t>(status_to_ack(outcome));
	return write_frame(reply);
}

//////////////////////////////////////////////////////////////////////////

ble_bridge::ack_code bridge_service::status_to_ack(const status &outcome)
{
	using ble_bridge::ack_code;
	if (outcome)
	{
		return ack_code::ok;
	}

	switch (outcome.error().code)
	{
		case error_code::invalid_config:
			return ack_code::invalid_request;
		case error_code::not_connected:
		case error_code::access_denied:
		case error_code::not_ready:
		case error_code::not_initialized:
			return ack_code::not_ready;
		case error_code::busy:
		case error_code::no_resources:
			return ack_code::busy;
		case error_code::not_supported:
		case error_code::not_implemented:
			return ack_code::unsupported;
		case error_code::overflow:
			return ack_code::overflow;
		default:
			return ack_code::io;
	}
}
}
