# Build-time copy of private-overlay DLLs next to rcli.exe.
# GLOB runs here (not at configure) so a newly dropped overlay is picked up
# on the next build without re-running CMake.
if(NOT DEFINED SRC_DIR OR NOT DEFINED DST_DIR)
    message(FATAL_ERROR "copy-overlay-dlls: SRC_DIR and DST_DIR required")
endif()
if(NOT IS_DIRECTORY "${SRC_DIR}")
    return()
endif()
file(GLOB _dlls "${SRC_DIR}/*.dll")
foreach(_dll IN LISTS _dlls)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}" "${DST_DIR}")
endforeach()
