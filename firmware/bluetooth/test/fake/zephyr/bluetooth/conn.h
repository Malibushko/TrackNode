#pragma once
#include <zephyr/bluetooth/bluetooth.h>
#include <cstdlib>
#include <zephyr/kernel.h>
using bt_security_t = unsigned;
enum bt_security_err { BT_SECURITY_ERR_SUCCESS, BT_SECURITY_ERR_PAIR_NOT_ALLOWED };
inline constexpr bt_security_t BT_SECURITY_L2 = 2;
struct bt_conn_pairing_feat { std::uint8_t auth_req = 1; };
struct bt_addr_le_t { unsigned value = 1; };
inline int bt_addr_le_cmp(const bt_addr_le_t *a, const bt_addr_le_t *b) { return a->value != b->value; }
struct bt_bond_info { bt_addr_le_t addr; };
inline constexpr int BT_ID_DEFAULT = 0;
inline bool fake_known_peer = true;
inline bool fake_restore_bond_security = true;
inline unsigned fake_bond_count = 1;
struct bt_conn
{
	bt_addr_le_t peer{};
	bt_security_t security = BT_SECURITY_L2;
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
	void (*security_changed)(bt_conn *, bt_security_t, bt_security_err) = nullptr;
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

struct bt_conn_auth_cb
{
	bt_security_err (*pairing_accept)(bt_conn *, const bt_conn_pairing_feat *) = nullptr;
};
struct bt_conn_auth_info_cb
{
	void (*pairing_complete)(bt_conn *, bool) = nullptr;
	void (*pairing_failed)(bt_conn *, bt_security_err) = nullptr;
};
inline bt_conn_auth_cb *registered_auth = nullptr;
inline bt_conn_auth_info_cb *registered_auth_info = nullptr;
inline int bt_conn_auth_cb_register(bt_conn_auth_cb *cb) { registered_auth = cb; return 0; }
inline int bt_conn_auth_info_cb_register(bt_conn_auth_info_cb *cb) { registered_auth_info = cb; return 0; }
inline int bt_conn_auth_info_cb_unregister(bt_conn_auth_info_cb *) { registered_auth_info = nullptr; return 0; }
inline int bt_conn_auth_cancel(bt_conn *) { return 0; }
inline const bt_addr_le_t *bt_conn_get_dst(bt_conn *conn) { return &conn->peer; }
inline bt_security_t bt_conn_get_security(bt_conn *conn) { return conn->security; }
inline unsigned bt_conn_enc_key_size(bt_conn *) { return 16; }
inline int bt_conn_set_security(bt_conn *conn, bt_security_t level)
{
	if (fake_known_peer && fake_restore_bond_security && registered_callback->security_changed)
	{
		conn->security = level;
		registered_callback->security_changed(conn, level, BT_SECURITY_ERR_SUCCESS);
	}
	return 0;
}
inline void bt_foreach_bond(unsigned, void (*visit)(const bt_bond_info *, void *), void *context)
{
	for (unsigned i = 0; i < fake_bond_count; ++i)
	{
		bt_bond_info bond{{fake_known_peer && i == 0 ? 1u : i + 20u}};
		visit(&bond, context);
	}
}
inline unsigned fake_unpairs = 0;
inline int bt_unpair(unsigned, const bt_addr_le_t *) { ++fake_unpairs; return 0; }
inline void bt_set_bondable(bool) {}
