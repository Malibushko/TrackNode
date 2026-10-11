#include "drivers/devices/zephyr_bluetooth.h"
#include "drivers/protocols/gatt_uuid.h"

#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("GATT integration test failed");
	}
}

int main()
{
	const auto *rx = bt_gatt_find_by_uuid(fake_gatt_service->attrs, fake_gatt_service->attr_count, &ble::gatt_uuid::rx.uuid);
	require(rx != nullptr && rx->write != nullptr);
	auto &ccc = fake_gatt_service->attrs[fake_gatt_service->attr_count - 1];
	require(ccc.changed != nullptr);

	ble::zephyr_bluetooth bluetooth{{.name = "TrackNode"}};
	bt_conn connection;
	std::array<std::uint8_t, ble::event_payload_capacity + 1> payload{};
	payload[0] = 1;
	payload[1] = 2;
	payload[2] = 3;

	require(rx->write(&connection, rx, payload.data(), 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED));
	require(bluetooth.init().has_value());
	registered_callback->connected(&connection, 0);
	const auto connected = bluetooth.poll(0);
	require(connected && connected->link.connected && connected->link.session != 0);
	require(connected->link.max_payload == 20 && !connected->link.subscribed);
	const auto session = connected->link.session;
	require(rx->write(&connection, rx, payload.data(), 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES));
	bluetooth.set_receive_session(session + 1);
	require(rx->write(&connection, rx, payload.data(), 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES));
	bluetooth.set_receive_session(session);

	require(rx->write(&connection, rx, payload.data(), 3, 1, 0) == BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
	require(rx->write(&connection, rx, payload.data(), 3, 0, BT_GATT_WRITE_FLAG_PREPARE) == BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED));
	require(rx->write(&connection, rx, payload.data(), 3, 0, BT_GATT_WRITE_FLAG_EXECUTE) == BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED));
	require(rx->write(&connection, rx, payload.data(), 3, 0, BT_GATT_WRITE_FLAG_CMD) == BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED));
	require(rx->write(&connection, rx, payload.data(), payload.size(), 0, 0) == BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN));
	require(rx->write(&connection, rx, nullptr, 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN));
	require(rx->write(nullptr, rx, payload.data(), 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED));
	require(bluetooth.poll(0)->type == ble::bluetooth_event::kind::idle);

	require(rx->write(&connection, rx, payload.data(), 3, 0, 0) == 3);
	payload[0] = 99;
	const auto received = bluetooth.poll(0);
	require(received && received->type == ble::bluetooth_event::kind::received);
	require(received->size == 3 && received->payload[0] == 1 && received->payload[1] == 2 && received->payload[2] == 3);
	require(received->link.session == session && connection.references == 2);
	require(rx->write(&connection, rx, nullptr, 0, 0, 0) == 0);
	require(bluetooth.poll(0)->size == 0);

	require(bluetooth.send({payload.data(), 3}, session).error().code == ble::error_code::not_ready);
	connection.subscribed = true;
	ccc.changed(&ccc, BT_GATT_CCC_NOTIFY);
	const auto subscribed = bluetooth.poll(0);
	require(subscribed && subscribed->link.subscribed && subscribed->link.session == session);
	require(bluetooth.send({payload.data(), 3}, session).has_value());
	require(notification_count == 1 && notification_data[0] == 99);
	require(bluetooth.send({nullptr, 0}, session).has_value());
	require(notification_data.empty());
	require(bluetooth.send({payload.data(), 3}, session + 1).error().code == ble::error_code::not_connected);
	require(bluetooth.send({payload.data(), 3}, 0).error().code == ble::error_code::not_connected);
	require(bluetooth.send({payload.data(), 21}, session).error().code == ble::error_code::invalid_config);
	require(bluetooth.send({nullptr, 1}, session).error().code == ble::error_code::invalid_config);

	connection.mtu = ble::event_payload_capacity + 3;
	registered_gatt_callback->att_mtu_updated(&connection, connection.mtu, connection.mtu);
	const auto mtu_changed = bluetooth.poll(0);
	require(mtu_changed && mtu_changed->link.max_payload == ble::event_payload_capacity);
	require(bluetooth.send({payload.data(), ble::event_payload_capacity}, session).has_value());
	require(notification_data.size() == ble::event_payload_capacity);
	require(bluetooth.send({payload.data(), payload.size()}, session).error().code == ble::error_code::invalid_config);
	require(rx->write(&connection, rx, payload.data(), ble::event_payload_capacity, 0, 0) == ble::event_payload_capacity);
	require(bluetooth.poll(0)->size == ble::event_payload_capacity);

	notify_error = -ENOMEM;
	require(bluetooth.send({payload.data(), 3}, session).error().code == ble::error_code::no_resources);
	notify_error = -EAGAIN;
	require(bluetooth.send({payload.data(), 3}, session).error().code == ble::error_code::busy);
	notify_error = 0;

	connection.subscribed = false;
	ccc.changed(&ccc, 0);
	require(!bluetooth.poll(0)->link.subscribed);
	require(bluetooth.send({payload.data(), 3}, session).error().code == ble::error_code::not_ready);

	for (int index = 0; index < 8; ++index)
	{
		require(rx->write(&connection, rx, payload.data(), 3, 0, 0) == 3);
	}
	require(rx->write(&connection, rx, payload.data(), 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES));
	require(connection.references == 10);
	require(bluetooth.poll(0).error().code == ble::error_code::overflow);
	require(bluetooth.send({payload.data(), 3}, session).error().code == ble::error_code::overflow);
	bluetooth.stop();
	require(connection.references == 1);
	require(rx->write(&connection, rx, payload.data(), 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED));

	require(bluetooth.init().has_value());
	bt_conn replacement;
	replacement.subscribed = true;
	registered_callback->connected(&replacement, 0);
	const auto reconnected = bluetooth.poll(0);
	require(reconnected && reconnected->link.session != session);
	const auto next_session = reconnected->link.session;
	require(rx->write(&replacement, rx, payload.data(), 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES));
	bluetooth.set_receive_session(next_session);
	require(bluetooth.send({payload.data(), 3}, session).error().code == ble::error_code::not_connected);
	require(rx->write(&connection, rx, payload.data(), 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
	registered_gatt_callback->att_mtu_updated(&connection, 200, 200);
	registered_callback->disconnected(&connection, 0x13);
	require(bluetooth.poll(0)->type == ble::bluetooth_event::kind::idle);
	require(replacement.references == 2);

	notification_hook = [&]
	{
		bluetooth.stop();
		require(replacement.references == 2);
		notify_error = -ENOTCONN;
	};
	require(bluetooth.send({payload.data(), 3}, next_session).error().code == ble::error_code::not_connected);
	notification_hook = {};
	notify_error = 0;
	require(replacement.references == 1);

	advertising_error = -ENOMEM;
	require(bluetooth.init().error().code == ble::error_code::no_resources);
	require(registered_callback == nullptr);
	require(rx->write(&replacement, rx, payload.data(), 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED));
	advertising_error = 0;

	ble::zephyr_gatt_server server;
	require(server.bind({}).error().code == ble::error_code::invalid_config);
	struct blocked_receiver
	{
		std::promise<void> entered;
		std::promise<void> release;
		std::shared_future<void> released = release.get_future().share();
	} receiver;

	require(server.bind({
		.context = &receiver,
		.received = [](void *context, bt_conn *, ble::data_view) -> ble::status
		{
			auto &state = *static_cast<blocked_receiver *>(context);
			state.entered.set_value();
			state.released.wait();
			return {};
		}
	}).has_value());
	require(bluetooth.init().error().code == ble::error_code::already_initialized);

	ssize_t write_result = 0;
	std::thread writer([&]
	{
		write_result = rx->write(&replacement, rx, payload.data(), 3, 0, 0);
	});
	receiver.entered.get_future().wait();
	std::promise<void> unbind_started;
	std::promise<void> unbind_finished;
	auto finished = unbind_finished.get_future();
	std::thread stopper([&]
	{
		unbind_started.set_value();
		server.unbind();
		unbind_finished.set_value();
	});
	unbind_started.get_future().wait();
	const bool waited = finished.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout;
	receiver.release.set_value();
	writer.join();
	stopper.join();
	require(waited && write_result == 3);
	require(rx->write(&replacement, rx, payload.data(), 3, 0, 0) == BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED));
	require(spinlock_depth == 0);

	std::cout << "GATT RX, notify, sessions, MTU, overflow, rollback and unbind synchronization passed\n";
	return 0;
}
