#include "infrastructure/bridge_service.h"

#include "drivers/bluetooth_backend.h"
#include "drivers/uart.h"
#include "drivers/protocols/bridge_codec.h"

namespace ble
{
bridge_service::bridge_service(bridge_config &config) : config_(config), parser_(config.codec)
{

}


status bridge_service::start()
{
	if (initialized_)
	{
		return fail(component::service, error_code::already_initialized);
	}

	const auto status = config_.transport.init()
				.and_then([&]{ return config_.bluetooth.init(); });

	if (status)
	{
		parser_.reset();
		initialized_ = true;
	}
	else
	{
		stop();
	}

	return status;
}

void bridge_service::stop()
{
	initialized_ = false;
	parser_.reset();

	config_.transport.stop();
	config_.bluetooth.stop();
}

status bridge_service::poll()
{
	if (!initialized_)
	{
		return fail(component::service, error_code::not_initialized);
	}

	const auto received = config_.transport.read(mutable_data_view{
		buffer_.data(),
		buffer_.size()
	}, config_.poll_transport_timeout_ms);

	if (received)
	{
		for (std::size_t index = 0; index < *received; ++index)
		{
			auto event = parser_.feed(buffer_[index]);
			if (event.type == ble_bridge::stream_event_type::frame)
			{
				if (const auto handled = handle_frame(event.value); !handled)
				{
					return handled;
				}
			}
		}
	}
	else if (received.error().code != error_code::timeout)
	{
		parser_.reset();
		return tl::unexpected(received.error());
	}

	if (const auto event = config_.bluetooth.poll(config_.poll_bluetooth_timeout_ms); !event)
	{
		return tl::unexpected(event.error());
	}

	return {};
}

status bridge_service::handle_frame(ble_bridge::frame &frame)
{
	switch (frame.type)
	{
		case ble_bridge::frame_type::advertise:
			return config_.bluetooth.start_advertise();
		case ble_bridge::frame_type::stop_advertise:
			return config_.bluetooth.stop_advertise();

		default:
			return fail(component::service, error_code::not_implemented);
	}
}
}
