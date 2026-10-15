#include "infrastructure/gnss_service.h"

#include "common/common.h"

#include <algorithm>

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

void gnss_service::set_power_mode(power_mode mode)
{
	taskENTER_CRITICAL();
	power_mode_ = mode;
	taskEXIT_CRITICAL();
}

void gnss_service::request_lookup()
{
	taskENTER_CRITICAL();
	lookup_requested_ = true;
	taskEXIT_CRITICAL();
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

bool gnss_service::is_high_power_active(std::uint32_t now_ms) const
{
	taskENTER_CRITICAL();
	const bool active = !standby_known_ || now_ms - standby_started_ms_ >= standby_duration_ms_;
	taskEXIT_CRITICAL();
	return active;
}

//////////////////////////////////////////////////////////////////////////

void gnss_service::task_entry(void *argument)
{
	static_cast<gnss_service*>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

void gnss_service::run()
{
	const auto now_ms = [] { return common::ticks_to_ms(common::get_tick()); };
	lookup_.start(now_ms());

	while (true)
	{
		taskENTER_CRITICAL();
		const bool is_low_power = power_mode_ == power_mode::low;
		const bool lookup_requested = lookup_requested_;
		lookup_requested_ = false;
		taskEXIT_CRITICAL();

		if (lookup_.sleeping())
		{
			if (!is_low_power || lookup_requested)
			{
				taskENTER_CRITICAL();
				standby_known_ = false;
				taskEXIT_CRITICAL();
				if (auto result = device_.wake(); result)
				{
					lookup_.on_wake(now_ms());
					common::delay(500);
				}
				else
				{
					(void)logger_service_.write("Failed to wake GNSS: %u\r\n", result.error().code);
					if (lookup_requested)
					{
						taskENTER_CRITICAL();
						lookup_requested_ = true;
						taskEXIT_CRITICAL();
					}
					common::delay(config_.sleep_check_ms);
				}
			}
			else if (lookup_.sleep_remaining_ms(now_ms()) == 0)
			{
				lookup_.on_wake(now_ms());
			}
			else
			{
				const auto wait_ms = std::min(config_.sleep_check_ms, lookup_.sleep_remaining_ms(now_ms()));
				common::delay(wait_ms);
			}
			monitor_.progress(service_id::gnss, common::get_tick());
			continue;
		}

		const auto current_ms = now_ms();
		if (is_low_power && lookup_.should_sleep(current_ms))
		{
			const auto duration_ms = lookup_.sleep_duration_ms();
			if (auto result = device_.standby_for(duration_ms); result)
			{
				const auto started_ms = now_ms();
				lookup_.on_sleep_started(started_ms);
				taskENTER_CRITICAL();
				standby_started_ms_ = started_ms;
				standby_duration_ms_ = duration_ms;
				standby_known_ = true;
				taskEXIT_CRITICAL();
			}
			else
			{
				(void)logger_service_.write("Failed to put GNSS to standby mode: %u\r\n", result.error().code);
				common::delay(config_.sleep_check_ms);
			}
			monitor_.progress(service_id::gnss, common::get_tick());
			continue;
		}

		const auto remaining_ms = is_low_power ? lookup_.lookup_remaining_ms(current_ms) : config_.poll_timeout_ms;
		const auto timeout_ms = std::min(config_.poll_timeout_ms, remaining_ms);
		if (timeout_ms > 0)
		{
			if (auto snapshot = device_.read_snapshot(timeout_ms); snapshot.has_value())
			{
				bool is_new = false;
				taskENTER_CRITICAL();
				if (!last_snapshot_.has_value() || snapshot->sequence > last_snapshot_->sequence)
				{
					last_snapshot_ = snapshot.value();
					has_updates_ = true;
					is_new = true;
				}
				taskEXIT_CRITICAL();
				if (is_new && snapshot->position_valid)
				{
					lookup_.on_fix();
				}
			}
		}
		monitor_.progress(service_id::gnss, common::get_tick());
	}
}
