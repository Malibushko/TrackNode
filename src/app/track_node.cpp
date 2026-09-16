#include "app/track_node.h"

#include "libs/freertos/freertos_helpers.h"

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

track_node::track_node(track_node_board &hardware)
    : hardware_(hardware),
      diagnostic_led_(gpio_config{
          .pin = { GPIOC, GPIO_PIN_13 }, .mode = GPIO_MODE_OUTPUT_PP, .initial_high = true, .active_low = true }),
      indicator_(diagnostic_led_), console_input_(hardware.console()), protocol_(console_input_)
{
}

status track_node::init()
{
	auto init_task = [](void * arg)
	{
		auto* application = static_cast<track_node *>(arg);
		if (auto result = application->startup(); !result && !application->can_signal_error())
		{
			panic();
		}

		vTaskDelete(nullptr);
	};

	if (auto result = task_create(init_task, "init_task", configMINIMAL_STACK_SIZE * 4, this, configMAX_PRIORITIES - 1); !result)
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
	init_status = init_buffers()
						.and_then([&]() -> status { return hardware_.init(); });
	if (!init_status)
	{
		set_error(init_status.error(), true);
		return init_status;
	}

	set_stdout_uart(&hardware_.console());
	std::setvbuf(stdout, nullptr, _IONBF, 0);

	init_status = init_indicators_tasks()
					.and_then([&]() -> status { return init_protocol_tasks(); })
					.and_then([&]() -> status { return init_logger_tasks(); })
					.and_then([&]() -> status { return init_sensor_tasks(); });

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
	auto display_indicator_task = [](void * arg)
	{
		auto * track_node_ptr = static_cast<track_node *>(arg);
		auto queue = track_node_ptr->indicator_queue_;
		auto & indicator = track_node_ptr->indicator_;

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

	if (auto result = task_create(display_indicator_task, "indicators_task", configMINIMAL_STACK_SIZE * 4, this, tskIDLE_PRIORITY + 2); !result)
	{
		return tl::make_unexpected(result.error());
	}

	return {};
}

status track_node::init_protocol_tasks()
{
	auto console_task = [](void * arg)
	{
		auto track_node_ptr = static_cast<track_node *>(arg);
		auto & console = track_node_ptr->console_input_;
		auto & protocol = track_node_ptr->protocol_;

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
						xQueueSend(track_node_ptr->indicator_queue_, &command, 0);
						break;
					}
					case command::read_pressure:
					case command::read_temperature:
					{
						xQueueSend(track_node_ptr->sensor_queue_, &command, 0);
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

	if (auto result = task_create(console_task, "console_task", configMINIMAL_STACK_SIZE * 4, this, tskIDLE_PRIORITY + 1); !result)
	{
		return tl::make_unexpected(result.error());
	}

	return {};
}

status track_node::init_sensor_tasks()
{
	auto sensor_task = [](void * arg)
	{
		auto* application = static_cast<track_node*>(arg);
		auto & sensor = application->hardware_.environment_sensor();
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

						break;
					}
					case command::read_pressure:
					{
						if (auto data = sensor.read_measurement())
						{
							application->log("Pressure = %f\r\n", data->pressure_pa);
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

	if (auto result = task_create(sensor_task, "sensor_task", configMINIMAL_STACK_SIZE * 4, this, tskIDLE_PRIORITY + 1); !result)
	{
		return tl::make_unexpected(result.error());
	}

	return {};
}

status track_node::init_logger_tasks()
{
	auto logger_task = [](void * arg)
	{
		auto application = static_cast<track_node*>(arg);
		auto queue = application->logger_queue_;
		auto & console = application->hardware_.console();

		log_message msg{};

		while (true)
		{
			if (xQueueReceive(queue, &msg, portMAX_DELAY) == pdPASS)
			{
				size_t len = strnlen(msg.text, sizeof(msg.text));

				(void)console.write(reinterpret_cast<const uint8_t*>(msg.text), len);
			}
		}
	};

	if (auto result = task_create(logger_task, "logger_task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY); !result)
	{
		return tl::make_unexpected(result.error());
	}

	return {};
};

void track_node::update_indicators(uint32_t now)
{
	auto indicator_status = indicator_.update(now, state_);

	if (!indicator_status)
	{
		set_error(indicator_status.error(), false);
		diagnostic_ready_ = false;
	}
}

void track_node::log(const char *fmt, ...)
{
	if (!logger_queue_)
		return;

	log_message msg{};

	va_list args;
	va_start(args, fmt);
	vsnprintf(msg.text, sizeof(msg.text), fmt, args);
	va_end(args);

	msg.text[sizeof(msg.text) - 1] = '\0';

	xQueueSend(logger_queue_, &msg, 0);
}

void track_node::set_error(error failure, bool during_init)
{
	last_error_ = failure;
	state_ = during_init ? program_state::initialization_error : program_state::runtime_error;

	if (failure.code == error_code::hal_failure)
	{
		state_ = program_state::hal_error;
	}
}