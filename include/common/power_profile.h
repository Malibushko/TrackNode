#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class power_profile
{
public:
	static constexpr std::size_t task_capacity = 12;
	static constexpr std::size_t name_capacity = 16;
	static constexpr std::size_t exception_capacity = 128;
	static constexpr std::size_t reported_wake_sources = 6;

	struct task_activity
	{
		std::array<char, name_capacity> name{};
		std::uint32_t switches = 0;
	};

	struct wake_source
	{
		std::uint16_t exception = 0;
		std::uint32_t count = 0;
	};

	struct window
	{
		std::uint32_t duration_ms = 0;
		std::uint64_t slept_cycles = 0;
		std::uint32_t sleeps = 0;
		std::uint32_t stops = 0;
		std::uint32_t untracked_switches = 0;
		std::array<task_activity, task_capacity> tasks{};
		std::size_t task_count = 0;
		std::array<wake_source, reported_wake_sources> wake_sources{};
		std::size_t wake_source_count = 0;
	};

public: // Event recording
	void record_task_switch(const void *task, const char *name);
	void record_sleep();
	void record_stop();
	void record_wake(std::uint16_t exception);
	void record_slept_cycles(std::uint32_t cycles);

public: // Reporting
	void take_window(std::uint32_t now_ms, window &out);

private:
	struct task_slot
	{
		const void *task = nullptr;
		std::array<char, name_capacity> name{};
		std::uint32_t switches = 0;
	};

	task_slot *find_or_add_task(const void *task, const char *name);
	void collect_tasks(window &out) const;
	void collect_wake_sources(window &out) const;
	void reset_counters(std::uint32_t now_ms);

private:
	std::array<task_slot, task_capacity> tasks_{};
	std::size_t task_count_ = 0;
	std::uint32_t untracked_switches_ = 0;
	std::array<std::uint32_t, exception_capacity> wakes_{};
	std::uint64_t slept_cycles_ = 0;
	std::uint32_t sleeps_ = 0;
	std::uint32_t stops_ = 0;
	std::uint32_t window_started_ms_ = 0;
};

std::uint32_t rate_tenths_per_second(std::uint32_t count, std::uint32_t duration_ms);
std::uint32_t awake_permille(std::uint64_t slept_cycles, std::uint32_t duration_ms, std::uint32_t core_clock_hz);
