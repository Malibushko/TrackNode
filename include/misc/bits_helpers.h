#pragma once
#include <type_traits>

template <class T, class U>
constexpr T from_bits(U value, int first_bit, int last_bit)
{
	static_assert(std::is_unsigned_v<U>, "value must be unsigned");

	int step = (last_bit >= first_bit) ? 1 : -1;
	unsigned result  = 0;
	unsigned out_pos = 0;

	for (int bit = first_bit; ; bit += step)
	{
		unsigned bit_val = (static_cast<unsigned>(value) >> bit) & 1u;
		result |= (bit_val << out_pos);
		++out_pos;

		if (bit == last_bit)
			break;
	}

	return static_cast<T>(result);
}