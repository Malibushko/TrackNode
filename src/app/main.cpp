#include "board/board.h"
#include "common/common.h"
#include "platform/error.h"

#include "misc/status_led.h"
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
	status_blink_task status_display = make_status_blinker(track_node.get<gpio>("status_led"), g_state);

	auto & console = track_node.get<uart>("console");
	set_stdout_uart(&track_node.get<uart>("console"));

	std::array<uint8_t, 64> buffer{};

	int tick = 0;

	while (true)
	{
		status_display.poll();

		switch (g_state)
		{
			case program_state::ok:
			{
				uint32_t rx_available = console.available();
				if (!rx_available)
				{
					std::printf("[TICK %u] No data received.\r\n", tick++);

					common::delay(500);
					break;
				}

				size_t read_count{};
				console.read_available(buffer.data(), std::min<uint32_t>(buffer.size(), rx_available), read_count);

				// just echo for now
				console.write(buffer.data(), read_count);
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
