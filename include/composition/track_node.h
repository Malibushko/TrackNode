#pragma once

#include "composition/service_list.h"
#include "infrastructure/indicator_service.h"
#include "core/component_list.h"
#include "core/hardware_object.h"

// Bootstrap only: concrete services and devices are owned by the composition.
class track_node : public hardware_object
{
public:
	track_node(component_list &devices, supervisor &monitor, indicator_service &indicator)
		: devices_(devices), supervisor_(monitor), indicator_(indicator) {}

public: // Composition API
	template<class Service>
	status add(service_id id, Service &service, std::initializer_list<service_id> dependencies = {})
	{ return services_.add(id, service, dependencies); }

public: // Application lifecycle
	status init();
	[[noreturn]] void launch();

public: // Application state and diagnostics
	[[nodiscard]] program_state state() const { return supervisor_.state(); }
	[[nodiscard]] std::optional<error> last_error() const { return supervisor_.last_error(); }
	[[nodiscard]] bool can_signal_error() const { return indicator_.diagnostic_ready(); }

private:
	static void init_task(void *argument);
	status startup();
	void stop_workers();

	component_list &devices_;
	supervisor &supervisor_;
	indicator_service &indicator_;
	service_list services_;
	bool init_requested_ = false;
	bool attempted_ = false;
};
