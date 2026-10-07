#pragma once
#include <zephyr/bluetooth/bluetooth.h>
#include <cstdlib>
#include <zephyr/kernel.h>
struct bt_conn
{
	int references = 1;
	int disconnects = 0;
	std::uint16_t mtu = 23;
	bool subscribed = false;
};
inline bt_conn *bt_conn_ref(bt_conn *connection)
{
	++connection->references;
	return connection;
}
inline void bt_conn_unref(bt_conn *connection)
{
	--connection->references;
	if (connection->references < 1 || spinlock_depth != 0)
	{
		std::abort();
	}
}
inline int bt_conn_disconnect(bt_conn *connection, std::uint8_t)
{
	++connection->disconnects;
	return 0;
}
struct bt_conn_cb
{
	void (*connected)(bt_conn *, std::uint8_t) = nullptr;
	void (*disconnected)(bt_conn *, std::uint8_t) = nullptr;
	void (*recycled)() = nullptr;
	void *le_param_req = nullptr;
	void *le_param_updated = nullptr;
	int _node = 0;
};
inline bt_conn_cb *registered_callback = nullptr;
inline int registration_error = 0;
inline int bt_conn_cb_register(bt_conn_cb *callback)
{
	if (registration_error != 0)
	{
		return registration_error;
	}
	if (registered_callback == callback)
	{
		return -EEXIST;
	}
	registered_callback = callback;
	return 0;
}
inline int bt_conn_cb_unregister(bt_conn_cb *callback)
{
	if (registered_callback != callback)
	{
		return -ENOENT;
	}
	registered_callback = nullptr;
	return 0;
}
