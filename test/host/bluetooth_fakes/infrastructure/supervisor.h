#pragma once
#include "infrastructure/service_status.h"
class supervisor
{
public:
	service_health current = service_health::starting;
	unsigned errors = 0;
	std::array<service_diagnostic, static_cast<std::size_t>(service_id::count)> diagnostics;
	supervisor()
	{
		diagnostics.fill({service_health::ready, std::nullopt});
	}
	service_diagnostic diagnostic(service_id id) const
	{
		return diagnostics[static_cast<std::size_t>(id)];
	}
	void set_health(service_id id, service_health health)
	{
		current = health;
		diagnostics[static_cast<std::size_t>(id)] = {health, std::nullopt};
	}
	void report(service_id id, error failure, service_health health)
	{
		++errors;
		current = health;
		diagnostics[static_cast<std::size_t>(id)] = {health, failure};
	}
	void progress(service_id, TickType_t) {}
};
