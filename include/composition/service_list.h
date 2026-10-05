#pragma once

#include "infrastructure/service_status.h"
#include <initializer_list>

// Composition-time registry. Services are owned by main and outlive this list.
class service_list
{
public:
	template<class Service>
	status add(service_id id, Service &service, std::initializer_list<service_id> dependencies = {})
	{
		const auto i = static_cast<std::size_t>(id);
		if (frozen_ || i >= capacity || entries_[i].object)
			return fail(component::service, error_code::invalid_config);
		std::uint32_t required = 0;
		for (auto dependency : dependencies)
		{
			if (static_cast<std::size_t>(dependency) >= capacity || dependency == id)
				return fail(component::service, error_code::invalid_config);
			required |= bit(dependency);
		}
		for (const auto &entry : entries_)
			if (entry.object == &service) return fail(component::service, error_code::invalid_config);
		entries_[i] = { &service, required,
			[](void *object) { return static_cast<Service *>(object)->start(); },
			[](void *object) { static_cast<Service *>(object)->stop(); } };
		mask_ |= bit(id);
		return {};
	}

	status prepare()
	{
		if (frozen_) return fail(component::service, error_code::already_initialized);
		frozen_ = true;
		std::uint32_t selected = 0;
		while (selected != mask_)
		{
			const auto before = selected;
			for (std::size_t i = 0; i < capacity; ++i)
			{
				const auto flag = std::uint32_t{1} << i;
				if ((mask_ & flag) && !(selected & flag) && !(entries_[i].dependencies & ~selected))
				{
					order_[ordered_++] = i;
					selected |= flag;
				}
			}
			if (before == selected) return fail(component::service, error_code::invalid_config);
		}
		prepared_ = true;
		return {};
	}

	status start()
	{
		if (!prepared_) return fail(component::service, error_code::not_initialized);
		if (attempted_) return fail(component::service, error_code::already_initialized);
		attempted_ = true;
		for (auto i : order_)
		{
			if (started_ == ordered_) break;
			auto &entry = entries_[i];
			if (auto result = entry.start(entry.object); !result)
			{
				entry.stop(entry.object); // Also clean up a partially started service.
				stop();
				return result;
			}
			++started_;
		}
		return {};
	}

	void stop()
	{
		while (started_)
		{
			auto &entry = entries_[order_[--started_]];
			entry.stop(entry.object);
		}
	}

	[[nodiscard]] std::uint32_t mask() const { return mask_; }
	static constexpr std::uint32_t bit(service_id id) { return std::uint32_t{1} << static_cast<unsigned>(id); }

private:
	static constexpr std::size_t capacity = static_cast<std::size_t>(service_id::count);
	static_assert(capacity <= 32);
	struct entry
	{
		void *object = nullptr;
		std::uint32_t dependencies = 0;
		status (*start)(void *) = nullptr;
		void (*stop)(void *) = nullptr;
	};
	std::array<entry, capacity> entries_{};
	std::array<std::size_t, capacity> order_{};
	std::uint32_t mask_ = 0;
	std::size_t ordered_ = 0;
	std::size_t started_ = 0;
	bool frozen_ = false;
	bool prepared_ = false;
	bool attempted_ = false;
};
