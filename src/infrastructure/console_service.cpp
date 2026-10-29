#include "infrastructure/console_service.h"
#include "libs/freertos/freertos_helpers.h"

namespace
{
constexpr TickType_t disconnected_poll_interval = pdMS_TO_TICKS(10);
constexpr TickType_t empty_poll_interval = pdMS_TO_TICKS(5);
constexpr TickType_t failed_read_retry_interval = pdMS_TO_TICKS(1000);
}

status console_service::start()
{
	if (task_)
	{
		return fail(component::rtos, error_code::already_initialized);
	}
	auto created = task_create(task_entry, "console", configMINIMAL_STACK_SIZE * 4,
	                           this, tskIDLE_PRIORITY + 1);
	if (!created)
	{
		return tl::make_unexpected(created.error());
	}
	task_ = *created;
	monitor_.set_health(service_id::console, service_health::ready);
	return {};
}

//////////////////////////////////////////////////////////////////////////

void console_service::stop()
{
	if (task_)
	{
		vTaskDelete(task_);
		task_ = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////

void console_service::task_entry(void *argument)
{
	static_cast<console_service *>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

void console_service::run()
{
	listen_to_wake_sources();
	while (true)
	{
		const auto received = input_.read();
		if (!received)
		{
			protocol_.reset();
			const auto transport_failed = received.error().code != error_code::not_ready;
			if (transport_failed)
			{
				monitor_.report(service_id::console, received.error(), service_health::degraded);
				vTaskDelay(failed_read_retry_interval);
				continue;
			}
			wait_for_input(disconnected_poll_interval);
			continue;
		}
		if (*received)
		{
			handle_event(protocol_.feed(**received));
		}
		else
		{
			wait_for_input(empty_poll_interval);
		}
		if (monitor_.health(service_id::console) == service_health::degraded)
		{
			monitor_.set_health(service_id::console, service_health::ready);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void console_service::listen_to_wake_sources()
{
	const auto self = xTaskGetCurrentTaskHandle();
	for (auto *source : wake_sources_)
	{
		source->set_listener(self);
	}
}

//////////////////////////////////////////////////////////////////////////

void console_service::wait_for_input(TickType_t poll_interval)
{
	if (wake_sources_.empty())
	{
		vTaskDelay(poll_interval);
		return;
	}

	(void)wait_task_event(portMAX_DELAY);
}

//////////////////////////////////////////////////////////////////////////

void console_service::handle_event(protocol_event event)
{
	switch (event.type)
	{
		case protocol_event_type::none:
			break;

		case protocol_event_type::command:
			if (auto accepted = commands_.submit(command_request{event.value, &logger_}); !accepted)
			{
				rejected_.fetch_add(1);
				(void)logger_.write("ERR COMMAND_QUEUE_FULL\r\n");
			}
			break;

		case protocol_event_type::parse_error:
			rejected_.fetch_add(1);
			switch (event.error)
			{
				case command_parse_error::unknown_command:
					(void)logger_.write("ERR UNKNOWN_COMMAND\r\n");
					break;
				case command_parse_error::invalid_syntax:
					(void)logger_.write("ERR INVALID_SYNTAX\r\n");
					break;
				case command_parse_error::invalid_number:
					(void)logger_.write("ERR INVALID_NUMBER\r\n");
					break;
			}
			break;

		case protocol_event_type::line_too_long:
			rejected_.fetch_add(1);
			(void)logger_.write("ERR LINE_TOO_LONG\r\n");
			break;
	}
}

//////////////////////////////////////////////////////////////////////////
