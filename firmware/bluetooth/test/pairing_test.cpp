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
	require(!bluetooth.authorized(&reconnect));
	registered_callback->security_changed(&reconnect, BT_SECURITY_L2, BT_SECURITY_ERR_SUCCESS);
	require(!bluetooth.authorized(&reconnect));
	require(bluetooth.pairing_active());
	registered_callback->disconnected(&reconnect, 0x13);
	while (bluetooth.poll(0)->type != ble::bluetooth_event::kind::idle) {}

	fake_restore_bond_security = false;
	bt_conn repairing;
	repairing.security = 1;
	registered_callback->connected(&repairing, 0);
	require(repairing.disconnects == 0 && !bluetooth.authorized(&repairing));
	require(status->read(&repairing, status, value, sizeof(value), 0) == BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION));
	bt_conn_pairing_feat features;
	require(registered_auth->pairing_accept(&repairing, &features) == BT_SECURITY_ERR_SUCCESS);
	repairing.security = BT_SECURITY_L2;
	registered_callback->security_changed(&repairing, BT_SECURITY_L2, BT_SECURITY_ERR_SUCCESS);
	require(!bluetooth.authorized(&repairing));
	registered_auth_info->pairing_complete(&repairing, true);
	require(bluetooth.authorized(&repairing) && !bluetooth.pairing_active());
	require(fake_unpairs == 0);
	require(bluetooth.open_pairing_window().has_value());
	registered_callback->disconnected(&repairing, 0x13);
	while (bluetooth.poll(0)->type != ble::bluetooth_event::kind::idle) {}

	bt_conn stalled_repair;
	registered_callback->connected(&stalled_repair, 0);
	require(registered_auth->pairing_accept(&stalled_repair, &features) == BT_SECURITY_ERR_SUCCESS);
	simulated_time += 15000;
	(void)bluetooth.poll(0);
	require(stalled_repair.disconnects != 0 && !bluetooth.authorized(&stalled_repair));
	registered_callback->security_changed(&stalled_repair, BT_SECURITY_L2, BT_SECURITY_ERR_SUCCESS);
	require(!bluetooth.authorized(&stalled_repair));
	registered_callback->disconnected(&stalled_repair, 0x13);
	while (bluetooth.poll(0)->type != ble::bluetooth_event::kind::idle) {}
	simulated_time += 45000;
	(void)bluetooth.poll(0);
	require(!bluetooth.pairing_active());

	bt_conn outside_repair;
	registered_callback->connected(&outside_repair, 0);
	require(registered_auth->pairing_accept(&outside_repair, &features) == BT_SECURITY_ERR_PAIR_NOT_ALLOWED);
	require(!bluetooth.authorized(&outside_repair));
	registered_callback->disconnected(&outside_repair, 0x13);
	while (bluetooth.poll(0)->type != ble::bluetooth_event::kind::idle) {}
	require(bluetooth.open_pairing_window().has_value());
	simulated_time += 55000;
	bt_conn repair_at_window_end;
	registered_callback->connected(&repair_at_window_end, 0);
	require(registered_auth->pairing_accept(&repair_at_window_end, &features) == BT_SECURITY_ERR_SUCCESS);
	simulated_time += 5000;
	(void)bluetooth.poll(0);
	require(!bluetooth.pairing_active() && repair_at_window_end.disconnects != 0);
	registered_callback->security_changed(&repair_at_window_end, BT_SECURITY_L2, BT_SECURITY_ERR_SUCCESS);
	require(!bluetooth.authorized(&repair_at_window_end));
	registered_callback->disconnected(&repair_at_window_end, 0x13);
	while (bluetooth.poll(0)->type != ble::bluetooth_event::kind::idle) {}
	require(bluetooth.open_pairing_window().has_value());
	fake_restore_bond_security = true;

	fake_known_peer = false;
	bt_conn added;
	registered_callback->connected(&added, 0);
	require(!bluetooth.authorized(&added));
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
