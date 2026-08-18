#ifndef RCLI_UI_LIST_H
#define RCLI_UI_LIST_H

#include <ftxui/component/component_base.hpp>

#include <functional>
#include <string>
#include <vector>

namespace rcli::ui {

/// One selectable line: a label on the left, a live value on the right.
///
/// `value` is read every frame rather than stored, so a row reflects state that
/// changed elsewhere without anyone having to refresh it. `activate` runs on
/// Enter, Space or a click.
struct Row {
    std::string label;
    std::function<std::string()> value;
    std::function<void()> activate;
};

/// A vertical list of rows: Up and Down move, Enter activates, the mouse does
/// both. Built from FTXUI buttons so focus, hover and click handling are the
/// library's rather than ours.
ftxui::Component List(std::vector<Row> rows);

}  // namespace rcli::ui

#endif  // RCLI_UI_LIST_H
