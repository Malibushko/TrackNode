#include "composition/service_list.h"
#include <cassert>
#include <vector>

struct worker
{
	int id;
	std::vector<int> &events;
	bool fails = false;
	status start() { events.push_back(id); return fails ? status{fail(component::service, error_code::not_ready)} : status{}; }
	void stop() { events.push_back(-id); }
};

int main()
{
	std::vector<int> events;
	worker logger{1, events}, sensor{2, events}, console{3, events};
	service_list services;
	assert(services.add(service_id::console, console, {service_id::sensor}));
	assert(services.add(service_id::sensor, sensor, {service_id::logger}));
	assert(services.add(service_id::logger, logger));
	assert(!services.add(service_id::radio, logger));
	assert(services.prepare());
	assert(!services.add(service_id::radio, console));
	assert(services.start());
	assert(!services.start());
	services.stop(); services.stop();
	assert((events == std::vector<int>{1, 2, 3, -3, -2, -1}));

	events.clear(); sensor.fails = true;
	service_list rollback;
	assert(rollback.add(service_id::logger, logger));
	assert(rollback.add(service_id::sensor, sensor, {service_id::logger}));
	assert(rollback.add(service_id::console, console, {service_id::sensor}));
	assert(rollback.prepare());
	assert(!rollback.start()); rollback.stop();
	assert((events == std::vector<int>{1, 2, -2, -1}));

	service_list missing;
	assert(missing.add(service_id::console, console, {service_id::radio}));
	assert(!missing.prepare()); assert(!missing.start());
	service_list cycle;
	assert(cycle.add(service_id::logger, logger, {service_id::sensor}));
	assert(cycle.add(service_id::sensor, sensor, {service_id::logger}));
	assert(!cycle.prepare());
	service_list empty;
	assert(empty.prepare()); assert(empty.start()); empty.stop();
}
