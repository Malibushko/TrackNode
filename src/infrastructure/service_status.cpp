#include "infrastructure/service_status.h"

const char *service_name(service_id id)
{
	switch (id)
	{
		case service_id::console:
			return "console";
		case service_id::sensor:
			return "sensor";
#ifndef NO_STORAGE
		case service_id::storage:
			return "storage";
#endif
		case service_id::logger:
			return "logger";
		case service_id::indicator:
			return "indicator";
		case service_id::watchdog:
			return "watchdog";
		case service_id::gnss:
			return "gnss";
		case service_id::radio:
			return "radio";
		case service_id::bluetooth:
			return "bluetooth";
		case service_id::control_button:
			return "control_button";
		case service_id::count:
			break;
	}
	return "unknown";
}

//////////////////////////////////////////////////////////////////////////

const char *health_name(service_health health)
{
	switch (health)
	{
		case service_health::disabled:
			return "disabled";
		case service_health::starting:
			return "starting";
		case service_health::ready:
			return "ready";
		case service_health::degraded:
			return "degraded";
		case service_health::failed:
			return "failed";
	}
	return "unknown";
}
