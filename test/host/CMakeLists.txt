add_library(host_headers INTERFACE)
target_include_directories(host_headers INTERFACE
    fakes
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/lib/expected/include"
)

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

add_executable(log_record_test log_record_test.cpp "${PROJECT_SOURCE_DIR}/src/application/log_record.cpp")
target_link_libraries(log_record_test PRIVATE host_headers)
add_test(NAME log_record_test COMMAND log_record_test)

if(MINGW)
    foreach(test_target component_list_test pwm_test application_test command_protocol_test supervisor_policy_test log_record_test)
        target_link_options(${test_target} PRIVATE -static-libgcc -static-libstdc++)
    endforeach()
endif()
