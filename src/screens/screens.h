#ifndef RCLI_SCREENS_SCREENS_H
#define RCLI_SCREENS_SCREENS_H

#include <memory>
#include <vector>

#include "ui/screen.h"

namespace rcli::screens {

/// The app's screens, in tab order. Position is the number key that selects it.
std::vector<std::unique_ptr<ui::Screen>> All();

}  // namespace rcli::screens

#endif  // RCLI_SCREENS_SCREENS_H
