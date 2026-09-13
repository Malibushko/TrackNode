#include "board/board.h"
#include "common/common.h"
#include "common/debounce.h"
#include "platform/error.h"

#include "misc/status_led.h"

#include <cstdio>

board configure_track_node_board()
{
	board board{};

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


int main()
{
	gpio status_led(gpio_config{.pin = { GPIOC, GPIO_PIN_13 }, .mode = GPIO_MODE_OUTPUT_PP, .pull = GPIO_NOPULL, .speed = GPIO_SPEED_FAST });
	status_led.init();

	board track_node = configure_track_node_board();

	auto init_status = track_node.init();

	if (!init_status)
	{
		volatile error init_error = init_status.error();
		static_cast<void>(init_error);
	}

	int counter = 0;

	debounced_input<gpio, 20> button(track_node.get<gpio>("button"));

	while (true)
	{
		status_led.write(gpio::state::set);

		std::printf("Tick #%u\r\n", counter++);

		button.update(common::get_tick());

		if (button.state_changed())
		{
			if (button.state())
			{
				status_led.toggle();
			}
		}

		common::delay(1);
	}
}
