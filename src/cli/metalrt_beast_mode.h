#pragma once
// =============================================================================
// MetalRT Dashboard — premium, full-width GPU activity panel
// =============================================================================
//
// Uses ftxui::window() for a bordered panel with a branded title.
// The GPU gauge auto-fills the terminal width via ftxui flex.
// =============================================================================

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace rcli {
namespace beast {

class MetalRTDashboard {
public:
    // Bordered ftxui panel for the chat area.
    // Shows GPU gauge + throughput + branding in a windowed panel.
    ftxui::Element Render(float gpu_util, int tps, bool is_active,
                          const ftxui::Color& accent) {
        auto title = ftxui::hbox({
            ftxui::text(" MetalRT ") | ftxui::bold | ftxui::color(accent),
        });

        if (!is_active) {
            auto content = ftxui::hbox({
                ftxui::filler(),
                ftxui::text("Ready") | ftxui::dim,
                ftxui::text("  \xe2\x80\x94  ") | ftxui::dim,
                ftxui::text("Fastest Inference on Apple Silicon") | ftxui::dim,
                ftxui::filler(),
            });
            return ftxui::window(title, content);
        }

        int pct = std::clamp(static_cast<int>(gpu_util * 100), 0, 100);

        auto gpu_clr = (pct >= 80) ? ftxui::Color::Red
                     : (pct >= 50) ? ftxui::Color::Yellow
                     : ftxui::Color::Green;

        ftxui::Elements stats;
        if (tps > 0) {
            stats.push_back(
                ftxui::text(std::to_string(tps) + " tok/s")
                    | ftxui::bold | ftxui::color(ftxui::Color::Green));
        }

        auto gauge_row = ftxui::hbox({
            ftxui::text(" GPU ") | ftxui::bold,
            ftxui::gauge(gpu_util) | ftxui::flex | ftxui::color(gpu_clr),
            ftxui::text(" " + std::to_string(pct) + "%  ")
                | ftxui::bold | ftxui::color(gpu_clr),
            ftxui::hbox(std::move(stats)),
            ftxui::text(" "),
        });

        auto brand_row = ftxui::hbox({
            ftxui::text(" RunAnywhere") | ftxui::bold | ftxui::color(accent),
            ftxui::text("  \xe2\x80\x94  Fastest Inference Engine on Apple Silicon ")
                | ftxui::dim,
            ftxui::filler(),
            ftxui::text("Metal GPU ") | ftxui::dim,
        });

        auto content = ftxui::vbox({
            gauge_row,
            brand_row,
        });

        return ftxui::window(title, content);
    }

    // Compact one-line badge for the models-panel header row.
    std::string RenderBadge(float gpu_util, int tps, bool is_active,
                            bool verbose = false) {
        if (!is_active)
            return "MetalRT";

        std::ostringstream os;
        os << "MetalRT";

        if (verbose && tps > 0)
            os << " | " << tps << " tok/s";

        return os.str();
    }
};

} // namespace beast
} // namespace rcli
