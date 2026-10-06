#pragma once

#include "application/deferred_command_sink.h"
#include "application/telemetry_router.h"
#include "composition/service_list.h"
#include "core/component_list.h"

class supervisor;
class logger_service;
class sensor_service;
class indicator_service;
class gnss_service;
class storage_service;
class track_node;

// One firmware composition. Modules own function-local static objects.
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

status initialize_platform();
status configure_watchdog(firmware_composition &application);
status configure_console(firmware_composition &application);
status configure_sensors(firmware_composition &application);
status configure_indicator(firmware_composition &application);
status configure_gnss(firmware_composition &application);
status configure_storage(firmware_composition &application);
status configure_radio(firmware_composition &application);
status configure_bluetooth(firmware_composition &application);
status configure_controller(firmware_composition &application);
status configure_runtime(firmware_composition &application);
