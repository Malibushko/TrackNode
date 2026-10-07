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

inline constexpr int BT_GATT_CHRC_WRITE = 8;
inline constexpr int BT_GATT_CHRC_NOTIFY = 16;
inline constexpr int BT_GATT_PERM_NONE = 0;
inline constexpr int BT_GATT_PERM_READ = 1;
inline constexpr int BT_GATT_PERM_WRITE = 2;

#define BT_GATT_PRIMARY_SERVICE(uuid) bt_gatt_attr{uuid, nullptr}
#define BT_GATT_CHARACTERISTIC(uuid, props, perm, read, write, context) \
	bt_gatt_attr{uuid, nullptr}, bt_gatt_attr{uuid, context}
#define BT_GATT_CCC(changed, perm) bt_gatt_attr{}
#define BT_GATT_SERVICE_DEFINE(name, ...) \
	[[maybe_unused]] static bt_gatt_attr name[] = {__VA_ARGS__}
