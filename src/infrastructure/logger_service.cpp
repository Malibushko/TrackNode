#include "infrastructure/logger_service.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

void logger_service::set_log_sink(firmware_log_sink *sink)
{
	log_sink_.store(sink);
}

//////////////////////////////////////////////////////////////////////////

status logger_service::submit_reply(const command_reply &reply)
{
	if (reply.text.back() != '\0') return fail(component::service, error_code::invalid_argument);
	return write("%s", reply.text.data());
}

//////////////////////////////////////////////////////////////////////////

logger_service::tx_operation logger_service::tx_operation::promise_type::get_return_object()
{
	return tx_operation{ std::coroutine_handle<promise_type>::from_promise(*this) };
}

//////////////////////////////////////////////////////////////////////////

logger_service::tx_operation logger_service::tx_operation::promise_type::get_return_object_on_allocation_failure() noexcept
{
	return {};
}

//////////////////////////////////////////////////////////////////////////

void *logger_service::tx_operation::promise_type::operator new(
	std::size_t size, logger_service *owner, const std::uint8_t *, std::size_t) noexcept
{
	if (owner->tx_frame_busy_ || size > owner->tx_frame_.size())
	{
		return nullptr;
	}
	owner->tx_frame_busy_ = true;
	return owner->tx_frame_.data();
}

//////////////////////////////////////////////////////////////////////////

logger_service::tx_operation logger_service::send_async(logger_service *owner,
	const std::uint8_t *data, std::size_t size)
{
	const auto sent = co_await owner->output_->write_async(data, size);
	if (!sent)
	{
		co_return tl::make_unexpected(sent.error());
	}
	if (*sent != size)
	{
		co_return fail(component::service, error_code::device_error);
	}
	co_return status{};
}

//////////////////////////////////////////////////////////////////////////

status logger_service::start()
{
	if (started_)
	{
		return fail(component::rtos, error_code::already_initialized);
	}
	if (!output_)
	{
		started_ = true;
		monitor_.set_health(service_id::logger, service_health::ready);
		return {};
	}
	inbox_ = xQueueCreate(8, sizeof(message));
	if (!inbox_)
	{
		return fail(component::rtos, error_code::out_of_memory);
	}

	auto created = task_create(task_entry, "logger", configMINIMAL_STACK_SIZE * 4,
	                           this, tskIDLE_PRIORITY + 1);
	if (!created)
	{
		vQueueDelete(inbox_);
		inbox_ = nullptr;
		return tl::make_unexpected(created.error());
	}
	task_ = *created;
	started_ = true;
	monitor_.set_health(service_id::logger, service_health::ready);
	return {};
}

//////////////////////////////////////////////////////////////////////////

void logger_service::stop()
{
	started_ = false;
	if (task_)
	{
		vTaskSuspend(task_);
		if (active_tx_)
		{
			active_tx_.destroy();
			active_tx_ = {};
		}
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

status logger_service::write(const char *format, ...)
{
	if (!started_)
	{
		dropped_.fetch_add(1);
		return fail(component::rtos, error_code::not_initialized);
	}

	message entry{};
	va_list args;
	va_start(args, format);
	const int length = vsnprintf(entry.text, sizeof(entry.text), format, args);
	va_end(args);
	if (length < 0 || static_cast<std::size_t>(length) >= sizeof(entry.text))
	{
		dropped_.fetch_add(1);
		return fail(component::service, error_code::invalid_argument);
	}
	entry.text[sizeof(entry.text) - 1] = '\0';
	if (auto *sink = log_sink_.load())
	{
		sink->submit_log({entry.text, static_cast<std::size_t>(length)});
	}
	if (!output_)
	{
		return {};
	}
	if (xQueueSend(inbox_, &entry, 0) != pdPASS)
	{
		dropped_.fetch_add(1);
		return fail(component::rtos, error_code::queue_full);
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

void logger_service::task_entry(void *argument)
{
	static_cast<logger_service *>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

void logger_service::run()
{
	message entry{};
	while (true)
	{
		if (xQueueReceive(inbox_, &entry, portMAX_DELAY) != pdPASS)
		{
			continue;
		}

		const auto length = strnlen(entry.text, sizeof(entry.text));
		taskENTER_CRITICAL();
		auto operation = send_async(this, reinterpret_cast<const std::uint8_t *>(entry.text), length);
		active_tx_ = operation.handle;
		taskEXIT_CRITICAL();
		if (!operation.done())
		{
			(void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
			operation.resume();
		}
		if (auto sent = operation.result(); !sent)
		{
			failed_writes_.fetch_add(1);
			if (sent.error().code != error_code::not_ready)
			{
				monitor_.report(service_id::logger, sent.error(), service_health::degraded);
			}
		}
		else if (monitor_.health(service_id::logger) == service_health::degraded)
		{
			monitor_.set_health(service_id::logger, service_health::ready);
		}
		taskENTER_CRITICAL();
		if (operation.handle)
		{
			operation.handle.destroy();
			operation.handle = {};
		}
		active_tx_ = {};
		taskEXIT_CRITICAL();
	}
}

//////////////////////////////////////////////////////////////////////////
