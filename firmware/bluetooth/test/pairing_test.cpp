#include "drivers/devices/zephyr_bluetooth.h"
#include "drivers/protocols/gatt_uuid.h"
#include <stdexcept>

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("Pairing policy test failed");
	}
}

int main()
{
	ble::zephyr_bluetooth bluetooth{{.name = "TrackNode"}};
	require(bluetooth.init().has_value());
	const auto *status = bt_gatt_find_by_uuid(fake_gatt_service->attrs, fake_gatt_service->attr_count,
		&ble::gatt_uuid::measurement_status.uuid);
	std::uint8_t value[20]{};
	bt_conn unknown;
	fake_known_peer = false;
	registered_callback->connected(&unknown, 0);
	require(unknown.disconnects == 1);
	require(!bluetooth.authorized(&unknown));
	require(status->read(&unknown, status, value, sizeof(value), 0) == BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION));

	fake_known_peer = true;
	bt_conn old;
	registered_callback->connected(&old, 0);
	require(bluetooth.poll(0)->link.connected);
	require(bluetooth.authorized(&old));
	require(bluetooth.open_pairing_window().has_value());
	require(old.disconnects == 1 && !bluetooth.authorized(&old));
	registered_callback->security_changed(&old, BT_SECURITY_L2, BT_SECURITY_ERR_SUCCESS);
	require(!bluetooth.authorized(&old));
	registered_callback->disconnected(&old, 0x13);
	while (bluetooth.poll(0)->type != ble::bluetooth_event::kind::idle) {}
	bt_conn reconnect;
	registered_callback->connected(&reconnect, 0);
	require(reconnect.disconnects == 1);
	require(bluetooth.pairing_active());

	fake_known_peer = false;
	bt_conn added;
	registered_callback->connected(&added, 0);
	require(!bluetooth.authorized(&added));
	bt_conn_pairing_feat features;
	features.auth_req = 0;
	require(registered_auth->pairing_accept(&added, &features) == BT_SECURITY_ERR_PAIR_NOT_ALLOWED);
	features.auth_req = 1;
	fake_bond_count = 8;
	require(registered_auth->pairing_accept(&added, &features) == BT_SECURITY_ERR_SUCCESS);
	fake_bond_count = 1;
	require(registered_auth->pairing_accept(&added, &features) == BT_SECURITY_ERR_SUCCESS);
	registered_callback->security_changed(&added, BT_SECURITY_L2, BT_SECURITY_ERR_SUCCESS);
	require(!bluetooth.authorized(&added));
	registered_auth_info->pairing_complete(&added, true);
	require(bluetooth.authorized(&added) && !bluetooth.pairing_active());
	require(bluetooth.poll(0)->link.connected);
	require(registered_auth->pairing_accept(&added, &features) == BT_SECURITY_ERR_PAIR_NOT_ALLOWED);

	require(bluetooth.open_pairing_window().has_value());
	registered_callback->disconnected(&added, 0x13);
	while (bluetooth.poll(0)->type != ble::bluetooth_event::kind::idle) {}
	bt_conn stalled;
	registered_callback->connected(&stalled, 0);
	simulated_time += 15000;
	(void)bluetooth.poll(0);
	require(stalled.disconnects != 0 && !bluetooth.authorized(&stalled));
	registered_auth_info->pairing_complete(&stalled, true);
	require(!bluetooth.authorized(&stalled) && fake_unpairs == 1);
	registered_callback->disconnected(&stalled, 0x13);
	simulated_time += 45000;
	(void)bluetooth.poll(0);
	require(!bluetooth.pairing_active());
	bt_conn outside;
	registered_callback->connected(&outside, 0);
	require(outside.disconnects == 1);
	bluetooth.stop();
	require(!registered_auth && !registered_auth_info);
}
