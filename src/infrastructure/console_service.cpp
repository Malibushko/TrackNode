#include "infrastructure/console_service.h"

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
	while (true)
	{
		const auto received = input_.read();
		if (!received)
		{
			protocol_.reset();
			if (received.error().code != error_code::not_ready)
			{
				monitor_.report(service_id::console, received.error(), service_health::degraded);
			}
			vTaskDelay(pdMS_TO_TICKS(10));
			continue;
		}
		if (*received)
		{
			handle_event(protocol_.feed(**received));
		}
		else
		{
			vTaskDelay(pdMS_TO_TICKS(5));
		}
		if (monitor_.health(service_id::console) == service_health::degraded)
		{
			monitor_.set_health(service_id::console, service_health::ready);
		}
	}
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
