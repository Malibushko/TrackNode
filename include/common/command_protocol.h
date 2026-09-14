#pragma once
#include <concepts>
#include <cstdint>
#include <optional>
#include <queue>

template<class T>
concept ByteInput = requires(T& input) {
	{ input.read() }      -> std::same_as<std::uint8_t>;
	{ input.available() } -> std::integral;
};

template<
	ByteInput Input,
	class Command,
	class Derived
>
class command_protocol
{
public:
	explicit command_protocol(Input& input)
		: input_(input)
	{}

	bool consume()
	{
		bool decoded = false;

		while (input_.available() > 0)
		{
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
		auto result = std::move(decoded_commands_.front());
		decoded_commands_.pop();
		return result;
	}

protected:
	Derived& derived() noexcept
	{
		return static_cast<Derived&>(*this);
	}

private:
	Input& input_;
	std::queue<Command> decoded_commands_;
};