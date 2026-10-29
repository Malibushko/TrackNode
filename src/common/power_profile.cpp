#include "common/power_profile.h"

#include <algorithm>

namespace
{
void copy_name(std::array<char, power_profile::name_capacity> &target, const char *source)
{
	std::size_t index = 0;
	for (; source && source[index] != '\0' && index + 1 < target.size(); ++index)
	{
		target[index] = source[index];
	}
	target[index] = '\0';
}

//////////////////////////////////////////////////////////////////////////
}

//////////////////////////////////////////////////////////////////////////

void power_profile::record_task_switch(const void *task, const char *name)
{
	auto *slot = find_or_add_task(task, name);
	if (!slot)
	{
		++untracked_switches_;
		return;
	}

	++slot->switches;
}

//////////////////////////////////////////////////////////////////////////

void power_profile::record_sleep()
{
	++sleeps_;
}

//////////////////////////////////////////////////////////////////////////

void power_profile::record_wake(std::uint16_t exception)
{
	const auto known_exception = exception < wakes_.size();
	++wakes_[known_exception ? exception : 0];
}

//////////////////////////////////////////////////////////////////////////

void power_profile::record_slept_cycles(std::uint32_t cycles)
{
	slept_cycles_ += cycles;
}

//////////////////////////////////////////////////////////////////////////

void power_profile::take_window(std::uint32_t now_ms, window &out)
{
	out = {};
	out.duration_ms = now_ms - window_started_ms_;
	out.slept_cycles = slept_cycles_;
	out.sleeps = sleeps_;
	out.untracked_switches = untracked_switches_;
	collect_tasks(out);
	collect_wake_sources(out);
	reset_counters(now_ms);
}

//////////////////////////////////////////////////////////////////////////

power_profile::task_slot *power_profile::find_or_add_task(const void *task, const char *name)
{
	for (std::size_t index = 0; index < task_count_; ++index)
	{
		if (tasks_[index].task == task)
		{
			return &tasks_[index];
		}
	}
	if (task_count_ == tasks_.size())
	{
		return nullptr;
	}

	auto &slot = tasks_[task_count_++];
	slot.task = task;
	copy_name(slot.name, name);
	return &slot;
}

//////////////////////////////////////////////////////////////////////////

void power_profile::collect_tasks(window &out) const
{
	for (std::size_t index = 0; index < task_count_; ++index)
	{
		const auto &slot = tasks_[index];
		if (slot.switches == 0)
		{
			continue;
		}
		out.tasks[out.task_count++] = task_activity{
			.name = slot.name,
			.switches = slot.switches
		};
	}

	std::sort(out.tasks.begin(), out.tasks.begin() + out.task_count, [](const auto &left, const auto &right) {
		return left.switches > right.switches;
	});
}

//////////////////////////////////////////////////////////////////////////

void power_profile::collect_wake_sources(window &out) const
{
	for (std::size_t exception = 0; exception < wakes_.size(); ++exception)
	{
		const auto count = wakes_[exception];
		if (count == 0)
		{
			continue;
		}
		const wake_source candidate{
			.exception = static_cast<std::uint16_t>(exception),
			.count = count
		};
		if (out.wake_source_count < out.wake_sources.size())
		{
			out.wake_sources[out.wake_source_count++] = candidate;
			continue;
		}
		auto *weakest = std::min_element(out.wake_sources.begin(), out.wake_sources.end(), [](const auto &left, const auto &right) {
			return left.count < right.count;
		});
		if (weakest->count < candidate.count)
		{
			*weakest = candidate;
		}
	}

	std::sort(out.wake_sources.begin(), out.wake_sources.begin() + out.wake_source_count, [](const auto &left, const auto &right) {
		return left.count > right.count;
	});
}

//////////////////////////////////////////////////////////////////////////

void power_profile::reset_counters(std::uint32_t now_ms)
{
	for (std::size_t index = 0; index < task_count_; ++index)
	{
		tasks_[index].switches = 0;
	}
	untracked_switches_ = 0;
	wakes_.fill(0);
	slept_cycles_ = 0;
	sleeps_ = 0;
	window_started_ms_ = now_ms;
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t rate_tenths_per_second(std::uint32_t count, std::uint32_t duration_ms)
{
	if (duration_ms == 0)
	{
		return 0;
	}

	return static_cast<std::uint32_t>(static_cast<std::uint64_t>(count) * 10000u / duration_ms);
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t awake_permille(std::uint64_t slept_cycles, std::uint32_t duration_ms, std::uint32_t core_clock_hz)
{
	const auto window_cycles = static_cast<std::uint64_t>(duration_ms) * core_clock_hz / 1000u;
	if (window_cycles == 0)
	{
		return 0;
	}

	const auto slept_permille = std::min<std::uint64_t>(slept_cycles * 1000u / window_cycles, 1000u);
	return static_cast<std::uint32_t>(1000u - slept_permille);
}

//////////////////////////////////////////////////////////////////////////
