#include "board/track_node_board.h"
#include <array>

track_node_board::track_node_board()
    : console_(uart_config{
          .instance = USART1,
          .tx = pin{ GPIOA, GPIO_PIN_9 },
          .rx = pin{ GPIOA, GPIO_PIN_10 },
          .alternate = GPIO_AF7_USART1,
          .baud_rate = 115200,
          .mode = UART_MODE_TX_RX,
      }),
      sensor_bus_(i2c_bus_config{
          .instance = I2C1,
          .clock_speed = 100000,
          .pin_scl = { GPIOB, GPIO_PIN_6 },
          .pin_sda = { GPIOB, GPIO_PIN_7 },
      }),
      sensor_transport_(sensor_bus_, 0x76),
	  environment_sensor_(sensor_transport_, bmp280::config{})
{
}

status track_node_board::init()
{
	const std::array components{ component_ref{ console_ }, component_ref{ sensor_bus_ },
		                         component_ref{ environment_sensor_ } };
	return lifecycle_.init(components);
}
