#include "board/track_node_board.h"

#include "drivers/i2c_bus.h"

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

	board.add<i2c_bus>("i2c1", i2c_bus_config{
		.instance = I2C1,
		.clock_speed = 100000,
		.pin_scl = {GPIOB, GPIO_PIN_6},
		.pin_sda = {GPIOB, GPIO_PIN_7}
	});

	board.add<i2c_device>("i2c1_bpe280",board.get<i2c_bus>("i2c1"), static_cast<uint8_t>(0x77));

	return board;
}
