#include "board/board.h"
#include "board/track_node_control_protocol.h"
#include "common/common.h"
#include "misc/coro_helpers.h"
#include "platform/error.h"

#include "misc/status_led.h"
#include "misc/uart_read_adapter.h"
#include "platform/stdio.h"

#include <cstdio>

board configure_track_node_board()
{
	board board{};
	board.add<gpio>("status_led", gpio_config{
		.pin = { GPIOC, GPIO_PIN_13 },
		.mode = GPIO_MODE_OUTPUT_PP,
		.pull = GPIO_NOPULL,
		.speed = GPIO_SPEED_LOW,
		.active_low = true
	});

	board.add<uart>("console", uart_config{
			.instance  = USART1,
			.tx        = pin{ GPIOA, GPIO_PIN_9 },
			.rx        = pin{ GPIOA, GPIO_PIN_10 },
			.alternate = GPIO_AF7_USART1,
			.baud_rate = 115200,
			.mode      = UART_MODE_TX_RX,
	});

	board.add<timer>("led_timer", timer_config{
			.instance    = TIM2,
			.prescaler   = 71,
			.auto_reload = 999,
	});

	board.add<pwm>("led", board.get<timer>("led_timer"), pwm_config{
			.pin       = pin{ GPIOA, GPIO_PIN_1 },
			.alternate = GPIO_AF1_TIM2,
			.channel   = TIM_CHANNEL_2,
			.polarity  = TIM_OCPOLARITY_HIGH,
	});

	board.add<adc>("analog", adc_config{
			.pin         = pin{ GPIOA, GPIO_PIN_0 },
			.channel     = ADC_CHANNEL_0,
			.sample_time = ADC_SAMPLETIME_15CYCLES,
	});

	board.add<gpio>("button", gpio_config{
			.pin  = pin{ GPIOA, GPIO_PIN_15 },
			.mode = GPIO_MODE_INPUT,
			.pull = GPIO_PULLUP,
	});

	return board;
}

int main()
{
	HAL_Init();

	board track_node = configure_track_node_board();

	if (status init_status = track_node.init(); !init_status)
	{
		error init_error = init_status.error();
		error_handler(init_error.code);
	}

	// $NOTE: here we just hope that status_led was initialized
	gpio& status_led = track_node.get<gpio>("status_led");
	status_blink_task status_display = make_status_blinker(track_node.get<gpio>("status_led"), g_state);
	status_led.write(gpio::state::reset);

	uart &console = track_node.get<uart>("console");
	set_stdout_uart(&console);

	uart_read_adapter console_adapter(console);
	track_node_control_protocol commandReader(console_adapter);

	uint32_t tick = 0;

	while (++tick)
	{
		status_display.poll();

		switch (g_state)
		{
			case program_state::ok:
			{
				commandReader.consume();

				if (!commandReader.available())
				{
					common::delay(50);
				} else
				{
					switch (commandReader.read())
					{
					    case command::blink_once:
					    {
					    	run_to_completion(make_status_blinker(status_led, 1));

						    break;
					    }
					    case command::blink_twice:
					    {
					    	run_to_completion(make_status_blinker(status_led, 2));

						    break;
					    }
					    case command::blink_thrice:
					    {
					    	run_to_completion(make_status_blinker(status_led, 3));

						    break;
					    }
				    }
			    }

				break;
			}

			case program_state::initialization_error:
			case program_state::runtime_error:
			case program_state::hal_error:
			{
				common::delay(50);

				break;
			}
		}
	}
}
