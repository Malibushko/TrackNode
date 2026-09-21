#include "app/track_node.h"

#include "drivers/filesystem.h"
#include "libs/fatfs/sd_diskio.h"
#include "libs/freertos/freertos_helpers.h"

#include <numeric>
#include <ranges>

extern "C"
{
#include "FreeRTOS.h"
#include "task.h"
#include "portable.h"
}

#include "board/clock.h"
#include "common/common.h"
#include "drivers/devices/bmp280.h"
#include "platform/stdio.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <printf.h>

track_node::track_node(track_node_board &hardware)
    : hardware_(hardware),
      diagnostic_led_(gpio_config{
          .pin = { GPIOC, GPIO_PIN_13 }, .mode = GPIO_MODE_OUTPUT_PP, .initial_high = true, .active_low = true }),
      indicator_(diagnostic_led_), console_input_(hardware.console()), protocol_(console_input_)
{
}

status track_node::init()
{
	auto init_task = [](void *arg)
	{
		auto *application = static_cast<track_node *>(arg);
		if (auto result = application->startup(); !result)
		{
			for (auto &task : application->worker_tasks_)
			{
				if (task)
				{
					vTaskDelete(task);
					task = nullptr;
				}
			}
			if (!application->can_signal_error())
			{
				panic();
			}
			for (;;)
			{
				application->update_indicators(common::get_tick());
				if (!application->can_signal_error())
				{
					panic();
				}
				vTaskDelay(pdMS_TO_TICKS(50));
			}
		}

		vTaskDelete(nullptr);
	};

	if (auto result = task_create(init_task, "init_task", configMINIMAL_STACK_SIZE * 4, this, configMAX_PRIORITIES - 1);
	    !result)
	{
		return tl::make_unexpected(result.error());
	}

	return {};
}

void track_node::launch()
{
	vTaskStartScheduler();

	while (true)
	{
		// Should not reach here
	}
}

status track_node::init_buffers()
{
	indicator_queue_ = xQueueCreate(4, sizeof(command));
	if (!indicator_queue_)
	{
		return fail(component::board, error_code::out_of_memory);
	}

	sensor_queue_ = xQueueCreate(4, sizeof(command));
	if (!sensor_queue_)
	{
		return fail(component::board, error_code::out_of_memory);
	}

	logger_queue_ = xQueueCreate(8, sizeof(log_message));
	if (!logger_queue_)
	{
		return fail(component::board, error_code::out_of_memory);
	}

	return {};
}

status track_node::startup()
{
	if (attempted_)
	{
		return fail(component::board, error_code::already_initialized);
	}

	attempted_ = true;
	auto init_status = diagnostic_led_.init();
	if (!init_status)
	{
		set_error(init_status.error(), true);
		return init_status;
	}

	diagnostic_ready_ = true;
	init_status = init_buffers().and_then(
	    [&]() -> status
	    {
		    return hardware_.init();
	    });
	if (!init_status)
	{
		set_error(init_status.error(), true);
		return init_status;
	}

	set_stdout_uart(&hardware_.console());
	std::setvbuf(stdout, nullptr, _IONBF, 0);

	init_status = init_logger_tasks()
	                  .and_then([&]() -> status { return init_sensor_tasks(); })
	                  .and_then([&]() -> status { return init_protocol_tasks(); })
	                  .and_then([&]() -> status { return init_indicators_tasks(); })
					  .and_then([&]() -> status { return init_storage_tasks(); });

	if (!init_status)
	{
		set_error(init_status.error(), true);
		return init_status;
	}

	state_ = program_state::ok;
	return {};
}

status track_node::init_indicators_tasks()
{
	auto display_indicator_task = [](void *arg)
	{
		auto *track_node_ptr = static_cast<track_node *>(arg);
		auto queue = track_node_ptr->indicator_queue_;
		auto &indicator = track_node_ptr->indicator_;

		command event;

		while (true)
		{
			BaseType_t event_received = xQueueReceive(queue, &event, pdMS_TO_TICKS(50));

			uint32_t now = xTaskGetTickCount();

			if (event_received == pdPASS)
			{
				switch (event)
				{
				case command::ping:
				{
					indicator.start_blink(1, now);
					break;
				}

				default:
				{
					break;
				}
				}
			}

			track_node_ptr->update_indicators(now);
		}
	};

	if (auto result = task_create(display_indicator_task, "indicators_task", configMINIMAL_STACK_SIZE * 4, this,
	                              tskIDLE_PRIORITY + 2);
	    !result)
	{
		return tl::make_unexpected(result.error());
	}

	else
	{
		worker_tasks_[0] = result.value();
	}

	return {};
}

status track_node::init_protocol_tasks()
{
	auto console_task = [](void *arg)
	{
		auto track_node_ptr = static_cast<track_node *>(arg);
		auto &console = track_node_ptr->console_input_;
		auto &protocol = track_node_ptr->protocol_;

		while (true)
		{
			if (protocol.process_next_byte())
			{
				command command = protocol.read();
				// TODO: create event bus to dispatch events to different queues
				switch (command)
				{
				case command::ping:
				{
					if (xQueueSend(track_node_ptr->indicator_queue_, &command, 0) != pdPASS)
					{
						track_node_ptr->dropped_commands_.fetch_add(1);
						track_node_ptr->log("Command queue full: %u\r\n", static_cast<unsigned>(command));
					}
					break;
				}
				case command::read_pressure:
				case command::read_temperature:
				{
					if (xQueueSend(track_node_ptr->sensor_queue_, &command, 0) != pdPASS)
					{
						track_node_ptr->dropped_commands_.fetch_add(1);
						track_node_ptr->log("Command queue full: %u\r\n", static_cast<unsigned>(command));
					}
					break;
				}
				}
			}

			if (console.last_error())
			{
				track_node_ptr->set_error(*console.last_error(), false);
			}
		}
	};

	if (auto result =
	        task_create(console_task, "console_task", configMINIMAL_STACK_SIZE * 4, this, tskIDLE_PRIORITY + 1);
	    !result)
	{
		return tl::make_unexpected(result.error());
	}

	else
	{
		worker_tasks_[1] = result.value();
	}

	return {};
}

status track_node::init_sensor_tasks()
{
	auto sensor_task = [](void *arg)
	{
		auto *application = static_cast<track_node *>(arg);
		auto &sensor = application->hardware_.environment_sensor();
		auto queue = application->sensor_queue_;

		command event;
		while (true)
		{
			BaseType_t event_received = xQueueReceive(queue, &event, portMAX_DELAY);

			if (event_received == pdPASS)
			{
				switch (event)
				{
				case command::read_temperature:
				{
					if (auto data = sensor.read_measurement())
					{
						application->log("Temperature = %f\r\n", data->temperature_c);
					}
					else
					{
						application->set_error(data.error(), false);
						application->log("Sensor read failed: %u\r\n", static_cast<unsigned>(data.error().code));
					}

					break;
				}
				case command::read_pressure:
				{
					if (auto data = sensor.read_measurement())
					{
						application->log("Pressure = %f\r\n", data->pressure_pa);
					}
					else
					{
						application->set_error(data.error(), false);
						application->log("Sensor read failed: %u\r\n", static_cast<unsigned>(data.error().code));
					}
					break;
				}

				default:
				{
					break;
				}
				}
			}
		}
	};

	if (auto result = task_create(sensor_task, "sensor_task", configMINIMAL_STACK_SIZE * 4, this, tskIDLE_PRIORITY + 1);
	    !result)
	{
		return tl::make_unexpected(result.error());
	}

	else
	{
		worker_tasks_[2] = result.value();
	}

	return {};
}

status track_node::init_logger_tasks()
{
	auto logger_task = [](void *arg)
	{
		auto application = static_cast<track_node *>(arg);
		auto queue = application->logger_queue_;
		auto &console = application->hardware_.console();

		log_message msg{};

		while (true)
		{
			if (xQueueReceive(queue, &msg, portMAX_DELAY) == pdPASS)
			{
				size_t len = strnlen(msg.text, sizeof(msg.text));

				if (auto result = console.write(reinterpret_cast<const uint8_t *>(msg.text), len); !result)
				{
					application->failed_uart_writes_.fetch_add(1);
					application->set_error(result.error(), false);
				}
			}
		}
	};

	if (auto result = task_create(logger_task, "logger_task", configMINIMAL_STACK_SIZE * 4, this, tskIDLE_PRIORITY);
	    !result)
	{
		return tl::make_unexpected(result.error());
	}

	else
	{
		worker_tasks_[3] = result.value();
	}

	return {};
}

status track_node::init_storage_tasks()
{
	auto storage_task = [](void *arg)
	{
		auto application = static_cast<track_node *>(arg);
		auto& sd_card = application->hardware_.storage();

		fatfs::Volume storage(sd_card);
		uint8_t init_attempts = 8;
		
		while (true)
		{
			if (!storage.mounted())
			{
				if (auto result = storage.mount(); result != FR_OK)
				{
					if (result == FR_NO_FILESYSTEM)
					{
						static std::uint8_t work[4096];

						fatfs::MkfsOptions opts;
						opts.format = FM_EXFAT;

						if (storage.mkfs(opts, work) == FR_OK)
							continue;
					}
					else if (result == FR_NOT_READY)
					{
						if (--init_attempts)
						{
							common::delay(1000);
							continue;
						}
					}

					application->set_error(error(component::storage, error_code::not_initialized), false);
					vTaskDelete(nullptr);
					break;
				}
			}

			common::delay(1000);
		}
	};

	if (auto result = task_create(storage_task, "storage_task", configMINIMAL_STACK_SIZE * 8, this, tskIDLE_PRIORITY); !result)
	{
		return tl::make_unexpected(result.error());
	}

	else
	{
		worker_tasks_[4] = result.value();
	}

	return {};
};

void track_node::update_indicators(uint32_t now)
{
	auto indicator_status = indicator_.update(now, state());

	if (!indicator_status)
	{
		set_error(indicator_status.error(), false);
		diagnostic_ready_ = false;
	}
}

void track_node::log(const char *fmt, ...)
{
	if (!logger_queue_)
	{
		dropped_logs_.fetch_add(1);
		return;
	}

	log_message msg{};

	va_list args;
	va_start(args, fmt);
	vsnprintf(msg.text, sizeof(msg.text), fmt, args);
	va_end(args);

	msg.text[sizeof(msg.text) - 1] = '\0';

	if (xQueueSend(logger_queue_, &msg, 0) != pdPASS)
	{
		dropped_logs_.fetch_add(1);
	}
}

void track_node::set_error(error failure, bool during_init)
{
	auto next_state = during_init ? program_state::initialization_error : program_state::runtime_error;
	if (failure.code == error_code::hal_failure)
	{
		next_state = program_state::hal_error;
	}
	const auto mask = __get_PRIMASK();
	__disable_irq();
	last_error_ = failure;
	state_.store(next_state, std::memory_order_relaxed);
	__set_PRIMASK(mask);
}