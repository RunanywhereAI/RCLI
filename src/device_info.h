#ifndef WALLY_DEVICE_INFO_H
#define WALLY_DEVICE_INFO_H

#include "rac/core/rac_types.h"

namespace wally {

// Installs the desktop device-registration callbacks on the commons device
// manager. Must run before SDK phase 2 so registration carries real hardware
// info instead of being skipped for missing callbacks.
rac_result_t install_device_callbacks();

} // namespace wally

#endif // WALLY_DEVICE_INFO_H
