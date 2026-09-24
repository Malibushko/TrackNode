#include "composition/track_node.h"
#include "board/clock.h"
#include "core/component_list.h"
#include "platform/error.h"

int main()
{
	HAL_Init();
	if (!init_system_clock())
	{
		panic();
	}

	static uart console{ uart_config{
	    .instance = USART1,
	    .tx = { GPIOA, GPIO_PIN_9 },
	    .rx = { GPIOA, GPIO_PIN_10 },
	    .alternate = GPIO_AF7_USART1,
	    .baud_rate = 115200,
	    .mode = UART_MODE_TX_RX,
	} };
	static i2c_bus sensor_bus{ i2c_bus_config{
	    .instance = I2C1,
	    .clock_speed = 100000,
	    .pin_scl = { GPIOB, GPIO_PIN_6 },
	    .pin_sda = { GPIOB, GPIO_PIN_7 },
	} };
	static i2c_device sensor_transport{ sensor_bus, 0x76 };
	static bmp280 sensor{ sensor_transport, bmp280::config{} };
	static spi_bus storage_bus{ spi_bus_config{
	    .instance = SPI1,
	    .baud_rate_prescaler = SPI_BAUDRATEPRESCALER_256,
	    .pin_sck = { GPIOB, GPIO_PIN_3 },
	    .pin_miso = { GPIOB, GPIO_PIN_4 },
	    .pin_mosi = { GPIOB, GPIO_PIN_5 },
	} };
	static sd_card storage{ storage_bus, spi_device_config{
	    .cs_pin = { GPIOA, GPIO_PIN_15 },
	} };
	static watchdog watchdog_device{ watchdog_config{
	    .instance = IWDG,
	    .prescaler = IWDG_PRESCALER_128,
	    .reload = 0x3FF,
	} };

	static component_list devices;
	auto registered = devices.add("console", console, component_startup::initialize)
	    .and_then([&]() -> status { return devices.add("sensor_bus", sensor_bus, component_startup::initialize); })
	    .and_then([&]() -> status { return devices.add("sensor_transport", sensor_transport, component_startup::defer); })
	    .and_then([&]() -> status { return devices.add("sensor", sensor, component_startup::defer); })
	    .and_then([&]() -> status { return devices.add("storage_bus", storage_bus, component_startup::initialize); })
	    .and_then([&]() -> status { return devices.add("storage", storage, component_startup::defer); })
	    .and_then([&]() -> status { return devices.add("watchdog", watchdog_device, component_startup::defer); });
	if (!registered)
	{
		panic();
	}

	auto *console_device = devices.get<uart>("console");
	auto *sensor_device = devices.get<bmp280>("sensor");
	auto *storage_device = devices.get<sd_card>("storage");
	auto *watchdog = devices.get<::watchdog>("watchdog");
	if (!console_device || !sensor_device || !storage_device || !watchdog)
	{
		panic();
	}

	static track_node application{ devices, *console_device, *sensor_device,
	                               *storage_device, *watchdog };
	if (auto started = application.init(); !started && !application.can_signal_error())
	{
		panic();
	}
	application.launch();
}

//////////////////////////////////////////////////////////////////////////
