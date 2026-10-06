#pragma once

#include "composition/track_node.h"
#include "core/component_list.h"
#include "drivers/devices/neo6m.h"
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
// Static ownership and constructor wiring; used only at composition time.
struct firmware_composition
{
	uart console{ uart_config{
	    .instance = USART1,
	    .tx = { GPIOA, GPIO_PIN_9 },
	    .rx = { GPIOA, GPIO_PIN_10 },
	    .alternate = GPIO_AF7_USART1,
	    .baud_rate = 115200,
	    .mode = UART_MODE_TX_RX,
	} };
	i2c_bus sensor_bus{ i2c_bus_config{
	    .instance = I2C1,
	    .clock_speed = 100000,
	    .pin_scl = { GPIOB, GPIO_PIN_6 },
	    .pin_sda = { GPIOB, GPIO_PIN_7 },
	} };
	i2c_device sensor_transport{ sensor_bus, 0x76 };
	bmp280 sensor{ sensor_transport, bmp280::config{} };
#ifndef NO_STORAGE
	spi_bus storage_bus{ spi_bus_config{
	    .instance = SPI1,
	    .baud_rate_prescaler = SPI_BAUDRATEPRESCALER_256,
	    .pin_sck = { GPIOB, GPIO_PIN_3 },
	    .pin_miso = { GPIOB, GPIO_PIN_4 },
	    .pin_mosi = { GPIOB, GPIO_PIN_5 },
	} };
	sd_card storage{ storage_bus, spi_device_config{
	    .cs_pin = { GPIOA, GPIO_PIN_15 },
	} };
#endif
	watchdog watchdog_device{ watchdog_config{
	    .instance = IWDG,
	    .prescaler = IWDG_PRESCALER_128,
	    .reload = 0x3FF,
	} };
	uart gps_transport = uart{ uart_config{
		.instance = USART2,
		.tx = { GPIOA, GPIO_PIN_2 },
		.rx = { GPIOA, GPIO_PIN_3 },
		.alternate = GPIO_AF7_USART2,
		.baud_rate = 9600,
		.mode = UART_MODE_TX_RX,
	} };

#if TRACK_NODE_ENABLE_LORA
	spi_bus radio_bus = spi_bus { spi_bus_config {
		.instance = SPI2,
		.baud_rate_prescaler = SPI_BAUDRATEPRESCALER_8,
		.pin_sck  = { GPIOB, GPIO_PIN_13 },
		.pin_miso = { GPIOB, GPIO_PIN_14 },
		.pin_mosi = { GPIOB, GPIO_PIN_15, }
	} };

	sx126x_driver radio_module = sx126x_driver{ radio_bus, sx126x_config {
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

	neo6m gps_module { gps_transport };

	component_list devices;
	supervisor monitor{watchdog_device};
	logger_service logger{console, monitor};
	sensor_service measurements{sensor};
	board_state_service board_state;
	indicator_service indicator{logger, monitor};
	gnss_service gnss{gps_module, logger, monitor};
#ifndef NO_STORAGE
	storage_service records{storage, storage_service::file_config{"logs", sample_csv_header}, logger, monitor};
#endif
	telemetry_router<2> telemetry;
	node_controller controller{measurements,
#ifndef NO_STORAGE
	    records,
#endif
	    indicator, logger, gnss, telemetry, board_state, monitor};
	console_service commands{console, controller, logger, monitor};
	track_node runtime{devices, monitor, indicator};

#if TRACK_NODE_ENABLE_LORA
	radio_service radio{radio_module, logger, monitor};
	radio_telemetry_filter filtered_radio{radio};
#endif
#if TRACK_NODE_ENABLE_BLUETOOTH
	// TODO(user): choose a free UART and pins. Console/GPS ports cannot be shared.
	// This deliberately invalid configuration is never initialized by the scaffold.
	uart ble_uart{uart_config{.instance = nullptr, .alternate = 0,
	    .baud_rate = 115200, .mode = UART_MODE_TX_RX}};
	xiao_ble_bridge ble_backend{ble_uart};
	bluetooth_service bluetooth{ble_backend, controller, logger, monitor};
#endif
};

status initialize_platform();
status configure_console(firmware_composition &application);
status configure_sensors(firmware_composition &application);
status configure_indicator(firmware_composition &application);
status configure_gnss(firmware_composition &application);
status configure_storage(firmware_composition &application);
status configure_radio(firmware_composition &application);
status configure_bluetooth(firmware_composition &application);
status configure_controller(firmware_composition &application);
status configure_watchdog(firmware_composition &application);
