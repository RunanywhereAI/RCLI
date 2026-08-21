#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "catalog/catalog.h"
#include "cli/commands.h"
#include "cli/output.h"
#include "sdk/download.h"
#include "sdk/llm.h"

namespace rcli::cli {
namespace {

using out::Ink;

const catalog::Model* Find(const std::string& id) {
    for (const catalog::Model& model : catalog::All()) {
        if (model.id == id || (!model.alias.empty() && model.alias == id)) {
            return &model;
        }
    }
    return nullptr;
}

bool IsLocal(const std::string& id) {
    for (const sdk::LocalModel& model : sdk::LocalModels()) {
        if (model.id == id) {
            return model.complete;
        }
    }
    return false;
}

int List(bool all) {
    if (!Start()) {
        return 1;
    }
    if (!all) {
        const std::vector<sdk::LocalModel> models = sdk::LocalModels();
        if (models.empty()) {
            out::Status("nothing downloaded — rcli pull <model>, or rcli list --all");
            return 0;
        }
        for (const sdk::LocalModel& model : models) {
            char line[200];
            std::snprintf(line, sizeof(line), "%-44s %-10s %8s %s", model.id.c_str(),
                          model.framework.c_str(), out::HumanSize(model.bytes).c_str(),
                          model.complete ? "" : "incomplete");
            out::Line(line);
        }
        return 0;
    }
    for (const catalog::Model& model : catalog::All()) {
        char line[220];
        std::snprintf(line, sizeof(line), "%-44s %-10s %-9s %8s %s", std::string(model.id).c_str(),
                      std::string(catalog::Label(model.backend)).c_str(),
                      std::string(catalog::Label(model.category)).c_str(),
                      out::HumanSize(model.bytes).c_str(),
                      IsLocal(std::string(model.id)) ? "downloaded" : "");
        out::Line(line);
    }
    return 0;
}

int Search(const std::string& query) {
    for (const catalog::Model* model : catalog::Search(query)) {
        char line[220];
        std::snprintf(line, sizeof(line), "%-44s %-10s %8s  %s", std::string(model->id).c_str(),
                      std::string(catalog::Label(model->backend)).c_str(),
                      out::HumanSize(model->bytes).c_str(), std::string(model->name).c_str());
        out::Line(line);
    }
    return 0;
}

/// Shared by `rcli pull` and the prompt's /pull. Blocks with a bar, because a
/// command that returns before the bytes have landed has not done its job.
int Pull(const std::string& id) {
    if (!Start()) {
        return 1;
    }
    const catalog::Model* entry = Find(id);
    if (entry == nullptr) {
        out::Error("no catalog entry called " + id);
        return 1;
    }
    const std::string model_id(entry->id);
    if (IsLocal(model_id)) {
        out::Status(model_id + " is already here");
        return 0;
    }

    out::Progress progress(model_id);
    std::string error;
    if (!sdk::Downloads::Instance().Start(*entry, &error)) {
        progress.Finish("failed");
        out::Error(error);
        return 1;
    }
    while (true) {
        const sdk::Download state = sdk::Downloads::Instance().Get(model_id);
        if (state.phase == sdk::Phase::Done) {
            progress.Finish("done");
            return 0;
        }
        if (state.phase == sdk::Phase::Failed || state.phase == sdk::Phase::Cancelled) {
            progress.Finish("failed");
            out::Error(state.detail.empty() ? "download failed" : state.detail);
            return 1;
        }
        if (state.phase == sdk::Phase::Extracting) {
            progress.Tick("extracting");
        } else {
            std::string detail = out::HumanSize(state.bytes);
            if (state.bytes_per_second > 0.0F) {
                detail +=
                    "  " + out::HumanSize(static_cast<std::int64_t>(state.bytes_per_second)) + "/s";
            }
            progress.Update(state.fraction, detail);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }
}

int Remove(const std::string& id) {
    if (!Start()) {
        return 1;
    }
    const catalog::Model* entry = Find(id);
    const std::string model_id = entry != nullptr ? std::string(entry->id) : id;
    std::int64_t freed = 0;
    std::string error;
    if (!sdk::Remove(model_id, &freed, &error)) {
        out::Error(error);
        return 1;
    }
    out::Line("deleted " + model_id + ", freed " + out::HumanSize(freed));
    return 0;
}

int Show(const std::string& id) {
    if (!Start()) {
        return 1;
    }
    const catalog::Model* entry = Find(id);
    if (entry == nullptr) {
        out::Error("no catalog entry called " + id);
        return 1;
    }
    auto row = [](const char* key, const std::string& value) {
        if (value.empty()) {
            return;
        }
        char line[256];
        std::snprintf(line, sizeof(line), "  %-16s %s", key, value.c_str());
        out::Line(line);
    };
    row("id", std::string(entry->id));
    row("alias", std::string(entry->alias));
    row("name", std::string(entry->name));
    row("engine", std::string(catalog::Label(entry->backend)));
    row("kind", std::string(catalog::Label(entry->category)));
    row("format", std::string(catalog::Label(entry->format)));
    row("size", out::HumanSize(entry->bytes));
    if (entry->context_length > 0) {
        row("context", std::to_string(entry->context_length));
    }
    row("reasoning", entry->thinks ? "yes" : "no");
    row("files", entry->files.empty() ? "1" : std::to_string(entry->files.size()));
    row("downloaded", IsLocal(std::string(entry->id)) ? "yes" : "no");
    return 0;
}

}  // namespace

void RegisterModels(CLI::App& app, Options& options) {
    auto all = std::make_shared<bool>(false);
    auto* list = app.add_subcommand("list", "models on this machine");
    list->add_flag("-a,--all", *all, "every model in the catalog, downloaded or not");
    list->callback([&options, all] { options.status = List(*all); });

    auto query = std::make_shared<std::string>();
    auto* search = app.add_subcommand("search", "find a model in the catalog");
    search->add_option("query", *query, "matched against id, alias and name");
    search->callback([&options, query] { options.status = Search(*query); });

    auto pull_id = std::make_shared<std::string>();
    auto* pull = app.add_subcommand("pull", "download a model");
    pull->add_option("model", *pull_id, "catalog id or alias")->required();
    pull->callback([&options, pull_id] { options.status = Pull(*pull_id); });

    auto rm_id = std::make_shared<std::string>();
    auto* remove = app.add_subcommand("rm", "delete a downloaded model");
    remove->add_option("model", *rm_id, "model id")->required();
    remove->callback([&options, rm_id] { options.status = Remove(*rm_id); });

    auto show_id = std::make_shared<std::string>();
    auto* show = app.add_subcommand("show", "what the catalog knows about a model");
    show->add_option("model", *show_id, "catalog id or alias")->required();
    show->callback([&options, show_id] { options.status = Show(*show_id); });
}

}  // namespace rcli::cli
