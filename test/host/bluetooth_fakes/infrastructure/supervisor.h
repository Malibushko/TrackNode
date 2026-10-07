#pragma once
#include "infrastructure/service_status.h"
class supervisor
{
public:
	service_health current = service_health::starting;
	unsigned errors = 0;
	void set_health(service_id, service_health health)
	{
		current = health;
	}
	void report(service_id, error, service_health health)
	{
		++errors;
		current = health;
	}
	void progress(service_id, TickType_t) {}
};
