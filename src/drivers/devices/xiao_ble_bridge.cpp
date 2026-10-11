#include "drivers/devices/xiao_ble_bridge.h"
#include "drivers/uart.h"
#include <algorithm>

namespace
{
constexpr std::uint32_t response_timeout_ms = 1000;

status ack_to_status(ble_bridge::ack_code code)
{
	switch (code)
	{
		case ble_bridge::ack_code::ok:
			return {};
		case ble_bridge::ack_code::invalid_request:
			return fail(component::service, error_code::invalid_argument);
		case ble_bridge::ack_code::not_ready:
			return fail(component::service, error_code::not_ready);
		case ble_bridge::ack_code::busy:
		case ble_bridge::ack_code::overflow:
			return fail(component::service, error_code::queue_full);
		case ble_bridge::ack_code::unsupported:
			return fail(component::service, error_code::not_implemented);
		default:
			return fail(component::service, error_code::device_error);
	}
}
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::init()
{
	if (initialized_)
	{
		return fail(component::service, error_code::already_initialized);
	}

	const auto started = transport_.init();
	if (!started && started.error().code != error_code::already_initialized)
	{
		return started;
	}

	stop();
	dropped_ = transport_.dropped_count();
	const std::uint8_t delimiter = 0;
	if (const auto resynchronized = transport_.write(&delimiter, ble_bridge::delimiter_size, response_timeout_ms); !resynchronized)
	{
		return resynchronized;
	}

	if (++session_counter_ == 0)
	{
		++session_counter_;
	}
	remote_session_ = session_counter_;
	ble_bridge::frame hello{
		.session = remote_session_
	};
	const auto ready = send_and_wait_for_response(hello, ble_bridge::frame_type::ready);
	initialized_ = ready.has_value();
	return ready;
}

//////////////////////////////////////////////////////////////////////////

void xiao_ble_bridge::stop()
{
	initialized_ = false;
	waiting_ = false;
	answered_ = false;
	parser_.reset();
	link_ = {};
	remote_session_ = 0;
	event_head_ = 0;
	event_count_ = 0;
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::start_advertise()
{
	if (!initialized_)
	{
		return fail(component::service, error_code::not_initialized);
	}

	ble_bridge::frame request{
		.type = ble_bridge::frame_type::advertise,
		.session = remote_session_
	};
	return send_and_wait_for_response(request, ble_bridge::frame_type::ack);
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::stop_advertise()
{
	if (!initialized_)
	{
		return fail(component::service, error_code::not_initialized);
	}

	ble_bridge::frame request{
		.type = ble_bridge::frame_type::stop_advertise,
		.session = remote_session_
	};
	return send_and_wait_for_response(request, ble_bridge::frame_type::ack);
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::send(std::span<const std::uint8_t> payload, std::uint32_t session)
{
	if (!initialized_)
	{
		return fail(component::service, error_code::not_initialized);
	}
	if (!link_.connected || !link_.subscribed || session != link_.session)
	{
		return fail(component::service, error_code::not_ready);
	}
	if (payload.empty() || payload.size() > link_.max_payload || payload.size() > ble_bridge::payload_capacity)
	{
		return fail(component::service, error_code::invalid_argument);
	}

	ble_bridge::frame request{
		.type = ble_bridge::frame_type::send,
		.session = remote_session_,
		.size = static_cast<ble_bridge::length_type>(payload.size())
	};
	std::copy(payload.begin(), payload.end(), request.payload.begin());
	return send_and_wait_for_response(request, ble_bridge::frame_type::ack);
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::send_and_wait_for_response(ble_bridge::frame &frame, ble_bridge::frame_type response)
{
	if (++sequence_ == 0)
	{
		++sequence_;
	}

	frame.sequence = sequence_;
	expected_type_ = response;
	pending_sequence_ = sequence_;
	pending_session_ = frame.session;
	answered_ = false;
	waiting_ = true;

	const auto outcome = write_frame(frame)
							.and_then([&]{ return wait_for_response(); });

	waiting_ = false;
	return outcome;
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::write_frame(const ble_bridge::frame &frame)
{
	std::array<std::uint8_t, ble_bridge::wire_frame_capacity> output{};
	const auto size = codec_.encode(frame, output);
	if (!size)
	{
		return fail(component::service, error_code::invalid_argument);
	}

	const auto written = transport_.write(output.data(), *size, response_timeout_ms);
	if (!written)
	{
		initialized_ = false;
	}

	return written;
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::wait_for_response()
{
	const auto started = xTaskGetTickCount();
	const auto budget = pdMS_TO_TICKS(response_timeout_ms);
	while (!answered_)
	{
		const auto elapsed = xTaskGetTickCount() - started;
		if (elapsed >= budget)
		{
			initialized_ = false;
			return fail(component::service, error_code::timeout_error);
		}

		const auto received = read_frames((budget - elapsed) * portTICK_PERIOD_MS);
		if (!received)
		{
			initialized_ = false;
			return received;
		}
	}

	return ack_to_status(response_);
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::enqueue(const bluetooth_event &event)
{
	if (event_count_ == events_.size())
	{
		initialized_ = false;
		return fail(component::service, error_code::queue_full);
	}

	events_[(event_head_ + event_count_) % events_.size()] = event;
	++event_count_;
	return {};
}

//////////////////////////////////////////////////////////////////////////

bool xiao_ble_bridge::should_handle_frame(const ble_bridge::frame &frame) const
{
	if (waiting_ && expected_type_ == ble_bridge::frame_type::ready)
	{
		return frame.type == ble_bridge::frame_type::ready && frame.sequence == pending_sequence_ &&
			frame.session == pending_session_;
	}

	return frame.type == ble_bridge::frame_type::hello ||
		(frame.type != ble_bridge::frame_type::ready && frame.session == remote_session_);
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::handle_frame(const ble_bridge::frame &frame)
{
	if (!should_handle_frame(frame))
	{
		return {};
	}

	switch (frame.type)
	{
		case ble_bridge::frame_type::hello:
			return handle_hello(frame);
		case ble_bridge::frame_type::ack:
			return handle_ack(frame);
		case ble_bridge::frame_type::ready:
			return handle_ready(frame);
		case ble_bridge::frame_type::link:
			return handle_link(frame);
		case ble_bridge::frame_type::received:
			return handle_received(frame);
		default:
			return {};
	}
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::handle_ack(const ble_bridge::frame &frame)
{
	if (waiting_ && expected_type_ == frame.type && frame.sequence == pending_sequence_ &&
		frame.session == pending_session_ && frame.size == 1)
	{
		response_ = static_cast<ble_bridge::ack_code>(frame.payload[0]);
		answered_ = true;
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::handle_hello(const ble_bridge::frame &frame)
{
	if (frame.size == 0 && frame.session == 0 && frame.sequence == 0)
	{
		stop();
		return fail(component::service, error_code::not_ready);
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::handle_ready(const ble_bridge::frame &frame)
{
	const auto handled = handle_link(frame);
	if (handled)
	{
		response_ = ble_bridge::ack_code::ok;
		answered_ = true;
	}

	return handled;
}

//////////////////////////////////////////////////////////////////////////

result<bluetooth_link> xiao_ble_bridge::decode_link(const ble_bridge::frame &frame) const
{
	const auto state = ble_bridge::decode_link(frame);
	if (!state)
	{
		return fail(component::service, error_code::device_error);
	}

	bluetooth_link link{
		.connected = state->connected != 0,
		.subscribed = state->subscribed != 0,
		.max_payload = state->max_payload,
		.session = frame.session
	};
	if (link.max_payload > ble_bridge::payload_capacity || (link.connected && link.session == 0) ||
		(link.subscribed && !link.connected))
	{
		return fail(component::service, error_code::device_error);
	}

	return link;
}

//////////////////////////////////////////////////////////////////////////

void xiao_ble_bridge::update_link(bluetooth_link next)
{
	link_ = next;
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::handle_link(const ble_bridge::frame &frame)
{
	const auto next = decode_link(frame);
	if (!next)
	{
		return tl::unexpected(next.error());
	}

	update_link(*next);
	return enqueue(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = link_
	});
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::handle_received(const ble_bridge::frame &frame)
{
	if (!link_.connected || frame.session != remote_session_)
	{
		return {};
	}

	return enqueue(bluetooth_event{
		.type = bluetooth_event::kind::received,
		.link = link_,
		.payload = frame.payload,
		.size = frame.size
	});
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::check_transport_overflow()
{
	if (transport_.dropped_count() != dropped_)
	{
		parser_.reset();
		initialized_ = false;
		return fail(component::uart, error_code::uart_error);
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::consume_bytes(std::span<const std::uint8_t> bytes)
{
	for (const auto byte : bytes)
	{
		const auto event = parser_.feed(byte);
		if (event.type == ble_bridge::stream_event_type::frame)
		{
			if (const auto handled = handle_frame(event.value); !handled)
			{
				initialized_ = false;
				return handled;
			}
		}
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status xiao_ble_bridge::read_frames(std::uint32_t timeout_ms)
{
	if (const auto checked = check_transport_overflow(); !checked)
	{
		return checked;
	}

	std::array<std::uint8_t, ble_bridge::wire_frame_capacity> input{};
	const auto size = transport_.read(input.data(), input.size(), timeout_ms);
	if (!size)
	{
		if (size.error().code == error_code::timeout_error)
		{
			return {};
		}

		initialized_ = false;
		return tl::unexpected(size.error());
	}

	return check_transport_overflow()
				.and_then([&]{ return consume_bytes(std::span{input}.first(*size)); });
}

//////////////////////////////////////////////////////////////////////////

result<bluetooth_event> xiao_ble_bridge::poll(std::uint32_t timeout_ms)
{
	if (!initialized_)
	{
		return fail(component::service, error_code::not_initialized);
	}
	if (event_count_ == 0)
	{
		if (const auto received = read_frames(timeout_ms); !received)
		{
			return tl::unexpected(received.error());
		}
	}
	if (event_count_ == 0)
	{
		return bluetooth_event{};
	}

	const auto event = events_[event_head_];
	event_head_ = (event_head_ + 1) % events_.size();
	--event_count_;
	return event;
}
