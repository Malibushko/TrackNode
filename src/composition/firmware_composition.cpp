#include "composition/firmware_composition.h"
#include "board/clock.h"

status initialize_platform()
{
	HAL_Init();
#ifdef TRACK_NODE_DEBUG_IN_SLEEP
	HAL_DBGMCU_EnableDBGSleepMode();
#endif
	return init_system_clock();
}

status configure_console(firmware_composition &application)
{
	return application.devices.add("console", application.console, component_startup::initialize)
	    .and_then([&] { return application.runtime.add(service_id::logger, application.logger); })
	    .and_then([&] { return application.runtime.add(service_id::console, application.commands, {service_id::sensor}); });
}

status configure_sensors(firmware_composition &application)
{
	return application.devices.add("sensor_bus", application.sensor_bus, component_startup::initialize)
	    .and_then([&] { return application.devices.add("sensor_transport", application.sensor_transport, component_startup::defer); })
	    .and_then([&] { return application.devices.add("sensor", application.sensor, component_startup::defer); });
}

status configure_indicator(firmware_composition &application)
{
	return application.runtime.add(service_id::indicator, application.indicator, {service_id::logger});
}

status configure_gnss(firmware_composition &application)
{
	return application.devices.add("gps_transport", application.gps_transport, component_startup::initialize)
	    .and_then([&] { return application.devices.add("gps", application.gps_module, component_startup::defer); })
	    .and_then([&] { return application.runtime.add(service_id::gnss, application.gnss, {service_id::logger}); });
}

status configure_storage(firmware_composition &application)
{
#ifndef NO_STORAGE
	return application.devices.add("storage_bus", application.storage_bus, component_startup::initialize)
	    .and_then([&] { return application.devices.add("storage", application.storage, component_startup::defer); })
	    .and_then([&] { return application.runtime.add(service_id::storage, application.records, {service_id::logger}); });
#else
	(void)application;
	return {};
#endif
}

status configure_radio(firmware_composition &application)
{
#if TRACK_NODE_ENABLE_LORA
	return application.devices.add("radio_bus", application.radio_bus, component_startup::initialize)
	    .and_then([&] { return application.devices.add("radio", application.radio_module, component_startup::defer); })
	    .and_then([&] { return application.telemetry.add(application.filtered_radio); })
	    .and_then([&] { return application.runtime.add(service_id::radio, application.radio, {service_id::logger}); });
#else
	(void)application;
	return {};
#endif
}

status configure_bluetooth(firmware_composition &application)
{
#if TRACK_NODE_ENABLE_BLUETOOTH
	return application.telemetry.add(application.bluetooth)
	    .and_then([&] { return application.runtime.add(service_id::bluetooth, application.bluetooth, {service_id::logger}); });
#else
	(void)application;
	return {};
#endif
}

status configure_controller(firmware_composition &application)
{
	application.telemetry.seal();
	return application.runtime.add(service_id::sensor, application.controller,
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
}

status configure_watchdog(firmware_composition &application)
{
	return application.devices.add("watchdog", application.watchdog_device, component_startup::defer)
	    .and_then([&] { return application.runtime.add(service_id::watchdog, application.monitor, {service_id::console}); });
}
