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

//////////////////////////////////////////////////////////////////////////

	const auto started = config_.transport.init()
		.and_then([&]{ return config_.bluetooth.init(); });
	if (!started)
	{
		stop();
		return started;
	}

//////////////////////////////////////////////////////////////////////////

	parser_.reset();
	link_ = {};
	initialized_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

void bridge_service::stop()
{
	initialized_ = false;
	parser_.reset();
	link_ = {};

	config_.transport.stop();
	config_.bluetooth.stop();
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

//////////////////////////////////////////////////////////////////////////

	return config_.transport.write({output.data(), *encoded}, config_.poll_transport_timeout_ms);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::write_link(ble_bridge::frame_type type, ble_bridge::sequence_type sequence)
{
	ble_bridge::frame frame{
		.type = type,
		.sequence = sequence,
		.session = link_.session
	};
	ble_bridge::encode_link(frame, {
		.connected = static_cast<std::uint8_t>(link_.connected),
		.subscribed = static_cast<std::uint8_t>(link_.subscribed),
		.max_payload = link_.max_payload
	});
	return write_frame(frame);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::poll()
{
	if (!initialized_)
	{
		return fail(component::service, error_code::not_initialized);
	}

//////////////////////////////////////////////////////////////////////////

	return poll_transport().and_then([&]{ return poll_bluetooth(); });
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::poll_transport()
{
	const auto received = config_.transport.read({buffer_.data(), buffer_.size()}, config_.poll_transport_timeout_ms);
	if (!received)
	{
		if (received.error().code == error_code::timeout)
		{
			return {};
		}

//////////////////////////////////////////////////////////////////////////

		parser_.reset();
		return tl::unexpected(received.error());
	}

//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

	return {};
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::poll_bluetooth()
{
	const auto event = config_.bluetooth.poll(config_.poll_bluetooth_timeout_ms);
	if (!event)
	{
		return tl::unexpected(event.error());
	}

//////////////////////////////////////////////////////////////////////////

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

status bridge_service::handle_link_changed(const bluetooth_event &event)
{
	link_ = event.link;
	return write_link(ble_bridge::frame_type::link);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_received(const bluetooth_event &event)
{
	if (event.size > ble_bridge::payload_capacity)
	{
		return fail(component::protocol, error_code::overflow);
	}

//////////////////////////////////////////////////////////////////////////

	ble_bridge::frame frame{
		.type = ble_bridge::frame_type::received,
		.session = event.link.session,
		.size = static_cast<ble_bridge::length_type>(event.size)
	};
	std::copy_n(event.payload.begin(), event.size, frame.payload.begin());
	return write_frame(frame);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_frame(const ble_bridge::frame &frame)
{
	switch (frame.type)
	{
		case ble_bridge::frame_type::hello:
			return handle_hello(frame);
		case ble_bridge::frame_type::send:
			return handle_send(frame);
		case ble_bridge::frame_type::advertise:
			return handle_advertise(frame);
		case ble_bridge::frame_type::stop_advertise:
			return handle_stop_advertise(frame);
		default:
			return write_ack(frame, fail(component::protocol, error_code::not_supported));
	}
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_hello(const ble_bridge::frame &frame)
{
	if (frame.size != 0 || frame.session != 0)
	{
		return write_ack(frame, fail(component::protocol, error_code::invalid_config));
	}

//////////////////////////////////////////////////////////////////////////

	return write_link(ble_bridge::frame_type::ready, frame.sequence);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_send(const ble_bridge::frame &frame)
{
	const auto sent = config_.bluetooth.send({frame.payload.data(), frame.size}, frame.session);
	return write_ack(frame, sent);
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_advertise(const ble_bridge::frame &frame)
{
	if (frame.size != 0 || frame.session != 0)
	{
		return write_ack(frame, fail(component::protocol, error_code::invalid_config));
	}

//////////////////////////////////////////////////////////////////////////

	return write_ack(frame, config_.bluetooth.start_advertise());
}

//////////////////////////////////////////////////////////////////////////

status bridge_service::handle_stop_advertise(const ble_bridge::frame &frame)
{
	if (frame.size != 0 || frame.session != 0)
	{
		return write_ack(frame, fail(component::protocol, error_code::invalid_config));
	}

//////////////////////////////////////////////////////////////////////////

	return write_ack(frame, config_.bluetooth.stop_advertise());
}

//////////////////////////////////////////////////////////////////////////

ble_bridge::ack_code bridge_service::status_to_ack(const status &outcome)
{
	using ble_bridge::ack_code;
	if (outcome)
	{
		return ack_code::ok;
	}

//////////////////////////////////////////////////////////////////////////

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
}
