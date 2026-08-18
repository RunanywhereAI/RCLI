#include "screens/models.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

#include <memory>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "theme/theme.h"

namespace rcli::screens {
namespace {

using namespace ftxui;

/// Backends in a fixed order so the list does not reshuffle as the query
/// narrows. Only groups with a match are drawn.
constexpr catalog::Backend kBackendOrder[] = {
    catalog::Backend::LlamaCpp, catalog::Backend::Mlx, catalog::Backend::NeuRT,
    catalog::Backend::Onnx,     catalog::Backend::Sherpa,
};

class Models final : public ui::Screen {
   public:
    Models() {
        InputOption option;
        option.content = &query_;
        option.placeholder = "search models";
        option.multiline = false;
        // Rebuilding on change rather than every frame keeps the row components
        // stable, which is what lets focus survive between keystrokes.
        option.on_change = [this] { Rebuild(); };
        option.transform = [](InputState state) {
            const auto& t = theme::Current();
            return hbox({
                text(" ") | color(t.accent),
                std::move(state.element) | color(state.focused ? t.text : t.textDim),
            });
        };
        search_ = Input(option);

        Rebuild();
        auto layout = Container::Vertical({search_, rows_});
        body_ = Renderer(layout, [this] {
            const auto& t = theme::Current();
            return vbox({
                hbox({search_->Render() | flex}) | bgcolor(t.inset),
                separator() | color(t.separator),
                rows_->Render() | vscroll_indicator | yframe | flex,
            });
        });
    }

    Component Body() override { return body_; }
    std::string_view Title() const override { return "models"; }
    bool CapturesTyping() const override { return true; }

    Element Hints() const override {
        const auto& t = theme::Current();
        return hbox({
            text(std::to_string(shown_)) | color(t.accent),
            text(" of ") | color(t.textDim),
            text(std::to_string(catalog::All().size())) | color(t.textDim),
            text("   "),
            text("up/down") | color(t.accent),
            text(" move   ") | color(t.textDim),
            text("tab") | color(t.accent),
            text(" leave") | color(t.textDim),
        });
    }

   private:
    static Element Heading(catalog::Backend backend, std::size_t count) {
        const auto& t = theme::Current();
        // Apple-only engines are worth calling out beside the count: the number
        // is misleading if half of them cannot run on this machine.
        const bool apple_only =
            backend == catalog::Backend::Mlx || backend == catalog::Backend::NeuRT;
        return hbox({
            text(std::string(catalog::Label(backend))) | bold | color(t.accent),
            text("  " + std::to_string(count)) | color(t.textFaint),
            apple_only ? text("  apple silicon") | color(t.textFaint) : text(""),
        });
    }

    static Element RowElement(const catalog::Model& model, const EntryState& state) {
        const auto& t = theme::Current();
        const bool marked = state.focused;
        return hbox({
            text(marked ? "▌ " : "  ") | color(t.accent),
            text(std::string(model.id)) | color(marked ? t.text : t.textDim),
            text(model.alias.empty() ? "" : "  " + std::string(model.alias)) | color(t.textFaint),
            filler(),
            text(std::string(catalog::Label(model.category))) | color(t.info),
            text("  "),
            text(catalog::HumanSize(model.bytes)) | color(t.textDim),
            text("  "),
        });
    }

    /// Rows are components so they can hold focus; headings are plain Renderers,
    /// which have no children and are therefore skipped when focus moves.
    void Rebuild() {
        const std::vector<const catalog::Model*> hits = catalog::Search(query_);
        shown_ = hits.size();

        rows_->DetachAllChildren();
        for (const catalog::Backend backend : kBackendOrder) {
            std::vector<const catalog::Model*> group;
            for (const catalog::Model* model : hits) {
                if (model->backend == backend) {
                    group.push_back(model);
                }
            }
            if (group.empty()) {
                continue;
            }
            const std::size_t count = group.size();
            rows_->Add(Renderer([backend, count] { return Heading(backend, count); }));
            for (const catalog::Model* model : group) {
                ButtonOption option;
                option.label = std::string(model->id);
                option.on_click = [] {};  // nothing to install yet
                option.transform = [model](const EntryState& state) {
                    return RowElement(*model, state);
                };
                rows_->Add(Button(option));
            }
            rows_->Add(Renderer([] { return text(""); }));
        }
        if (hits.empty()) {
            const std::string query = query_;
            rows_->Add(Renderer([query] {
                const auto& t = theme::Current();
                return hbox({filler(), text("nothing matches \"" + query + "\"") | color(t.textFaint),
                             filler()});
            }));
        }
    }

    std::string query_;
    std::size_t shown_ = 0;
    Component search_;
    Component rows_ = Container::Vertical({});
    Component body_;
};

}  // namespace

std::unique_ptr<ui::Screen> MakeModels() {
    return std::make_unique<Models>();
}

}  // namespace rcli::screens
