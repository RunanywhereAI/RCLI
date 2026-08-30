# CLI parse/REPL/output tests. Proto headers come from the kit via rcli_core.
enable_testing()

add_executable(test_rcli_unit test_rcli_unit.cpp)
target_include_directories(test_rcli_unit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_rcli_unit PRIVATE rcli_core)
rcli_stage_windows_runtime_dlls(test_rcli_unit)
add_test(NAME rcli_unit_tests COMMAND test_rcli_unit --run-all)

add_executable(test_rcli_account test_rcli_account.cpp)
target_include_directories(test_rcli_account PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_rcli_account PRIVATE rcli_core)
rcli_stage_windows_runtime_dlls(test_rcli_account)
add_test(NAME rcli_account_tests COMMAND test_rcli_account --run-all)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME rcli_account_cli_e2e
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_account_cli.py"
            "$<TARGET_FILE:rcli>")
set_tests_properties(rcli_account_cli_e2e PROPERTIES TIMEOUT 20)

add_executable(test_rcli_segment test_rcli_segment.cpp)
target_include_directories(test_rcli_segment PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_rcli_segment PRIVATE rcli_core)
rcli_stage_windows_runtime_dlls(test_rcli_segment)
add_test(NAME rcli_segment_tests COMMAND test_rcli_segment --run-all)

add_executable(test_rcli_telemetry_live test_rcli_telemetry_live.cpp)
target_include_directories(test_rcli_telemetry_live PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_rcli_telemetry_live PRIVATE rcli_core)
rcli_stage_windows_runtime_dlls(test_rcli_telemetry_live)
add_test(NAME rcli_telemetry_live_tests COMMAND test_rcli_telemetry_live)

if(APPLE AND RunAnywhere_HAS_MLX)
    add_executable(test_rcli_mlx_e2e test_rcli_mlx_e2e.cpp)
    target_include_directories(test_rcli_mlx_e2e PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_rcli_mlx_e2e PRIVATE rcli_core)
    rcli_stage_windows_runtime_dlls(test_rcli_mlx_e2e)
    add_test(NAME rcli_mlx_e2e_tests COMMAND test_rcli_mlx_e2e --run-all)
endif()
