#include "common/power_profile.h"

#include <cassert>
#include <cstring>

int main()
{
	power_profile profile;
	int console = 0;
	int button = 0;
	power_profile::window window{};

	for (int index = 0; index < 200; ++index)
	{
		profile.record_task_switch(&console, "console");
	}
	for (int index = 0; index < 100; ++index)
	{
		profile.record_task_switch(&button, "button");
	}
	for (int index = 0; index < 30; ++index)
	{
		profile.record_sleep();
		profile.record_wake(15);
	}
	profile.record_wake(53);
	profile.record_wake(500);
	profile.record_slept_cycles(108'000'000);
	profile.take_window(2000, window);

	assert(window.duration_ms == 2000);
	assert(window.task_count == 2);
	assert(std::strcmp(window.tasks[0].name.data(), "console") == 0);
	assert(window.tasks[0].switches == 200);
	assert(window.tasks[1].switches == 100);
	assert(window.sleeps == 30);
	assert(window.wake_source_count == 3);
	assert(window.wake_sources[0].exception == 15);
	assert(window.wake_sources[0].count == 30);
	assert(rate_tenths_per_second(window.tasks[0].switches, window.duration_ms) == 1000);
	assert(awake_permille(window.slept_cycles, window.duration_ms, 72'000'000) == 250);

	profile.record_task_switch(&button, "button");
	profile.take_window(3000, window);
	assert(window.duration_ms == 1000);
	assert(window.task_count == 1);
	assert(std::strcmp(window.tasks[0].name.data(), "button") == 0);
	assert(window.sleeps == 0);
	assert(window.wake_source_count == 0);
	assert(window.slept_cycles == 0);

	int tasks[power_profile::task_capacity + 1]{};
	for (auto &task : tasks)
	{
		profile.record_task_switch(&task, "a_very_long_task_name_beyond_capacity");
	}
	profile.take_window(4000, window);
	assert(window.untracked_switches == 3);
	assert(window.tasks[0].name.back() == '\0');

	for (std::uint16_t exception = 16; exception < 16 + power_profile::reported_wake_sources + 2; ++exception)
	{
		for (std::uint16_t count = 0; count <= exception; ++count)
		{
			profile.record_wake(exception);
		}
	}
	profile.take_window(5000, window);
	assert(window.wake_source_count == power_profile::reported_wake_sources);
	assert(window.wake_sources[0].exception == 16 + power_profile::reported_wake_sources + 1);

	assert(rate_tenths_per_second(5, 0) == 0);
	assert(awake_permille(100, 0, 72'000'000) == 0);
	assert(awake_permille(0, 1000, 72'000'000) == 1000);
	assert(awake_permille(1'000'000'000, 1000, 72'000'000) == 0);
	return 0;
}
