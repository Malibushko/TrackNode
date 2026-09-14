#pragma once

#include "core/hardware_object.h"
#include "drivers/uart.h"
#include "drivers/pwm.h"
#include "drivers/adc.h"
#include "platform/error.h"

#include <algorithm>
#include <unordered_map>
#include <vector>
#include <variant>
#include <string_view>
#include <string>

class board : public hardware_object
{
public:
	using component_t = std::variant<uart, timer, pwm, adc, gpio>;

	board() = default;

	status init();

	template <typename T, typename... Args> board &add(std::string_view name, Args &&...args)
	{
		std::string key{ name };
		components_.emplace(std::piecewise_construct, std::forward_as_tuple(key),
		                    std::forward_as_tuple(std::in_place_type<T>, std::forward<Args>(args)...));
		insertion_order_.push_back(std::move(key));
		return *this;
	}

	template <typename T> T &get(std::string_view name)
	{
		return std::get<T>(components_.at(std::string{ name }));
	}

	template <typename T> const T &get(std::string_view name) const
	{
		return std::get<T>(components_.at(std::string{ name }));
	}

	component_t &at(std::string_view name)
	{
		return components_.at(std::string{ name });
	}

	const component_t &at(std::string_view name) const
	{
		return components_.at(std::string{ name });
	}

private:
	std::unordered_map<std::string, component_t> components_;
	std::vector<std::string> insertion_order_;
	bool attempted_ = false;
};
