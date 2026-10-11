add_library(host_headers INTERFACE)
target_include_directories(host_headers INTERFACE
    fakes
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/lib/expected/include"
)

add_executable(control_button_gesture_test control_button_gesture_test.cpp)
target_link_libraries(control_button_gesture_test PRIVATE host_headers)
add_test(NAME control_button_gesture_test COMMAND control_button_gesture_test)
if(MINGW)
    target_link_options(control_button_gesture_test PRIVATE -static-libgcc -static-libstdc++)
endif()

add_executable(board_state_service_test board_state_service_test.cpp
    "${PROJECT_SOURCE_DIR}/src/infrastructure/board_state_service.cpp")
target_include_directories(board_state_service_test PRIVATE board_state_fakes)
target_link_libraries(board_state_service_test PRIVATE host_headers)
add_test(NAME board_state_service_test COMMAND board_state_service_test)

add_executable(component_list_test component_list_test.cpp)
target_link_libraries(component_list_test PRIVATE host_headers)
add_test(NAME component_list_test COMMAND component_list_test)

add_executable(pwm_test pwm_test.cpp "${PROJECT_SOURCE_DIR}/src/drivers/pwm.cpp")
target_link_libraries(pwm_test PRIVATE host_headers)
add_test(NAME pwm_test COMMAND pwm_test)

add_executable(application_test application_test.cpp)
target_include_directories(application_test PRIVATE application_fakes)
target_link_libraries(application_test PRIVATE host_headers)
add_test(NAME application_test COMMAND application_test)

add_executable(command_protocol_test command_protocol_test.cpp
    "${PROJECT_SOURCE_DIR}/src/infrastructure/command_parser.cpp"
    "${PROJECT_SOURCE_DIR}/src/infrastructure/track_node_control_protocol.cpp")
target_link_libraries(command_protocol_test PRIVATE host_headers)
add_test(NAME command_protocol_test COMMAND command_protocol_test)

add_executable(supervisor_policy_test supervisor_policy_test.cpp)
target_link_libraries(supervisor_policy_test PRIVATE host_headers)
add_test(NAME supervisor_policy_test COMMAND supervisor_policy_test)

add_executable(gnss_lookup_strategy_test gnss_lookup_strategy_test.cpp)
target_link_libraries(gnss_lookup_strategy_test PRIVATE host_headers)
add_test(NAME gnss_lookup_strategy_test COMMAND gnss_lookup_strategy_test)

add_executable(supervisor_policy_no_storage_test supervisor_policy_test.cpp)
target_link_libraries(supervisor_policy_no_storage_test PRIVATE host_headers)
target_compile_definitions(supervisor_policy_no_storage_test PRIVATE NO_STORAGE=1)
add_test(NAME supervisor_policy_no_storage_test COMMAND supervisor_policy_no_storage_test)

add_executable(log_record_test log_record_test.cpp "${PROJECT_SOURCE_DIR}/src/application/log_record.cpp")
target_link_libraries(log_record_test PRIVATE host_headers)
add_test(NAME log_record_test COMMAND log_record_test)

add_executable(radio_packet_test radio_packet_test.cpp "${PROJECT_SOURCE_DIR}/src/application/radio_packet.cpp")
target_link_libraries(radio_packet_test PRIVATE host_headers)
add_test(NAME radio_packet_test COMMAND radio_packet_test)

add_executable(radio_publish_policy_test radio_publish_policy_test.cpp)
target_link_libraries(radio_publish_policy_test PRIVATE host_headers)
add_test(NAME radio_publish_policy_test COMMAND radio_publish_policy_test)

add_executable(nmea0183_test nmea0183_test.cpp "${PROJECT_SOURCE_DIR}/src/drivers/protocols/nmea0183.cpp")
target_link_libraries(nmea0183_test PRIVATE host_headers)
add_test(NAME nmea0183_test COMMAND nmea0183_test)

add_executable(neo6m_test neo6m_test.cpp
    "${PROJECT_SOURCE_DIR}/src/drivers/devices/neo6m.cpp"
    "${PROJECT_SOURCE_DIR}/src/drivers/protocols/nmea0183.cpp")
target_include_directories(neo6m_test PRIVATE neo6m_fakes)
target_link_libraries(neo6m_test PRIVATE host_headers)
add_test(NAME neo6m_test COMMAND neo6m_test)

foreach(test_target service_list_test telemetry_router_test command_reply_test)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE host_headers)
    add_test(NAME ${test_target} COMMAND ${test_target})
    if(MINGW)
        target_link_options(${test_target} PRIVATE -static-libgcc -static-libstdc++)
    endif()
endforeach()

if(MINGW)
    target_link_options(board_state_service_test PRIVATE -static-libgcc -static-libstdc++)
    foreach(test_target component_list_test pwm_test application_test command_protocol_test supervisor_policy_test gnss_lookup_strategy_test supervisor_policy_no_storage_test log_record_test radio_packet_test radio_publish_policy_test nmea0183_test neo6m_test)
        target_link_options(${test_target} PRIVATE -static-libgcc -static-libstdc++)
    endforeach()
endif()

add_executable(bridge_codec_test bridge_codec_test.cpp
    "${PROJECT_SOURCE_DIR}/src/drivers/protocols/bridge_codec.cpp")
target_link_libraries(bridge_codec_test PRIVATE host_headers)
add_test(NAME bridge_codec_test COMMAND bridge_codec_test)
if(MINGW)
    target_link_options(bridge_codec_test PRIVATE -static-libgcc -static-libstdc++)
endif()

add_executable(bridge_stream_parser_test bridge_stream_parser_test.cpp
    "${PROJECT_SOURCE_DIR}/src/drivers/protocols/bridge_codec.cpp"
    "${PROJECT_SOURCE_DIR}/src/drivers/protocols/bridge_stream_parser.cpp")
target_link_libraries(bridge_stream_parser_test PRIVATE host_headers)
add_test(NAME bridge_stream_parser_test COMMAND bridge_stream_parser_test)

add_executable(xiao_ble_bridge_test xiao_ble_bridge_test.cpp
    "${PROJECT_SOURCE_DIR}/src/drivers/devices/xiao_ble_bridge.cpp"
    "${PROJECT_SOURCE_DIR}/src/drivers/protocols/bridge_codec.cpp"
    "${PROJECT_SOURCE_DIR}/src/drivers/protocols/bridge_stream_parser.cpp")
target_include_directories(xiao_ble_bridge_test PRIVATE bluetooth_fakes)
target_link_libraries(xiao_ble_bridge_test PRIVATE host_headers)
add_test(NAME xiao_ble_bridge_test COMMAND xiao_ble_bridge_test)

add_executable(bluetooth_service_test bluetooth_service_test.cpp
    "${PROJECT_SOURCE_DIR}/src/infrastructure/bluetooth_service.cpp"
    "${PROJECT_SOURCE_DIR}/src/infrastructure/diagnostic_log.cpp"
    "${PROJECT_SOURCE_DIR}/src/infrastructure/service_status.cpp"
    "${PROJECT_SOURCE_DIR}/src/infrastructure/command_parser.cpp"
    "${PROJECT_SOURCE_DIR}/src/application/log_record.cpp")
target_include_directories(bluetooth_service_test PRIVATE bluetooth_fakes)
target_link_libraries(bluetooth_service_test PRIVATE host_headers)
add_test(NAME bluetooth_service_test COMMAND bluetooth_service_test)
