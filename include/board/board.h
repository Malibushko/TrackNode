#pragma once

#include "core/hardware_object.h"
#include "drivers/uart.h"
#include "drivers/pwm.h"
#include "drivers/adc.h"
#include "platform/error.h"

#include <algorithm>
#include <variant>
#include <string_view>
#include <string>

class board : public hardware_object
{
public:
	using component_t = std::variant<uart, timer, pwm, adc, gpio>;
	using entry_t = std::pair<std::string, component_t>;

	board() = default;

	status init();

	template <typename T, typename... Args>
	board &add(std::string_view name, Args &&...args)
	{
		components_.emplace_back(std::string{name}, component_t{std::in_place_type<T>, std::forward<Args>(args)...});
		return *this;
	}

	template <typename T>
	T &get(std::string_view name)
	{
		return std::get<T>(find(name));
	}

	template <typename T>
	const T &get(std::string_view name) const
	{
		return std::get<T>(find(name));
	}

	[[nodiscard]] component_t &at(std::string_view name)
	{
		return find(name);
	}

	[[nodiscard]] const component_t &at(std::string_view name) const
	{
		return find(name);
	}

private:

	component_t & find(std::string_view name)
	{
		const auto it = std::ranges::find_if(components_, [&](const entry_t &entry) { return entry.first == name; });
		if (it == components_.end())
		{
			error_handler();
		}

		return it->second;
	}

	const component_t &find(std::string_view name) const
	{
		const auto it = std::ranges::find_if(components_, [&](const entry_t &entry) { return entry.first == name; });
		if (it == components_.end())
		{
			error_handler();
		}
		return it->second;
	}

	std::vector<entry_t> components_;
	bool attempted_ = false;
};