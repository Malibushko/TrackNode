#include "board/track_node_board.h"

board configure_track_node_board()
{
	board board{};

	board.add<uart>("console", uart_config{
	                               .instance = USART1,
	                               .tx = pin{ GPIOA, GPIO_PIN_9 },
	                               .rx = pin{ GPIOA, GPIO_PIN_10 },
	                               .alternate = GPIO_AF7_USART1,
	                               .baud_rate = 115200,
	                               .mode = UART_MODE_TX_RX,
	                           });

	board.add<timer>("led_timer", timer_config{
	                                  .instance = TIM2,
	                                  .prescaler = 71,
	                                  .auto_reload = 999,
	                              });

	board.add<pwm>("led", board.get<timer>("led_timer"),
	               pwm_config{
	                   .pin = pin{ GPIOA, GPIO_PIN_1 },
	                   .alternate = GPIO_AF1_TIM2,
	                   .channel = TIM_CHANNEL_2,
	                   .polarity = TIM_OCPOLARITY_HIGH,
	               });

	board.add<adc>("analog", adc_config{
	                             .pin = pin{ GPIOA, GPIO_PIN_0 },
	                             .channel = ADC_CHANNEL_0,
	                             .sample_time = ADC_SAMPLETIME_15CYCLES,
	                         });

	board.add<gpio>("button", gpio_config{
	                              .pin = pin{ GPIOA, GPIO_PIN_15 },
	                              .mode = GPIO_MODE_INPUT,
	                              .pull = GPIO_PULLUP,
	                          });

	return board;
}
