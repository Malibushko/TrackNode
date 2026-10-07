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
