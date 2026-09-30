#include "infrastructure/supervisor.h"
#include "infrastructure/progress_policy.h"

status supervisor::start()
{
	if (task_)
	{
		return fail(component::rtos, error_code::already_initialized);
	}

	auto created = task_create(task_entry, "supervisor", configMINIMAL_STACK_SIZE * 2, this, configMAX_PRIORITIES - 1);
	if (!created)
	{
		return tl::make_unexpected(created.error());
	}
	task_ = *created;
	return {};
}

//////////////////////////////////////////////////////////////////////////

void supervisor::stop()
{
	if (task_)
	{
		vTaskDelete(task_);
		task_ = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////

void supervisor::set_health(service_id id, service_health health)
{
	health_[index(id)].store(health, std::memory_order_relaxed);
	recompute_state();
}

//////////////////////////////////////////////////////////////////////////

void supervisor::report(service_id id, error failure, service_health health)
{
	taskENTER_CRITICAL();
	last_error_ = failure;
	health_[index(id)].store(health, std::memory_order_relaxed);
	recompute_state();
	taskEXIT_CRITICAL();
}

//////////////////////////////////////////////////////////////////////////

void supervisor::report_startup(error failure)
{
	taskENTER_CRITICAL();
	last_error_ = failure;
	state_.store(program_state::initialization_error, std::memory_order_relaxed);
	taskEXIT_CRITICAL();
}

//////////////////////////////////////////////////////////////////////////

void supervisor::progress(service_id id, TickType_t now)
{
	last_progress_[index(id)].store(now, std::memory_order_relaxed);
}

//////////////////////////////////////////////////////////////////////////

service_health supervisor::health(service_id id) const
{
	return health_[index(id)].load(std::memory_order_relaxed);
}

//////////////////////////////////////////////////////////////////////////

std::optional<error> supervisor::last_error() const
{
	taskENTER_CRITICAL();
	auto value = last_error_;
	taskEXIT_CRITICAL();
	return value;
}

//////////////////////////////////////////////////////////////////////////

void supervisor::recompute_state()
{
	program_state next = program_state::ok;
	for (const auto &entry : health_)
	{
		const auto value = entry.load(std::memory_order_relaxed);
		if (value == service_health::failed)
		{
			next = program_state::runtime_error;
			break;
		}
		if (value == service_health::degraded)
		{
			next = program_state::runtime_error;
		}
		else if (value == service_health::starting && next == program_state::ok)
		{
			next = program_state::initialization_error;
		}
	}
	state_.store(next, std::memory_order_relaxed);
}

//////////////////////////////////////////////////////////////////////////

void supervisor::task_entry(void *argument)
{
	static_cast<supervisor *>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

void supervisor::run()
{
	const TickType_t started = xTaskGetTickCount();
	if (auto initialized = watchdog_.init(); !initialized)
	{
		report(service_id::watchdog, initialized.error(), service_health::failed);
		vTaskDelete(nullptr);
		return;
	}
	set_health(service_id::watchdog, service_health::ready);

	while (true)
	{
		const TickType_t now = xTaskGetTickCount();
		const progress_observation observed{
		    health(service_id::sensor),
#ifndef NO_STORAGE
		    health(service_id::storage),
#endif
		    last_progress_[index(service_id::sensor)].load(std::memory_order_relaxed),
#ifndef NO_STORAGE
		    last_progress_[index(service_id::storage)].load(std::memory_order_relaxed),
#endif
		};

		if (should_refresh_watchdog(now, started, observed))
		{
			if (!watchdog_.refresh())
			{
				panic();
			}
		}

		vTaskDelay(pdMS_TO_TICKS(250));
	}
}

//////////////////////////////////////////////////////////////////////////
