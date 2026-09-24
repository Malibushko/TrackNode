#include "application/node_controller.h"
#include "application/log_record.h"

#include "infrastructure/indicator_service.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/sensor_service.h"
#include "infrastructure/storage_service.h"
#include "infrastructure/supervisor.h"

node_controller::node_controller(sensor_service &sensor, storage_service &storage,
                                 indicator_service &indicator, logger_service &logger,
                                 supervisor &monitor)
    : sensor_(sensor), storage_(storage), indicator_(indicator),
      logger_(logger), monitor_(monitor)
{
}

//////////////////////////////////////////////////////////////////////////

status node_controller::start()
{
	if (inbox_)
	{
		return fail(component::rtos, error_code::already_initialized);
	}
	inbox_ = xQueueCreate(8, sizeof(command));
	if (!inbox_)
	{
		return fail(component::rtos, error_code::out_of_memory);
	}
	auto created = task_create(task_entry, "node", configMINIMAL_STACK_SIZE * 4,
	                           this, tskIDLE_PRIORITY + 1);
	if (!created)
	{
		vQueueDelete(inbox_);
		inbox_ = nullptr;
		return tl::make_unexpected(created.error());
	}
	task_ = *created;
	return {};
}

//////////////////////////////////////////////////////////////////////////

void node_controller::stop()
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

status node_controller::submit(command value)
{
	if (!inbox_)
	{
		return fail(component::rtos, error_code::not_initialized);
	}
	if (xQueueSend(inbox_, &value, 0) != pdPASS)
	{
		return fail(component::rtos, error_code::queue_full);
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

void node_controller::task_entry(void *argument)
{
	static_cast<node_controller *>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

void node_controller::run()
{
	constexpr TickType_t period = pdMS_TO_TICKS(3000);
	take_sample();
	TickType_t last_sample_at = xTaskGetTickCount();

	for (;;)
	{
		const TickType_t elapsed = xTaskGetTickCount() - last_sample_at;
		if (elapsed >= period)
		{
			take_sample();
			last_sample_at = xTaskGetTickCount();
			continue;
		}
		command received{};
		if (xQueueReceive(inbox_, &received, period - elapsed) == pdPASS)
		{
			handle_command(received);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void node_controller::take_sample()
{
	if (!sensor_initialized_)
	{
		auto started = sensor_.init();
		if (!started)
		{
			last_sample_.valid = false;
			monitor_.report(service_id::sensor, started.error(), service_health::degraded);
			monitor_.progress(service_id::sensor, xTaskGetTickCount());
			(void)logger_.write("Sensor init failed: %u\r\n",
			                    static_cast<unsigned>(started.error().code));
			return;
		}
		sensor_initialized_ = true;
	}

	const auto measurement = sensor_.read();
	const TickType_t now = xTaskGetTickCount();
	if (measurement)
	{
		last_sample_ = sensor_sample{
		    .timestamp = now,
		    .sequence = next_sequence_++,
		    .temperature_c = measurement->temperature_c,
		    .pressure_pa = measurement->pressure_pa,
		    .valid = true,
		};
		if (auto record = format_sample_csv(last_sample_))
		{
			monitor_.set_health(service_id::sensor, service_health::ready);
			(void)storage_.submit_record(*record);
		}
		else
		{
			last_sample_.valid = false;
			monitor_.report(service_id::sensor, record.error(), service_health::degraded);
			(void)logger_.write("Sensor sample cannot be recorded\r\n");
		}
	}
	else
	{
		last_sample_.valid = false;
		monitor_.report(service_id::sensor, measurement.error(), service_health::degraded);
		(void)logger_.write("Sensor read failed: %u\r\n",
		                    static_cast<unsigned>(measurement.error().code));
	}
	monitor_.progress(service_id::sensor, now);
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_command(command value)
{
	switch (value)
	{
	case command::ping:
		if (auto accepted = indicator_.ping(); !accepted)
		{
			(void)logger_.write("Indicator queue full\r\n");
		}
		break;

	case command::read_temperature:
	case command::read_pressure:
	{
		if (!last_sample_.valid)
		{
			if (last_sample_.sequence == 0)
			{
				(void)logger_.write("Sensor sample unavailable (no measurement)\r\n");
			}
			else
			{
				const auto age = pdTICKS_TO_MS(xTaskGetTickCount() - last_sample_.timestamp);
				(void)logger_.write("Sensor sample invalid (last age=%lu ms, seq=%lu)\r\n",
				                    static_cast<unsigned long>(age),
				                    static_cast<unsigned long>(last_sample_.sequence));
			}
			break;
		}
		const auto age = pdTICKS_TO_MS(xTaskGetTickCount() - last_sample_.timestamp);
		if (value == command::read_temperature)
		{
			(void)logger_.write("Temperature = %.2f C (age=%lu ms, seq=%lu)\r\n",
			                    static_cast<double>(last_sample_.temperature_c),
			                    static_cast<unsigned long>(age),
			                    static_cast<unsigned long>(last_sample_.sequence));
		}
		else
		{
			(void)logger_.write("Pressure = %.2f Pa (age=%lu ms, seq=%lu)\r\n",
			                    static_cast<double>(last_sample_.pressure_pa),
			                    static_cast<unsigned long>(age),
			                    static_cast<unsigned long>(last_sample_.sequence));
		}
		break;
	}

	case command::read_log:
	{
		const auto log = storage_.snapshot();
		if (log.health != service_health::ready)
		{
			(void)logger_.write("Storage unavailable (dropped=%lu)\r\n",
			                    static_cast<unsigned long>(log.dropped_samples));
			break;
		}
		const auto age = pdTICKS_TO_MS(xTaskGetTickCount() - log.observed_at);
		(void)logger_.write("Log size: %llu bytes (age=%lu ms, dropped=%lu)\r\n",
		                    static_cast<unsigned long long>(log.log_size),
		                    static_cast<unsigned long>(age),
		                    static_cast<unsigned long>(log.dropped_samples));
		break;
	}
	}
}

//////////////////////////////////////////////////////////////////////////
