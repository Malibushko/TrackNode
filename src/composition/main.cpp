#include "application/deferred_command_sink.h"
#include "application/telemetry_router.h"
#include "composition/track_node.h"
#include "board/clock.h"
#include "platform/error.h"
#include "application/node_controller.h"
#include "drivers/devices/neo6m.h"
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

class storage_service;

//////////////////////////////////////////////////////////////////////////

struct firmware_composition
{
	component_list devices;
	service_list services;
	telemetry_router<2> telemetry;
	deferred_command_sink commands;

	supervisor *monitor = nullptr;
	logger_service *logger = nullptr;
	sensor_service *measurements = nullptr;
	indicator_service *indicator = nullptr;
	gnss_service *gnss = nullptr;
	storage_service *records = nullptr;

	track_node *runtime = nullptr;
};

//////////////////////////////////////////////////////////////////////////

status initialize_platform()
{
	HAL_Init();
#ifdef TRACK_NODE_DEBUG_IN_SLEEP
	HAL_DBGMCU_EnableDBGSleepMode();
#endif

	return init_system_clock();
}

//////////////////////////////////////////////////////////////////////////

status configure_watchdog(firmware_composition &application)
{
	static watchdog device{watchdog_config{
	    .instance = IWDG,
	    .prescaler = IWDG_PRESCALER_128,
	    .reload = 0x3FF
	}};

	if (auto added = application.devices.add("watchdog", device, component_startup::defer); !added)
	{
		return added;
	}

	static supervisor monitor{device};

	if (auto added = application.services.add(service_id::watchdog, monitor, {service_id::console}); !added)
	{
		return added;
	}

	application.monitor = &monitor;

	return {};
}

//////////////////////////////////////////////////////////////////////////

status configure_console(firmware_composition &application)
{
	if (!application.monitor)
	{
		return fail(component::service, error_code::invalid_config);
	}

	static uart device{uart_config{
	    .instance = USART1,
	    .tx = {GPIOA, GPIO_PIN_9},
	    .rx = {GPIOA, GPIO_PIN_10},
	    .alternate = GPIO_AF7_USART1,
	    .baud_rate = 115200,
	    .mode = UART_MODE_TX_RX
	}};

	if (auto added = application.devices.add("console", device, component_startup::initialize); !added)
	{
		return added;
	}

	static logger_service logger{device, *application.monitor};

	if (auto added = application.services.add(service_id::logger, logger); !added)
	{
		return added;
	}

	static console_service console{device, application.commands, logger, *application.monitor};

	if (auto added = application.services.add(service_id::console, console, {service_id::sensor}); !added)
	{
		return added;
	}

	application.logger = &logger;

	return {};
}

//////////////////////////////////////////////////////////////////////////

status configure_sensors(firmware_composition &application)
{
	static i2c_bus bus{i2c_bus_config{
	    .instance = I2C1,
	    .clock_speed = 100000,
	    .pin_scl = {GPIOB, GPIO_PIN_6},
	    .pin_sda = {GPIOB, GPIO_PIN_7}
	}};

	static i2c_device transport{bus, 0x76};
	static bmp280 sensor{transport, bmp280::config{}};

	auto added = application.devices.add("sensor_bus", bus, component_startup::initialize)
	    .and_then([&]{ return application.devices.add("sensor_transport", transport, component_startup::defer); })
	    .and_then([&]{ return application.devices.add("sensor", sensor, component_startup::defer); });

	if (!added)
	{
		return added;
	}

	static sensor_service measurements{sensor};
	application.measurements = &measurements;

	return {};
}

//////////////////////////////////////////////////////////////////////////

status configure_indicator(firmware_composition &application)
{
	if (!application.logger || !application.monitor)
	{
		return fail(component::service, error_code::invalid_config);
	}

	static indicator_service indicator{*application.logger, *application.monitor};

	if (auto added = application.services.add(service_id::indicator, indicator, {service_id::logger}); !added)
	{
		return added;
	}

	application.indicator = &indicator;

	return {};
}

//////////////////////////////////////////////////////////////////////////

status configure_gnss(firmware_composition &application)
{
	if (!application.logger || !application.monitor)
	{
		return fail(component::service, error_code::invalid_config);
	}

	static uart transport{uart_config{
	    .instance = USART2,
	    .tx = {GPIOA, GPIO_PIN_2},
	    .rx = {GPIOA, GPIO_PIN_3},
	    .alternate = GPIO_AF7_USART2,
	    .baud_rate = 9600,
	    .mode = UART_MODE_TX_RX
	}};

	static neo6m device{transport};

	auto added = application.devices.add("gps_transport", transport, component_startup::initialize)
	    .and_then([&]{ return application.devices.add("gps", device, component_startup::defer); });

	if (!added)
	{
		return added;
	}

	static gnss_service gnss{device, *application.logger, *application.monitor};

	if (auto registered = application.services.add(service_id::gnss, gnss, {service_id::logger}); !registered)
	{
		return registered;
	}

	application.gnss = &gnss;

	return {};
}

//////////////////////////////////////////////////////////////////////////

status configure_storage(firmware_composition &application)
{
#ifndef NO_STORAGE
	if (!application.logger || !application.monitor)
	{
		return fail(component::service, error_code::invalid_config);
	}

	static spi_bus bus{spi_bus_config{
	    .instance = SPI1,
	    .baud_rate_prescaler = SPI_BAUDRATEPRESCALER_256,
	    .pin_sck = {GPIOB, GPIO_PIN_3},
	    .pin_miso = {GPIOB, GPIO_PIN_4},
	    .pin_mosi = {GPIOB, GPIO_PIN_5}
	}};

	static sd_card card{bus, spi_device_config{
	    .cs_pin = {GPIOA, GPIO_PIN_15}
	}};

	auto added = application.devices.add("storage_bus", bus, component_startup::initialize)
	    .and_then([&]{ return application.devices.add("storage", card, component_startup::defer); });

	if (!added)
	{
		return added;
	}

	static storage_service records{card, storage_service::file_config{
	    "logs",
	    sample_csv_header
	},
	    *application.logger, *application.monitor};

	if (auto registered = application.services.add(service_id::storage, records, {service_id::logger}); !registered)
	{
		return registered;
	}

	application.records = &records;
#else
	(void)application;
#endif

	return {};
}

//////////////////////////////////////////////////////////////////////////

status configure_radio(firmware_composition &application)
{
#if TRACK_NODE_ENABLE_LORA
	if (!application.logger || !application.monitor)
	{
		return fail(component::service, error_code::invalid_config);
	}

	static spi_bus bus{spi_bus_config{
	    .instance = SPI2,
	    .baud_rate_prescaler = SPI_BAUDRATEPRESCALER_8,
	    .pin_sck = {GPIOB, GPIO_PIN_13},
	    .pin_miso = {GPIOB, GPIO_PIN_14},
	    .pin_mosi = {GPIOB, GPIO_PIN_15}
	}};

	static sx126x_driver device{bus, sx126x_config{
	    .nss = {GPIOB, GPIO_PIN_12},
	    .reset = {GPIOA, GPIO_PIN_4},
	    .busy = {GPIOB, GPIO_PIN_0},
	    .dio1 = {GPIOA, GPIO_PIN_5},
	    .txen = {GPIOA, GPIO_PIN_6},
	    .rxen = {GPIOA, GPIO_PIN_7},
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

	auto added = application.devices.add("radio_bus", bus, component_startup::initialize)
	    .and_then([&]{ return application.devices.add("radio", device, component_startup::defer); });

	if (!added)
	{
		return added;
	}

	static radio_service radio{device, *application.logger, *application.monitor};
	static radio_telemetry_filter filtered_radio{radio};

	return application.services.add(service_id::radio, radio, {service_id::logger})
	    .and_then([&]{ return application.telemetry.add(filtered_radio); });
#else
	(void)application;

	return {};
#endif
}

//////////////////////////////////////////////////////////////////////////

status configure_bluetooth(firmware_composition &application)
{
#if TRACK_NODE_ENABLE_BLUETOOTH
	if (!application.logger || !application.monitor)
	{
		return fail(component::service, error_code::invalid_config);
	}

	static uart transport{uart_config{
	    .instance = nullptr, // TODO select UART interface
		.tx = pin{}, // TODO
		.rx = pin{}, // TODO
	    .alternate = 0, // TODO
	    .baud_rate = 115200,
	    .mode = UART_MODE_TX_RX,
	}};

	static xiao_ble_bridge backend{transport};
	static bluetooth_service bluetooth{backend, application.commands, *application.logger, *application.monitor};

	return application.services.add(service_id::bluetooth, bluetooth, {service_id::logger})
	    .and_then([&]{ return application.telemetry.add(bluetooth); });
#else
	(void)application;

	return {};
#endif
}

//////////////////////////////////////////////////////////////////////////

status configure_controller(firmware_composition &application)
{
	if (!application.measurements || !application.indicator || !application.logger ||
	    !application.gnss || !application.monitor || application.commands.bound())
	{
		return fail(component::service, error_code::invalid_config);
	}
#ifndef NO_STORAGE
	if (!application.records)
	{
		return fail(component::service, error_code::invalid_config);
	}
#endif

	static board_state_service board_state;
	static node_controller controller{*application.measurements,
#ifndef NO_STORAGE
	    *application.records,
#endif
	    *application.indicator, *application.logger, *application.gnss,
	    application.telemetry, board_state, *application.monitor};

	auto added = application.services.add(service_id::sensor, controller,
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
	    });

	if (!added)
	{
		return added;
	}
	if (auto bound = application.commands.bind(controller); !bound)
	{
		return bound;
	}

	application.telemetry.seal();

	return {};
}

//////////////////////////////////////////////////////////////////////////

status configure_runtime(firmware_composition &application)
{
	if (!application.monitor || !application.indicator || !application.commands.bound() || application.runtime)
	{
		return fail(component::service, error_code::invalid_config);
	}

	static track_node runtime{application.devices, application.services, *application.monitor, *application.indicator};
	application.runtime = &runtime;

	return {};
}

//////////////////////////////////////////////////////////////////////////

int main()
{
	if (!initialize_platform())
	{
		panic();
	}

	static firmware_composition application;

	auto configured = configure_watchdog(application)
	    .and_then([&]{ return configure_console(application); })
	    .and_then([&]{ return configure_sensors(application); })
	    .and_then([&]{ return configure_indicator(application); })
	    .and_then([&]{ return configure_gnss(application); })
	    .and_then([&]{ return configure_storage(application); })
	    .and_then([&]{ return configure_radio(application); })
	    .and_then([&]{ return configure_bluetooth(application); })
	    .and_then([&]{ return configure_controller(application); })
	    .and_then([&]{ return configure_runtime(application); });

	if (!configured)
	{
		panic();
	}

	if (auto started = application.runtime->init(); !started && !application.runtime->can_signal_error())
	{
		panic();
	}

	application.runtime->launch();
}
