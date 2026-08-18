#include "ui/list.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

#include <memory>
#include <utility>

#include "theme/theme.h"

namespace rcli::ui {
namespace {

using namespace ftxui;

/// The selected row is marked by an accent bar and brighter text rather than a
/// full-width fill: at list length the fill reads as a block of colour and
/// stops pointing at anything.
Element RowElement(const Row& row, const EntryState& state) {
    const auto& t = theme::Current();
    const bool marked = state.active || state.focused;
    return hbox({
        text(state.active ? "▌" : " ") | color(t.accent),
        text(" "),
        text(row.label) | color(marked ? t.text : t.textDim),
        filler(),
        text(row.value ? row.value() : std::string{}) | color(marked ? t.accent : t.textDim),
        text(" "),
    });
}

}  // namespace

Component List(std::vector<Row> rows) {
    // Rows outlive the buttons: ButtonOption::label is a ConstStringRef and the
    // transform reads `value` every frame, so both have to keep pointing at
    // storage that is still alive.
    auto storage = std::make_shared<std::vector<Row>>(std::move(rows));

    Components buttons;
    buttons.reserve(storage->size());
    for (std::size_t i = 0; i < storage->size(); ++i) {
        const Row& row = (*storage)[i];
        ButtonOption option;
        option.label = row.label;
        option.on_click = row.activate ? row.activate : [] {};
        option.transform = [storage, i](const EntryState& state) {
            return RowElement((*storage)[i], state);
        };
        buttons.push_back(Button(option));
    }
    return Container::Vertical(std::move(buttons));
}

}  // namespace rcli::ui
