# Build-time copy of private-overlay runtime files next to rcli.exe.
# GLOB runs here (not at configure) so a newly dropped overlay is picked up
# on the next build without re-running CMake.
#
# Globs *.dll, *.so AND *.cat: the QHexRT overlay's Bonsai/Maple ternary decoder
# resolves its FastRPC skel (librun_main_on_hexagon_skel.so + its matching .cat
# security catalog, QHexRT/src/bonsai/fastrpc_win.cpp) through ADSP_LIBRARY_PATH
# ∪ exe_dir() -- neither of which this exe's own directory satisfies unless the
# skel/.cat pair is staged here too. A *.dll-only glob left them in kit/bin
# only, so rcli.exe ran every standard QNN-graph model fine (those resolve via
# LoadLibraryW + PATH, which addSidecarDirToDllSearch-equivalent staging still
# covers) while the ternary decoder failed remote_handle64_open with
# AEE_EUNABLETOLOAD (0x80000406) -- content-verified, so this reads exactly
# like a missing/mismatched catalog even though the real cause was the file
# never being copied here at all. See the rcli-e2e skill's "Device / overlay
# gotchas" for the full trace.
if(NOT DEFINED SRC_DIR OR NOT DEFINED DST_DIR)
    message(FATAL_ERROR "copy-overlay-dlls: SRC_DIR and DST_DIR required")
endif()
if(NOT IS_DIRECTORY "${SRC_DIR}")
    return()
endif()
file(GLOB _dlls "${SRC_DIR}/*.dll" "${SRC_DIR}/*.so" "${SRC_DIR}/*.cat")
foreach(_dll IN LISTS _dlls)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}" "${DST_DIR}")
endforeach()
