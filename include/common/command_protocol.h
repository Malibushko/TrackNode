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

	bool process_next_byte()
	{
		const uint8_t byte = input_.read();

		if (auto cmd = derived().decode(byte))
		{
			if (!decoded_commands_.full()) {
				decoded_commands_.push(std::move(*cmd));
			}
			return true;
		}

		return false;
	}

	bool feed(uint8_t byte)
	{
		if (auto cmd = derived().decode(byte))
		{
			if (!decoded_commands_.full()) {
				decoded_commands_.push(std::move(*cmd));
			}
			return true;
		}
		return false;
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
