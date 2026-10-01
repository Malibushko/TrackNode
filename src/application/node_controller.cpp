#include "application/node_controller.h"
#include "common/common.h"
#ifndef NO_STORAGE
#include "application/log_record.h"
#endif

#include "infrastructure/indicator_service.h"
#include "infrastructure/gnss_service.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/radio_service.h"
#include "infrastructure/sensor_service.h"
#include "infrastructure/board_state_service.h"
#ifndef NO_STORAGE
#include "infrastructure/storage_service.h"
#endif
#include "infrastructure/supervisor.h"

#include <charconv>

node_controller::node_controller(sensor_service &sensor,
#ifndef NO_STORAGE
                                 storage_service &storage,
#endif
                                 indicator_service &indicator, logger_service &logger,
                                 gnss_service &gnss, radio_service &radio,
                                 board_state_service &board_state, supervisor &monitor)
    : sensor_(sensor),
#ifndef NO_STORAGE
      storage_(storage),
#endif
      indicator_(indicator),
      logger_(logger), gnss_(gnss), radio_(radio), board_state_(board_state), monitor_(monitor)
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

#ifdef TRACK_NODE_TICKLESS_IDLE
	set_power_mode(power_mode::low);
#endif

	for (;;)
	{
		if (auto sampled = board_state_.sample_if_due(common::ticks_to_ms(common::get_tick())); !sampled)
		{
			(void)logger_.write("Board voltage sample failed: %u\r\n",
			                    static_cast<unsigned>(sampled.error().code));
		}
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

		std::optional<gnss_sample> gnss;
		if (const auto latest = gnss_.read())
		{
			gnss = gnss_sample{
			    .sequence = latest->sequence,
			    .received_at = latest->received_at,
			    .position = latest->position_valid
			                    ? std::optional<geo_position>{{latest->latitude, latest->longitude}}
			                    : std::nullopt,
			    .speed_mps = latest->speed_valid
			                     ? std::optional<float>{latest->speed_mps}
			                     : std::nullopt,
			    .utc_time = latest->time_valid
			                    ? std::optional<std::time_t>{latest->utc_time}
			                    : std::nullopt,
			};
		}

		const TickType_t recorded_at = xTaskGetTickCount();
		const auto recorded_at_ms = static_cast<std::uint32_t>(pdTICKS_TO_MS(recorded_at));
		if (power_mode_ == power_mode::high ||
		    radio_publish_.should_publish(last_sample_->temperature_c,
		                                  last_sample_->pressure_pa, recorded_at_ms))
		{
			if (radio_.submit(*last_sample_, gnss, recorded_at))
			{
				radio_publish_.on_published(last_sample_->temperature_c,
				                            last_sample_->pressure_pa, recorded_at_ms);
			}
		}
#ifndef NO_STORAGE
		if (auto record = format_sample_csv(*last_sample_, gnss, recorded_at))
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
#else
		monitor_.set_health(service_id::sensor, service_health::ready);
#endif
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

		case command_type::read_gnss:
			handle_read_gnss();
			break;

		case command_type::read_log:
			handle_read_log();
			break;

		case command_type::set_sample_period:
			handle_set_sample_period(value.sample_period_ms);
			break;

		case command_type::reset_storage_prepare:
#ifdef NO_STORAGE
			(void)logger_.write("ERR STORAGE_DISABLED\r\n");
#else
			(void)logger_.write("WARNING ALL SD DATA WILL BE ERASED; SEND RESET STORAGE CONFIRM\r\n");
#endif
			break;

		case command_type::reset_storage_confirm:
			handle_reset_storage();
			break;

		case command_type::read_voltage:
			handle_read_voltage();
			break;

		case command_type::set_power_mode:
			handle_set_power_mode(value.power_mode_);
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

void node_controller::set_power_mode(power_mode mode)
{
	power_mode_ = mode;
	gnss_.set_power_mode(mode);
	radio_.set_power_mode(mode);
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

void node_controller::handle_read_gnss()
{
	gnss_.request_lookup();
	const auto latest = gnss_.read();
	if (!latest)
	{
		(void)logger_.write("GNSS snapshot unavailable\r\n");
		return;
	}

	char sequence[21]{};
	const auto converted = std::to_chars(sequence, sequence + sizeof(sequence) - 1,
	                                     latest->sequence);
	if (converted.ec != std::errc{})
	{
		(void)logger_.write("ERR GNSS_FORMAT\r\n");
		return;
	}
	*converted.ptr = '\0';

	const auto age_ms = pdTICKS_TO_MS(xTaskGetTickCount() - latest->received_at);
	const bool fresh = age_ms <= gnss_freshness_limit_ms;
	const bool position_valid = fresh && latest->position_valid;
	const bool speed_valid = fresh && latest->speed_valid;
	const bool time_valid = fresh && latest->time_valid;
	(void)logger_.write("GNSS seq=%s age=%lu ms pos=%u speed=%u time_field=%u\r\n",
	                    sequence, static_cast<unsigned long>(age_ms),
	                    static_cast<unsigned>(position_valid),
	                    static_cast<unsigned>(speed_valid),
	                    static_cast<unsigned>(time_valid));

	if (position_valid)
	{
		(void)logger_.write("GNSS lat=%.6f lon=%.6f\r\n", latest->latitude, latest->longitude);
	}
	if (speed_valid)
	{
		(void)logger_.write("GNSS speed=%.2f m/s\r\n", static_cast<double>(latest->speed_mps));
	}
	if (time_valid)
	{
		char utc_time[21]{};
		const auto time_converted = std::to_chars(utc_time, utc_time + sizeof(utc_time) - 1,
		                                        static_cast<std::int64_t>(latest->utc_time));
		if (time_converted.ec == std::errc{})
		{
			*time_converted.ptr = '\0';
			(void)logger_.write("GNSS utc_unix_s=%s\r\n", utc_time);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_read_log()
{
#ifdef NO_STORAGE
	(void)logger_.write("ERR STORAGE_DISABLED\r\n");
#else
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
#endif
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
#ifdef NO_STORAGE
	(void)logger_.write("ERR STORAGE_DISABLED\r\n");
#else
	if (auto queued = storage_.request_format(); !queued)
	{
		(void)logger_.write("ERR STORAGE_FORMAT_BUSY\r\n");
		return;
	}
	(void)logger_.write("OK STORAGE_FORMAT_QUEUED\r\n");
#endif
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_read_voltage()
{
	const auto latest = board_state_.read();
	if (!latest)
	{
		(void)logger_.write("ERR VOLTAGE_UNAVAILABLE\r\n");
		return;
	}
	const auto age_ms = common::ticks_to_ms(common::get_tick()) - latest->sampled_at_ms;
	(void)logger_.write("supply_mv=%u low_voltage=%u vrefint_raw=%u age=%lu ms\r\n",
	                    static_cast<unsigned>(latest->supply_mv),
	                    static_cast<unsigned>(latest->low_voltage),
	                    static_cast<unsigned>(latest->vrefint_raw),
	                    static_cast<unsigned long>(age_ms));
}

//////////////////////////////////////////////////////////////////////////

void node_controller::handle_set_power_mode(power_mode power_mode)
{
	set_power_mode(power_mode);
	(void)logger_.write("power mode changed to: %s\r\n", power_mode == power_mode::high ? "high" : "low");
}

//////////////////////////////////////////////////////////////////////////
