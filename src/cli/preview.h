#ifndef RCLI_CLI_PREVIEW_H
#define RCLI_CLI_PREVIEW_H

#include <string>

namespace rcli::out {

/// Renders a PNG into the terminal, or returns the reason it cannot.
///
/// Two pixel rows per character cell using the upper half block, foreground for
/// the top pixel and background for the bottom — which is what keeps the result
/// square instead of stretched to the cell's 1:2 aspect. Truecolor only: the
/// 256-colour cube turns a photograph into mud, so without it the path is
/// printed instead of a bad picture.
std::string Preview(const std::string& path, int columns);

}  // namespace rcli::out

#endif  // RCLI_CLI_PREVIEW_H
