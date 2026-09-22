#pragma once

#include <type_traits>
#include <utility>

namespace common
{
	template <typename callback_type>
	class scope_guard
	{
	public:
		explicit scope_guard(callback_type callback)
			noexcept(std::is_nothrow_move_constructible_v<callback_type>)
			: callback_(std::move(callback))
		{
		}

		~scope_guard() noexcept
		{
			if (active_)
			{
				callback_();
			}
		}

		scope_guard(const scope_guard&) = delete;
		scope_guard& operator=(const scope_guard&) = delete;

		scope_guard(scope_guard&& other)
			noexcept(std::is_nothrow_move_constructible_v<callback_type>)
			: callback_(std::move(other.callback_)),
			  active_(std::exchange(other.active_, false))
		{
		}

		scope_guard& operator=(scope_guard&&) = delete;

		void release() noexcept
		{
			active_ = false;
		}

	private:
		callback_type callback_;
		bool active_ = true;
	};

	template <typename callback_type>
	scope_guard(callback_type) -> scope_guard<std::decay_t<callback_type>>;
}

#define TRACK_NODE_CONCAT_IMPL(left, right) left##right
#define TRACK_NODE_CONCAT(left, right) TRACK_NODE_CONCAT_IMPL(left, right)

#define BLOCK_GUARD(...) \
	[[maybe_unused]] ::common::scope_guard TRACK_NODE_CONCAT(scope_guard_, __COUNTER__){ __VA_ARGS__ }
