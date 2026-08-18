#include "screens/models.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
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
        option.transform = [](InputState state) {
            const auto& t = theme::Current();
            return hbox({
                text(" ") | color(t.accent),
                std::move(state.element) | color(state.focused ? t.text : t.textDim),
            });
        };
        search_ = Input(option);

        body_ = Renderer(search_, [this] { return Render(); });
    }

    Component Body() override { return body_; }
    std::string_view Title() const override { return "models"; }

    Element Hints() const override {
        const auto& t = theme::Current();
        const std::size_t shown = catalog::Search(query_).size();
        return hbox({
            text(std::to_string(shown)) | color(t.accent),
            text(" of ") | color(t.textDim),
            text(std::to_string(catalog::All().size())) | color(t.textDim),
            text("   "),
            text("type") | color(t.accent),
            text(" to filter") | color(t.textDim),
        });
    }

   private:
    Element Row(const catalog::Model& model) const {
        const auto& t = theme::Current();
        return hbox({
            text("  "),
            text(std::string(model.id)) | color(t.text),
            text(model.alias.empty() ? "" : "  " + std::string(model.alias)) | color(t.textFaint),
            filler(),
            text(std::string(catalog::Label(model.modality))) | color(t.info),
            text("  "),
            text(catalog::HumanSize(model.bytes)) | color(t.textDim),
            text("  "),
        });
    }

    Element Group(catalog::Backend backend, const std::vector<const catalog::Model*>& hits) const {
        const auto& t = theme::Current();
        Elements rows;
        for (const catalog::Model* model : hits) {
            if (model->backend == backend) {
                rows.push_back(Row(*model));
            }
        }
        if (rows.empty()) {
            return text("");
        }
        // Apple-only engines are worth calling out here rather than at install
        // time: the count is meaningless if half of them cannot run.
        const bool apple_only =
            backend == catalog::Backend::Mlx || backend == catalog::Backend::NeuRT;
        Element heading = hbox({
            text(std::string(catalog::Label(backend))) | bold | color(t.accent),
            text("  " + std::to_string(rows.size())) | color(t.textFaint),
            apple_only ? text("  apple silicon") | color(t.textFaint) : text(""),
        });
        rows.insert(rows.begin(), heading);
        rows.push_back(text(""));
        return vbox(std::move(rows));
    }

    Element Render() const {
        const auto& t = theme::Current();
        const std::vector<const catalog::Model*> hits = catalog::Search(query_);

        Elements groups;
        for (const catalog::Backend backend : kBackendOrder) {
            groups.push_back(Group(backend, hits));
        }
        if (hits.empty()) {
            groups.push_back(hbox({filler(),
                                   text("nothing matches \"" + query_ + "\"") | color(t.textFaint),
                                   filler()}));
        }

        return vbox({
            hbox({search_->Render() | flex}) | bgcolor(t.inset),
            separator() | color(t.separator),
            vbox(std::move(groups)) | vscroll_indicator | yframe | flex,
        });
    }

    std::string query_;
    Component search_;
    Component body_;
};

}  // namespace

std::unique_ptr<ui::Screen> MakeModels() {
    return std::make_unique<Models>();
}

}  // namespace rcli::screens
