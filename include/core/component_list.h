#pragma once

#include "core/result.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

enum class component_startup
{
	initialize,
	defer
};

// Non-owning registry used only while composing and starting the application.
// Registered objects and their names must outlive the list.
class component_list
{
public:
	static constexpr std::size_t capacity = 8;
	component_list() = default;
	component_list(const component_list &) = delete;
	component_list &operator=(const component_list &) = delete;
	component_list(component_list &&) = delete;
	component_list &operator=(component_list &&) = delete;

	template <typename device_t>
	status add(std::string_view name, device_t &device, component_startup startup)
	{
		if (attempted_)
		{
			return fail(component::board, error_code::already_initialized);
		}
		if (name.empty())
		{
			return fail(component::board, error_code::invalid_config);
		}
		for (std::size_t i = 0; i < count_; ++i)
		{
			if (entries_[i].name == name || entries_[i].object == std::addressof(device))
			{
				return fail(component::board, error_code::invalid_config);
			}
		}
		if (count_ == capacity)
		{
			return fail(component::board, error_code::out_of_memory);
		}
		entries_[count_++] = { name, std::addressof(device), type_key<device_t>(),
		                       [](void *object) -> status {
			                       return static_cast<device_t *>(object)->init();
		                       }, startup };
		return {};
	}

	template <typename device_t>
	[[nodiscard]] device_t *get(std::string_view name) const
	{
		for (std::size_t i = 0; i < count_; ++i)
		{
			if (entries_[i].name == name)
			{
				return entries_[i].type == type_key<device_t>()
				           ? static_cast<device_t *>(entries_[i].object) : nullptr;
			}
		}
		return nullptr;
	}

	status init()
	{
		if (attempted_)
		{
			return fail(component::board, error_code::already_initialized);
		}
		attempted_ = true;
		for (std::size_t i = 0; i < count_; ++i)
		{
			if (entries_[i].startup == component_startup::initialize)
			{
				if (auto started = entries_[i].init(entries_[i].object); !started)
				{
					return started;
				}
			}
		}
		return {};
	}

private:
	template <typename device_t>
	static const void *type_key()
	{
		static char token;
		return &token;
	}

	struct entry
	{
		std::string_view name{};
		void *object = nullptr;
		const void *type = nullptr;
		status (*init)(void *) = nullptr;
		component_startup startup = component_startup::defer;
	};

	std::array<entry, capacity> entries_{};
	std::size_t count_ = 0;
	bool attempted_ = false;
};
