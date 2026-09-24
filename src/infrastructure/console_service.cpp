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
		if (protocol_.process_next_byte())
		{
			if (auto accepted = commands_.submit(protocol_.read()); !accepted)
			{
				rejected_.fetch_add(1);
				(void)logger_.write("Command queue full\r\n");
			}
		}
		if (const auto failure = input_.last_error())
		{
			monitor_.report(service_id::console, *failure, service_health::degraded);
			vTaskDelay(pdMS_TO_TICKS(10));
		}
		else if (monitor_.health(service_id::console) == service_health::degraded)
		{
			monitor_.set_health(service_id::console, service_health::ready);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
