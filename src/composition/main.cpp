#include "composition/track_node.h"
#include "board/clock.h"
#include "core/component_list.h"
#include "drivers/devices/neo6m.h"
#include "platform/error.h"
#include "application/node_controller.h"
#include "application/telemetry_router.h"
#include "infrastructure/console_service.h"
#include "infrastructure/sensor_service.h"
#include "infrastructure/board_state_service.h"
#include "infrastructure/gnss_service.h"
#if TRACK_NODE_ENABLE_LORA
#include "application/radio_telemetry_filter.h"
#include "drivers/devices/sx126x.h"
#include "infrastructure/radio_service.h"
#endif
#if TRACK_NODE_ENABLE_BLUETOOTH
#include "drivers/devices/xiao_ble_bridge.h"
#include "infrastructure/bluetooth_service.h"
#endif
#ifndef NO_STORAGE
#include "infrastructure/storage_service.h"
#endif

int main()
{
	HAL_Init();
#ifdef TRACK_NODE_DEBUG_IN_SLEEP
	HAL_DBGMCU_EnableDBGSleepMode();
#endif
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
#ifndef NO_STORAGE
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
#endif
	static watchdog watchdog_device{ watchdog_config{
	    .instance = IWDG,
	    .prescaler = IWDG_PRESCALER_128,
	    .reload = 0x3FF,
	} };
	static uart gps_transport = uart{ uart_config{
		.instance = USART2,
		.tx = { GPIOA, GPIO_PIN_2 },
		.rx = { GPIOA, GPIO_PIN_3 },
		.alternate = GPIO_AF7_USART2,
		.baud_rate = 9600,
		.mode = UART_MODE_TX_RX,
	} };

#if TRACK_NODE_ENABLE_LORA
	static spi_bus radio_bus = spi_bus { spi_bus_config {
		.instance = SPI2,
		.baud_rate_prescaler = SPI_BAUDRATEPRESCALER_8,
		.pin_sck  = { GPIOB, GPIO_PIN_13 },
		.pin_miso = { GPIOB, GPIO_PIN_14 },
		.pin_mosi = { GPIOB, GPIO_PIN_15, }
	} };

	static sx126x_driver radio_module = sx126x_driver{ radio_bus, sx126x_config {
		.nss   = { GPIOB, GPIO_PIN_12 },
		.reset = { GPIOA, GPIO_PIN_4 },
		.busy  = { GPIOB, GPIO_PIN_0 },
		.dio1  = { GPIOA, GPIO_PIN_5 },
		.txen  = { GPIOA, GPIO_PIN_6 },
		.rxen  = { GPIOA, GPIO_PIN_7 },
		.frequency_hz = 868'100'000,
		.spreading_factor = 7,
		.bandwidth = sx126x_lora_bandwidth::KHZ_125,
		.coding_rate = sx126x_lora_coding_rate::CR_4_5,
		.low_data_rate_optimization = false,
		.preamble_symbols = 8,
		.explicit_header = true,
		.crc_enabled = true,
		.invert_iq = false,
		.sync_word = 0x1424,
		.tx_power_dbm = 10
	}};

#endif

	static neo6m gps_module { gps_transport };

	static component_list devices;
	auto registered = devices.add("console", console, component_startup::initialize)
	    .and_then([&]() -> status { return devices.add("sensor_bus", sensor_bus, component_startup::initialize); })
	    .and_then([&]() -> status { return devices.add("sensor_transport", sensor_transport, component_startup::defer); })
	    .and_then([&]() -> status { return devices.add("sensor", sensor, component_startup::defer); })
#ifndef NO_STORAGE
	    .and_then([&]() -> status { return devices.add("storage_bus", storage_bus, component_startup::initialize); })
	    .and_then([&]() -> status { return devices.add("storage", storage, component_startup::defer); })
#endif
	    .and_then([&]() -> status { return devices.add("watchdog", watchdog_device, component_startup::defer); })
		.and_then([&]() -> status { return devices.add("gps_transport", gps_transport, component_startup::initialize); })
	    .and_then([&]() -> status { return devices.add("gps", gps_module, component_startup::defer); })
#if TRACK_NODE_ENABLE_LORA
		.and_then([&]() -> status { return devices.add("radio_bus", radio_bus, component_startup::initialize);})
		.and_then([&]() -> status { return devices.add("radio", radio_module, component_startup::defer);})
#endif
	;

	if (!registered)
	{
		panic();
	}

	// Services own their queues/tasks; main owns all service lifetimes.
	static supervisor monitor{watchdog_device};
	static logger_service logger{console, monitor};
	static sensor_service measurements{sensor};
	static board_state_service board_state;
	static indicator_service indicator{logger, monitor};
	static gnss_service gnss{gps_module, logger, monitor};
#ifndef NO_STORAGE
	static storage_service records{storage, storage_service::file_config{"logs", sample_csv_header}, logger, monitor};
#endif
	static telemetry_router telemetry;
	static node_controller controller{measurements,
#ifndef NO_STORAGE
	    records,
#endif
	    indicator, logger, gnss, telemetry, board_state, monitor};
	static console_service commands{console, controller, logger, monitor};
	static track_node application{devices, monitor, indicator};

	auto configured = application.add(service_id::logger, logger)
	    .and_then([&]() { return application.add(service_id::indicator, indicator, {service_id::logger}); })
	    .and_then([&]() { return application.add(service_id::gnss, gnss, {service_id::logger}); })
#ifndef NO_STORAGE
	    .and_then([&]() { return application.add(service_id::storage, records, {service_id::logger}); })
#endif
	;

#if TRACK_NODE_ENABLE_LORA
	static radio_service radio{radio_module, logger, monitor};
	static radio_telemetry_filter filtered_radio{radio};
	configured = configured
	    .and_then([&]() { return telemetry.add(filtered_radio); })
	    .and_then([&]() { return application.add(service_id::radio, radio, {service_id::logger}); });
#endif
#if TRACK_NODE_ENABLE_BLUETOOTH
	// TODO(user): choose a free UART and pins. Console/GPS ports cannot be shared.
	// This deliberately invalid configuration is never initialized by the scaffold.
	static uart ble_uart{uart_config{.instance = nullptr, .alternate = 0,
	    .baud_rate = 115200, .mode = UART_MODE_TX_RX}};
	static xiao_ble_bridge ble_backend{ble_uart};
	static bluetooth_service bluetooth{ble_backend, controller, logger, monitor};
	configured = configured
	    .and_then([&]() { return telemetry.add(bluetooth); })
	    .and_then([&]() { return application.add(service_id::bluetooth, bluetooth, {service_id::logger}); });
#endif
	telemetry.seal();
	configured = configured
	    .and_then([&]() { return application.add(service_id::sensor, controller,
	        {service_id::logger, service_id::indicator, service_id::gnss,
#ifndef NO_STORAGE
	         service_id::storage,
#endif
#if TRACK_NODE_ENABLE_LORA
	         service_id::radio,
#endif
#if TRACK_NODE_ENABLE_BLUETOOTH
	         service_id::bluetooth,
#endif
	        }); })
	    .and_then([&]() { return application.add(service_id::console, commands, {service_id::sensor}); })
	    .and_then([&]() { return application.add(service_id::watchdog, monitor, {service_id::console}); });
	if (!configured) panic();

	if (auto started = application.init(); !started && !application.can_signal_error())
	{
		panic();
	}
	application.launch();
}

//////////////////////////////////////////////////////////////////////////
