#include "infrastructure/indicator_service.h"

namespace
{
constexpr TickType_t animation_step = pdMS_TO_TICKS(50);
}

indicator_service::indicator_service(logger_service &logger, supervisor &monitor)
    : output_(gpio_config{ .pin = { GPIOC, GPIO_PIN_13 },
                           .mode = GPIO_MODE_OUTPUT_PP,
                           .initial_high = true,
                           .active_low = true }),
      indicator_(output_), logger_(logger), monitor_(monitor)
{
}

//////////////////////////////////////////////////////////////////////////

status indicator_service::init_diagnostic()
{
	auto result = output_.init();
	ready_.store(result.has_value());
	return result;
}

//////////////////////////////////////////////////////////////////////////

status indicator_service::start()
{
	if (!diagnostic_ready())
	{
		return fail(component::gpio, error_code::not_initialized);
	}
	if (inbox_)
	{
		return fail(component::rtos, error_code::already_initialized);
	}
	inbox_ = xQueueCreate(4, sizeof(std::uint8_t));
	if (!inbox_)
	{
		return fail(component::rtos, error_code::out_of_memory);
	}
	auto created = task_create(task_entry, "indicator", configMINIMAL_STACK_SIZE * 4,
	                           this, tskIDLE_PRIORITY + 2);
	if (!created)
	{
		vQueueDelete(inbox_);
		inbox_ = nullptr;
		return tl::make_unexpected(created.error());
	}
	task_ = *created;
	monitor_.set_health(service_id::indicator, service_health::ready);
	return {};
}

//////////////////////////////////////////////////////////////////////////

void indicator_service::stop()
{
	if (task_)
	{
		vTaskDelete(task_);
		task_ = nullptr;
	}
	if (inbox_)
	{
		vQueueDelete(inbox_);
		inbox_ = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////

status indicator_service::ping()
{
	if (!inbox_)
	{
		return fail(component::rtos, error_code::not_initialized);
	}
	const std::uint8_t event = 1;
	if (xQueueSend(inbox_, &event, 0) != pdPASS)
	{
		return fail(component::rtos, error_code::queue_full);
	}
	signal_task_event(task_);
	return {};
}

//////////////////////////////////////////////////////////////////////////

void indicator_service::update_once(TickType_t now)
{
	if (auto result = indicator_.update(now, monitor_.state(), pairing_.load()); !result)
	{
		ready_.store(false);
		monitor_.report(service_id::indicator, result.error(), service_health::failed);
		(void)logger_.write("Diagnostic LED failed\r\n");
	}
}

//////////////////////////////////////////////////////////////////////////

void indicator_service::task_entry(void *argument)
{
	static_cast<indicator_service *>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

void indicator_service::run()
{
	monitor_.set_state_listener(xTaskGetCurrentTaskHandle());
	std::uint8_t event = 0;
	while (true)
	{
		const TickType_t now = xTaskGetTickCount();
		if (xQueueReceive(inbox_, &event, 0) == pdPASS)
		{
			indicator_.start_blink(event, now);
		}
		update_once(now);
		wait_for_next_update();
	}
}

//////////////////////////////////////////////////////////////////////////

void indicator_service::wait_for_next_update()
{
	const auto animation_running = indicator_.animating() || uxQueueMessagesWaiting(inbox_) != 0;
	(void)wait_task_event(animation_running ? animation_step : portMAX_DELAY);
}

//////////////////////////////////////////////////////////////////////////
