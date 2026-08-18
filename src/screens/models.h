#ifndef RCLI_SCREENS_MODELS_H
#define RCLI_SCREENS_MODELS_H

#include <memory>

#include "ui/screen.h"

namespace rcli::screens {

std::unique_ptr<ui::Screen> MakeModels();

}  // namespace rcli::screens

#endif  // RCLI_SCREENS_MODELS_H
