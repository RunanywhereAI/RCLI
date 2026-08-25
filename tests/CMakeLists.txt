# CLI parse/REPL/output tests. Proto headers come from the kit via rcli_core.
enable_testing()

add_executable(test_rcli_unit test_rcli_unit.cpp)
target_include_directories(test_rcli_unit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_rcli_unit PRIVATE rcli_core)
rcli_stage_windows_runtime_dlls(test_rcli_unit)
add_test(NAME rcli_unit_tests COMMAND test_rcli_unit --run-all)

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
