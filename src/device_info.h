#ifndef WALLY_DEVICE_INFO_H
#define WALLY_DEVICE_INFO_H

#include <cstdint>
#include <string>

#include "rac/core/rac_types.h"

namespace wally {

// Installs the desktop device-registration callbacks on the commons device
// manager. Must run before SDK phase 2 so registration carries real hardware
// info instead of being skipped for missing callbacks.
rac_result_t install_device_callbacks();

// Local CPU/OS/architecture facts, computed the same way the device
// registration payload is (see collect_device_info() in device_info.cpp) but
// without touching the device manager or registering anything. Used by
// `wally about`; memory comes from rac_get_platform_adapter() instead (same
// source `wally info` already uses), not repeated here.
struct DeviceSnapshot {
    std::string chip;          // CPU model / chip name
    std::string os_version;    // e.g. "macOS 15.1"
    std::string architecture;  // e.g. "arm64"
    int32_t core_count = 0;
    int32_t performance_cores = 0;
    int32_t efficiency_cores = 0;
};

DeviceSnapshot collect_device_snapshot();

} // namespace wally

#endif // WALLY_DEVICE_INFO_H
