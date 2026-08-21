#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "catalog/catalog.h"
#include "cli/commands.h"
#include "cli/output.h"
#include "cli/preview.h"
#include "sdk/imagine.h"

namespace rcli::cli {
namespace {

int Imagine(const std::string& prompt, const std::string& model, bool quiet) {
    if (!Start()) {
        return 1;
    }
    sdk::Imagine imagine;
    const std::string id = model.empty() ? sdk::Imagine::DefaultModel() : model;
    if (id.empty()) {
        out::Error("no image model downloaded — rcli pull stable-diffusion-v1-5-coreml");
        return 1;
    }
    {
        out::Progress loading("loading " + id);
        loading.Tick("");
        std::string error;
        if (!imagine.Load(id, &error)) {
            loading.Finish("failed");
            out::Error(error);
            return 1;
        }
        loading.Finish("");
    }

    // Minutes, with no step reporting on the lifecycle entry point, so elapsed
    // time is the honest signal — a percentage here would be invented.
    out::Progress progress("drawing");
    std::atomic<bool> running{true};
    const auto begin = std::chrono::steady_clock::now();
    std::thread ticker([&] {
        while (running.load()) {
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::steady_clock::now() - begin)
                                     .count();
            progress.Tick(out::HumanDuration(seconds));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });

    std::string error;
    const sdk::Imagine::Result result = imagine.Draw(prompt, nullptr, &error);
    running.store(false);
    ticker.join();
    progress.Finish(result.path.empty() ? "failed" : "");

    if (result.path.empty()) {
        out::Error(error);
        return 1;
    }
    if (!quiet) {
        const std::string picture = out::Preview(result.path, 48);
        if (!picture.empty()) {
            std::fputs(picture.c_str(), stdout);
        }
    }
    // The path last and alone on stdout, so `rcli imagine ... | xargs open`
    // works.
    out::Line(result.path);
    return 0;
}

}  // namespace

void RegisterImage(CLI::App& app, Options& options) {
    auto prompt = std::make_shared<std::string>();
    auto model = std::make_shared<std::string>();
    auto quiet = std::make_shared<bool>(false);
    auto* command = app.add_subcommand("imagine", "generate an image");
    command->alias("draw");
    command->add_option("prompt", *prompt, "what to draw")->required();
    command->add_option("-m,--model", *model, "image model; the downloaded one by default");
    command->add_flag("-q,--no-preview", *quiet, "print the path without drawing it");
    command->callback([&options, prompt, model, quiet] {
        options.status = Imagine(*prompt, *model, *quiet);
    });
}

}  // namespace rcli::cli
