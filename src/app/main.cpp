#include "board/board.h"
#include "common/common.h"
#include "common/debounce.h"
#include "platform/error.h"

#include "misc/status_led.h"

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
			.rx        = pin{},
			.alternate = GPIO_AF7_USART1,
			.baud_rate = 115200,
			.mode      = UART_MODE_TX,
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

void main_loop()
{
	common::delay(50);
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
	status_blink_task status_display = make_status_blinker(track_node.get<gpio>("status_led"), g_state);

	while (true)
	{
		status_display.poll();

		switch (g_state)
		{
			case program_state::ok:
			{
				main_loop();
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
