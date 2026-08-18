#ifndef RCLI_SCREENS_CHAT_H
#define RCLI_SCREENS_CHAT_H

#include <memory>

#include "ui/screen.h"

namespace rcli::screens {

std::unique_ptr<ui::Screen> MakeChat();

}  // namespace rcli::screens

#endif  // RCLI_SCREENS_CHAT_H
