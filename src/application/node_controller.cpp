#include "application/node_controller.h"
#include "application/log_record.h"

#include "infrastructure/indicator_service.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/sensor_service.h"
#include "infrastructure/storage_service.h"
#include "infrastructure/supervisor.h"

#include <charconv>

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
	constexpr TickType_t heartbeat_period = pdMS_TO_TICKS(1000);
	take_sample();
	TickType_t last_sample_at = xTaskGetTickCount();

	for (;;)
	{
		const TickType_t elapsed = xTaskGetTickCount() - last_sample_at;
		if (elapsed >= sample_period_)
		{
			take_sample();
			last_sample_at = xTaskGetTickCount();
			continue;
		}
		command received{};
		const TickType_t until_sample = sample_period_ - elapsed;
		const TickType_t wait = until_sample < heartbeat_period ? until_sample : heartbeat_period;
		if (xQueueReceive(inbox_, &received, wait) == pdPASS)
		{
			handle_command(received);
		}
		if (sensor_initialized_ && !sensor_faulted_)
		{
			monitor_.progress(service_id::sensor, xTaskGetTickCount());
		}
	}
}

//////////////////////////////////////////////////////////////////////////

status node_controller::init_sensor()
{
	auto started = sensor_.init();
	if (!started)
	{
		sensor_faulted_ = true;
		if (last_sample_)
		{
			last_sample_->valid = false;
		}
		monitor_.report(service_id::sensor, started.error(), service_health::degraded);
		(void)logger_.write("Sensor init failed: %u (HAL=%u, I2C=0x%lx)\r\n",
		                    static_cast<unsigned>(started.error().code),
		                    static_cast<unsigned>(started.error().hal_status),
		                    static_cast<unsigned long>(started.error().hal_detail));
		return started;
	}
	sensor_initialized_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////
void node_controller::take_sample()
{
	if (sensor_faulted_)
	{
		return;
	}
	if (!sensor_initialized_ && !init_sensor())
	{
		return;
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
		monitor_.progress(service_id::sensor, now);

		if (auto record = format_sample_csv(*last_sample_))
		{
			monitor_.set_health(service_id::sensor, service_health::ready);
#ifdef FAULT_INJECTION_ENABLE
		for (int i = 0; i < 3; i++)
#endif
			(void)storage_.submit_record(*record);
		}
		else
		{
			last_sample_->valid = false;
			monitor_.report(service_id::sensor, record.error(), service_health::degraded);
			(void)logger_.write("Sensor sample cannot be recorded\r\n");
		}
	}
	else
	{
		sensor_faulted_ = true;
		if (last_sample_)
		{
			last_sample_->valid = false;
		}
		monitor_.report(service_id::sensor, measurement.error(), service_health::degraded);
		(void)logger_.write("Sensor read failed: %u (HAL=%u, I2C=0x%lx)\r\n",
		                    static_cast<unsigned>(measurement.error().code),
		                    static_cast<unsigned>(measurement.error().hal_status),
		                    static_cast<unsigned long>(measurement.error().hal_detail));
	}
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_command(command value)
{
	switch (value.type)
	{
		case command_type::ping:
			handle_ping();
			break;

		case command_type::read_temperature:
			handle_read_temperature();
			break;

		case command_type::read_pressure:
			handle_read_pressure();
			break;

		case command_type::read_log:
			handle_read_log();
			break;

		case command_type::set_sample_period:
			handle_set_sample_period(value.sample_period_ms);
			break;

		case command_type::reset_storage_prepare:
			(void)logger_.write("WARNING ALL SD DATA WILL BE ERASED; SEND RESET STORAGE CONFIRM\r\n");
			break;

		case command_type::reset_storage_confirm:
			handle_reset_storage();
			break;
	}
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_ping()
{
	if (auto accepted = indicator_.ping(); !accepted)
	{
		(void)logger_.write("Indicator queue full\r\n");
	}
}

//////////////////////////////////////////////////////////////////////////

bool node_controller::ensure_sample_available()
{
	if (!last_sample_)
	{
		(void)logger_.write("Sensor sample unavailable (no measurement)\r\n");
		return false;
	}

	if (!last_sample_->valid)
	{
		const auto age = pdTICKS_TO_MS(xTaskGetTickCount() - last_sample_->timestamp);
		(void)logger_.write("Sensor sample invalid (last age=%lu ms, seq=%lu)\r\n",
		                    static_cast<unsigned long>(age),
		                    static_cast<unsigned long>(last_sample_->sequence));
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_read_temperature()
{
	if (!ensure_sample_available())
	{
		return;
	}

	const auto age = pdTICKS_TO_MS(xTaskGetTickCount() - last_sample_->timestamp);
	(void)logger_.write("Temperature = %.2f C (age=%lu ms, seq=%lu)\r\n",
	                    static_cast<double>(last_sample_->temperature_c),
	                    static_cast<unsigned long>(age),
	                    static_cast<unsigned long>(last_sample_->sequence));
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_read_pressure()
{
	if (!ensure_sample_available())
	{
		return;
	}

	const auto age = pdTICKS_TO_MS(xTaskGetTickCount() - last_sample_->timestamp);
	(void)logger_.write("Pressure = %.2f Pa (age=%lu ms, seq=%lu)\r\n",
	                    static_cast<double>(last_sample_->pressure_pa),
	                    static_cast<unsigned long>(age),
	                    static_cast<unsigned long>(last_sample_->sequence));
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_read_log()
{
	const auto log = storage_.snapshot();
	if (log.health != service_health::ready)
	{
		(void)logger_.write("Storage unavailable (dropped=%lu)\r\n",
		                    static_cast<unsigned long>(log.dropped_samples));
		return;
	}

	const auto age = pdTICKS_TO_MS(xTaskGetTickCount() - log.observed_at);
	char size_text[21]{};
	const auto converted = std::to_chars(size_text, size_text + sizeof(size_text) - 1,
	                                     log.log_size);
	if (converted.ec != std::errc{})
	{
		(void)logger_.write("Log size unavailable\r\n");
		return;
	}
	*converted.ptr = '\0';
	(void)logger_.write("Log %s: %s bytes (age=%lu ms, dropped=%lu)\r\n",
	                    log.file_name.data(), size_text,
	                    static_cast<unsigned long>(age),
	                    static_cast<unsigned long>(log.dropped_samples));
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_set_sample_period(std::uint32_t period_ms)
{
	if (period_ms < 100 || period_ms > 60000)
	{
		(void)logger_.write("ERR SAMPLE_PERIOD_RANGE 100..60000\r\n");
		return;
	}

	sample_period_ = pdMS_TO_TICKS(period_ms);
	(void)logger_.write("OK SAMPLE_PERIOD %lu\r\n", static_cast<unsigned long>(period_ms));
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_reset_storage()
{
	if (auto queued = storage_.request_format(); !queued)
	{
		(void)logger_.write("ERR STORAGE_FORMAT_BUSY\r\n");
		return;
	}
	(void)logger_.write("OK STORAGE_FORMAT_QUEUED\r\n");
}

//////////////////////////////////////////////////////////////////////////
