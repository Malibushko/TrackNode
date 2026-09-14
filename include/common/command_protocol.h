#pragma once
#include <concepts>
#include <cstdint>
#include <optional>
#include "common/ring_buffer.h"
#include <cstddef>

template <class T>
concept ByteInput = requires(T &input) {
	{
		input.read()
	} -> std::same_as<std::uint8_t>;
	{
		input.available()
	} -> std::integral;
};

template <ByteInput Input, class Command, class Derived> class command_protocol
{
public:
	explicit command_protocol(Input &input) : input_(input)
	{
	}

	bool consume(size_t byte_budget = 32)
	{
		bool decoded = false;

		while (byte_budget != 0 && !decoded_commands_.full() && input_.available() > 0)
		{
			--byte_budget;
			const auto byte = input_.read();

			if (auto cmd = derived().decode(byte))
			{
				decoded_commands_.push(std::move(*cmd));
				decoded = true;
			}
		}

		return decoded;
	}

	[[nodiscard]]
	bool available() const noexcept
	{
		return !decoded_commands_.empty();
	}

	Command read()
	{
		Command decoded{};
		decoded_commands_.pop(decoded);
		return decoded;
	}

protected:
	Derived &derived() noexcept
	{
		return static_cast<Derived &>(*this);
	}

private:
	Input &input_;
	ring_buffer<Command, 8> decoded_commands_;
};
