#include "infrastructure/gnss_service.h"

#include "common/common.h"

//////////////////////////////////////////////////////////////////////////

gnss_service::gnss_service(neo6m &device, logger_service &logger, supervisor &monitor) :
	device_(device),
	logger_service_(logger),
	monitor_(monitor),
	has_updates_(false)
{
}

//////////////////////////////////////////////////////////////////////////

status gnss_service::start()
{
	if (auto status = init(); !status)
	{
		return status;
	}

	auto created = task_create(task_entry, "gnss", configMINIMAL_STACK_SIZE * 4, this, tskIDLE_PRIORITY + 1);
	if (!created)
	{
		return tl::make_unexpected(created.error());
	}

	task_ = *created;
	monitor_.set_health(service_id::gnss, service_health::ready);
	return {};
}

//////////////////////////////////////////////////////////////////////////

void gnss_service::stop()
{
	if (task_)
	{
		vTaskDelete(task_);
		task_ = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////

status gnss_service::init()
{
	if (initialized_)
	{
		return {};
	}
	auto result = device_.init();
	initialized_ = result.has_value();

	return result;
}

//////////////////////////////////////////////////////////////////////////

result<gnss_service::snapshot_t> gnss_service::read()
{
	if (!initialized_)
	{
		return fail(component::service, error_code::not_initialized);
	}

	result<snapshot_t> snapshot;

	taskENTER_CRITICAL();

	if (!last_snapshot_.has_value())
	{
		snapshot = fail(component::service, error_code::not_ready);
	} else
	{
		has_updates_ = false;
		snapshot = last_snapshot_.value();
	}

	taskEXIT_CRITICAL();

	return snapshot;
}

//////////////////////////////////////////////////////////////////////////

bool gnss_service::has_updates() const
{
	bool has_updates = false;

	taskENTER_CRITICAL();
	has_updates = has_updates_;
	taskEXIT_CRITICAL();

	return has_updates;
}

//////////////////////////////////////////////////////////////////////////

void gnss_service::task_entry(void *argument)
{
	static_cast<gnss_service*>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

void gnss_service::run()
{
	while (true)
	{
		if (auto snapshot = device_.read_snapshot(config_.poll_frequency_ms); snapshot.has_value())
		{
			taskENTER_CRITICAL();

			if (!last_snapshot_.has_value() || snapshot->sequence > last_snapshot_->sequence)
			{
				last_snapshot_ = snapshot.value();
				has_updates_ = true;
			}

			taskEXIT_CRITICAL();
		}

		monitor_.progress(service_id::gnss, common::get_tick());
	}
}
