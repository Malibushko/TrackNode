#pragma once

#include <cstddef>
#include <cstring>
#include <functional>
#include <sys/types.h>
#include <vector>
#include <map>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>

struct bt_gatt_attr;
using fake_write_callback = ssize_t (*)(bt_conn *, const bt_gatt_attr *, const void *, std::uint16_t, std::uint16_t, std::uint8_t);
using fake_ccc_callback = void (*)(const bt_gatt_attr *, std::uint16_t);
using fake_read_callback = ssize_t (*)(bt_conn *, const bt_gatt_attr *, void *, std::uint16_t, std::uint16_t);

struct bt_gatt_attr
{
	const bt_uuid *uuid = nullptr;
	void *user_data = nullptr;
	fake_write_callback write = nullptr;
	fake_ccc_callback changed = nullptr;
	fake_read_callback read = nullptr;
};

struct bt_gatt_service
{
	bt_gatt_attr *attrs;
	std::size_t attr_count;
};

struct bt_gatt_cb
{
	void (*att_mtu_updated)(bt_conn *, std::uint16_t, std::uint16_t);
	int node;
};

inline bt_gatt_cb *registered_gatt_callback = nullptr;
inline bt_gatt_service *fake_gatt_service = nullptr;
inline std::vector<bt_gatt_service *> fake_gatt_services;
inline std::map<const bt_gatt_attr *, bool> subscriptions;
inline std::vector<std::pair<const bt_gatt_attr *, std::vector<std::uint8_t>>> notifications;
inline bt_uuid fake_declaration_uuid{0};
inline int notify_error = 0;
inline int notification_count = 0;
inline std::vector<std::uint8_t> notification_data;
inline std::function<void()> notification_hook;

inline constexpr int BT_ATT_ERR_WRITE_NOT_PERMITTED = 0x03;
inline constexpr int BT_ATT_ERR_NOT_SUPPORTED = 0x06;
inline constexpr int BT_ATT_ERR_INVALID_OFFSET = 0x07;
inline constexpr int BT_ATT_ERR_AUTHORIZATION = 0x08;
inline constexpr int BT_ATT_ERR_INVALID_ATTRIBUTE_LEN = 0x0d;
inline constexpr int BT_ATT_ERR_UNLIKELY = 0x0e;
inline constexpr int BT_ATT_ERR_INSUFFICIENT_RESOURCES = 0x11;
#define BT_GATT_ERR(error) (-error)

inline constexpr int BT_GATT_CHRC_WRITE = 8;
inline constexpr int BT_GATT_CHRC_READ = 2;
inline constexpr int BT_GATT_CHRC_NOTIFY = 16;
inline constexpr int BT_GATT_PERM_NONE = 0;
inline constexpr int BT_GATT_PERM_READ = 1;
inline constexpr int BT_GATT_PERM_WRITE = 2;
inline constexpr int BT_GATT_CCC_NOTIFY = 1;
inline constexpr int BT_GATT_WRITE_FLAG_PREPARE = 1;
inline constexpr int BT_GATT_WRITE_FLAG_CMD = 2;
inline constexpr int BT_GATT_WRITE_FLAG_EXECUTE = 4;

#define BT_GATT_PRIMARY_SERVICE(uuid) bt_gatt_attr{uuid}
#define BT_GATT_CHARACTERISTIC(uuid, props, perm, read, write_callback, context) 	bt_gatt_attr{&fake_declaration_uuid}, bt_gatt_attr{uuid, context, write_callback, nullptr, read}
#define BT_GATT_CCC(changed_callback, perm) bt_gatt_attr{nullptr, nullptr, nullptr, changed_callback}
#define BT_GATT_SERVICE_DEFINE(name, ...) 	static bt_gatt_attr name##_attributes[] = {__VA_ARGS__}; 	static bt_gatt_service name{name##_attributes, sizeof(name##_attributes) / sizeof(bt_gatt_attr)}; 	[[maybe_unused]] static bool name##_registered = (fake_gatt_services.push_back(&name), fake_gatt_service = &name, true)

inline const bt_gatt_attr *bt_gatt_find_by_uuid(const bt_gatt_attr *attributes, std::uint16_t count, const bt_uuid *uuid)
{
	for (std::uint16_t index = 0; index < count; ++index)
	{
		const auto *candidate = attributes[index].uuid;
		if (candidate != nullptr && ((candidate->type == 1 && uuid->type == 1 && reinterpret_cast<const bt_uuid_16 *>(candidate)->val == reinterpret_cast<const bt_uuid_16 *>(uuid)->val) || (candidate->type == 2 && uuid->type == 2 &&
			std::memcmp(reinterpret_cast<const bt_uuid_128 *>(candidate)->val,
				reinterpret_cast<const bt_uuid_128 *>(uuid)->val, 16) == 0)))
		{
			return &attributes[index];
		}
	}

	return nullptr;
}

inline void bt_gatt_cb_register(bt_gatt_cb *callback)
{
	registered_gatt_callback = callback;
}

inline ssize_t bt_gatt_attr_read(bt_conn *, const bt_gatt_attr *, void *buffer,
	std::uint16_t length, std::uint16_t offset, const void *value, std::uint16_t size)
{
	if (offset > size)
	{
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	const auto count = std::min<std::uint16_t>(length, size - offset);
	std::memcpy(buffer, static_cast<const std::uint8_t *>(value) + offset, count);
	return count;
}

inline std::uint16_t bt_gatt_get_mtu(bt_conn *connection)
{
	if (spinlock_depth != 0)
	{
		std::abort();
	}
	return connection->mtu;
}

inline bool bt_gatt_is_subscribed(bt_conn *connection, const bt_gatt_attr *attribute, std::uint16_t)
{
	if (spinlock_depth != 0)
	{
		std::abort();
	}
	return subscriptions.contains(attribute) ? subscriptions.at(attribute) : (attribute->read == nullptr && connection->subscribed);
}

inline int bt_gatt_notify(bt_conn *, const bt_gatt_attr *attribute, const void *data, std::uint16_t size)
{
	if (spinlock_depth != 0)
	{
		std::abort();
	}
	if (notification_hook)
	{
		notification_hook();
	}
	if (notify_error != 0)
	{
		return notify_error;
	}
	notification_data.resize(size);
	if (size != 0)
	{
		std::memcpy(notification_data.data(), data, size);
	}
	notifications.emplace_back(attribute, notification_data);
 ++notification_count;
	return 0;
}

inline constexpr int BT_GATT_PERM_READ_ENCRYPT = 4;
inline constexpr int BT_GATT_PERM_WRITE_ENCRYPT = 8;
