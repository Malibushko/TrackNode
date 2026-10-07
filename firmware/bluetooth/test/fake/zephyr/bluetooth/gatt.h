#pragma once

#include <cstddef>
#include <sys/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>

struct bt_gatt_attr
{
	const bt_uuid *uuid;
	void *user_data;
};

struct bt_gatt_chrc
{
	const bt_uuid *uuid;
	std::uint16_t value_handle;
	std::uint8_t properties;
};

struct bt_gatt_ccc_managed_user_data
{
	std::uint16_t value;
};

struct bt_gatt_service
{
	bt_gatt_attr *attrs;
	std::size_t attr_count;
};

inline constexpr int BT_ATT_ERR_NOT_SUPPORTED = 0x06;
#define BT_GATT_ERR(error) (-error)
