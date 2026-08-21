#include <cstdio>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "cli/commands.h"
#include "cli/output.h"
#include "sdk/llm.h"
#include "settings/settings.h"

namespace rcli::cli {
namespace {

constexpr char kPrompt[] =
    "Write one paragraph explaining why the sky appears blue during the day.";

/// Only models that generate text: a voice has no tokens per second, and
/// offering to measure one is an action that cannot succeed.
bool Measurable(const sdk::LocalModel& model) {
    for (const catalog::Model& entry : catalog::All()) {
        if (entry.id == model.id) {
            return entry.category == catalog::Category::Language ||
                   entry.category == catalog::Category::Multimodal;
        }
    }
    return model.framework == "LlamaCpp" || model.framework == "MLX";
}

int Measure(const std::string& only) {
    if (!Start()) {
        return 1;
    }
    std::vector<sdk::LocalModel> models;
    for (const sdk::LocalModel& model : sdk::LocalModels()) {
        if (Measurable(model) && (only.empty() || model.id == only)) {
            models.push_back(model);
        }
    }
    if (models.empty()) {
        out::Error(only.empty() ? "no language model downloaded" : "no such model: " + only);
        return 1;
    }

    char header[128];
    std::snprintf(header, sizeof(header), "%-40s %10s %10s %8s", "model", "tok/s", "first tok",
                  "tokens");
    out::Line(out::Bold(header));

    for (const sdk::LocalModel& model : models) {
        out::Progress progress(model.id);
        progress.Tick("loading");
        sdk::Llm llm;
        std::string error;
        if (!llm.Load(model.id, &error)) {
            progress.Finish("failed");
            out::Error(model.id + ": " + error);
            continue;
        }
        progress.Tick("generating");

        std::promise<sdk::Metrics> done;
        std::future<sdk::Metrics> metrics = done.get_future();
        llm.Generate(
            kPrompt, {}, "", [](sdk::Piece, std::string) {},
            [&done](std::string, sdk::Metrics measured) { done.set_value(measured); });
        const sdk::Metrics measured = metrics.get();
        progress.Finish("");

        char line[160];
        std::snprintf(line, sizeof(line), "%-40s %10.1f %8lldms %8d", model.id.c_str(),
                      measured.tokens_per_second, static_cast<long long>(measured.ttft_ms),
                      measured.output_tokens);
        out::Line(line);
    }
    // Placement changes the numbers, so the numbers are worth nothing without
    // it.
    out::Status("accelerator=" + settings::Accelerator() + "  engine=" + settings::Engine());
    return 0;
}

}  // namespace

void RegisterBench(CLI::App& app, Options& options) {
    auto model = std::make_shared<std::string>();
    auto* command = app.add_subcommand("bench", "measure generation speed");
    command->add_option("model", *model, "one model; every downloaded one by default");
    command->callback([&options, model] { options.status = Measure(*model); });
}

}  // namespace rcli::cli
