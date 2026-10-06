#include "composition/firmware_composition.h"
#include "platform/error.h"

int main()
{
	if (!initialize_platform()) panic();

	static firmware_composition application;
	auto configured = configure_console(application)
	    .and_then([&] { return configure_sensors(application); })
	    .and_then([&] { return configure_indicator(application); })
	    .and_then([&] { return configure_gnss(application); })
	    .and_then([&] { return configure_storage(application); })
	    .and_then([&] { return configure_radio(application); })
	    .and_then([&] { return configure_bluetooth(application); })
	    .and_then([&] { return configure_controller(application); })
	    .and_then([&] { return configure_watchdog(application); });
	if (!configured) panic();

	if (auto started = application.runtime.init(); !started && !application.runtime.can_signal_error())
	{
		panic();
	}
	application.runtime.launch();
}
