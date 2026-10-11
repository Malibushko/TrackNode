cmake_minimum_required(VERSION 3.24)
project(BluetoothUartTest LANGUAGES CXX)
enable_testing()
add_compile_definitions(CONFIG_BT_MAX_PAIRED=8)

add_executable(pairing_test pairing_test.cpp
    ../src/drivers/devices/zephyr_bluetooth.cpp
    ../src/drivers/devices/zephyr_gatt_server.cpp)
target_compile_features(pairing_test PRIVATE cxx_std_20)
target_include_directories(pairing_test PRIVATE fake ../include ../../../include ../../../lib/expected/include)
target_compile_options(pairing_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME pairing_test COMMAND pairing_test)
add_executable(zephyr_uart_test zephyr_uart_test.cpp ../src/drivers/devices/zephyr_uart.cpp)
target_compile_features(zephyr_uart_test PRIVATE cxx_std_20)
target_include_directories(zephyr_uart_test PRIVATE fake ../include ../../../lib/expected/include)
target_compile_options(zephyr_uart_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME zephyr_uart_test COMMAND zephyr_uart_test)

add_executable(zephyr_bluetooth_init_test zephyr_bluetooth_init_test.cpp
    ../src/drivers/devices/zephyr_bluetooth.cpp
    ../src/drivers/devices/zephyr_gatt_server.cpp)
target_compile_features(zephyr_bluetooth_init_test PRIVATE cxx_std_20)
target_include_directories(zephyr_bluetooth_init_test PRIVATE fake ../include ../../../include ../../../lib/expected/include)
target_compile_options(zephyr_bluetooth_init_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME zephyr_bluetooth_init_test COMMAND zephyr_bluetooth_init_test)

add_executable(bridge_service_test bridge_service_test.cpp
    ../src/infrastructure/bridge_service.cpp
    ../../../src/drivers/protocols/bridge_codec.cpp
    ../../../src/drivers/protocols/bridge_stream_parser.cpp)
target_compile_features(bridge_service_test PRIVATE cxx_std_20)
target_include_directories(bridge_service_test PRIVATE ../include ../../../include ../../../lib/expected/include)
target_compile_options(bridge_service_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME bridge_service_test COMMAND bridge_service_test)

add_executable(zephyr_gatt_test zephyr_gatt_test.cpp
    ../src/drivers/devices/zephyr_bluetooth.cpp
    ../src/drivers/devices/zephyr_gatt_server.cpp)
find_package(Threads REQUIRED)
target_link_libraries(zephyr_gatt_test PRIVATE Threads::Threads)
target_compile_features(zephyr_gatt_test PRIVATE cxx_std_20)
target_include_directories(zephyr_gatt_test PRIVATE fake ../include ../../../include ../../../lib/expected/include)
target_compile_options(zephyr_gatt_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME zephyr_gatt_test COMMAND zephyr_gatt_test)

add_executable(measurement_test measurement_test.cpp
    ../src/drivers/devices/zephyr_bluetooth.cpp
    ../src/drivers/devices/zephyr_gatt_server.cpp)
target_compile_features(measurement_test PRIVATE cxx_std_20)
target_include_directories(measurement_test PRIVATE fake ../include ../../../include ../../../lib/expected/include)
target_compile_options(measurement_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME measurement_test COMMAND measurement_test)
