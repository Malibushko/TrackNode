#pragma once

#include <memory>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>

namespace ble
{
struct connection_unref
{
	void operator()(bt_conn *connection) const
	{
		bt_conn_unref(connection);
	}
};

using connection_ref = std::unique_ptr<bt_conn, connection_unref>;

inline connection_ref retain_connection(bt_conn *connection)
{
	return connection_ref{connection != nullptr ? bt_conn_ref(connection) : nullptr};
}

class spin_lock_guard
{
public:
	explicit spin_lock_guard(k_spinlock &lock)
		: lock_(lock), key_(k_spin_lock(&lock_)) {}

	~spin_lock_guard()
	{
		k_spin_unlock(&lock_, key_);
	}

	spin_lock_guard(const spin_lock_guard &) = delete;
	spin_lock_guard &operator=(const spin_lock_guard &) = delete;

private:
	k_spinlock &lock_;
	k_spinlock_key_t key_;
};
}
