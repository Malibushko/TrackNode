#pragma once

#include <cstdint>

struct bt_uuid
{
	std::uint8_t type;
};

struct bt_uuid_128
{
	bt_uuid uuid;
	std::uint8_t val[16];
};

#define BT_UUID_128_ENCODE(w32, w1, w2, w3, w48) \
	static_cast<std::uint8_t>(w32), static_cast<std::uint8_t>((w32) >> 8), \
	static_cast<std::uint8_t>((w32) >> 16), static_cast<std::uint8_t>((w32) >> 24), \
	static_cast<std::uint8_t>(w1), static_cast<std::uint8_t>((w1) >> 8), \
	static_cast<std::uint8_t>(w2), static_cast<std::uint8_t>((w2) >> 8), \
	static_cast<std::uint8_t>(w3), static_cast<std::uint8_t>((w3) >> 8), \
	static_cast<std::uint8_t>(w48), static_cast<std::uint8_t>((w48) >> 8), \
	static_cast<std::uint8_t>((w48) >> 16), static_cast<std::uint8_t>((w48) >> 24), \
	static_cast<std::uint8_t>((w48) >> 32), static_cast<std::uint8_t>((w48) >> 40)

#define BT_UUID_INIT_128(...) {{2}, {__VA_ARGS__}}
