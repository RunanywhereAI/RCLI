#include <cstdlib>
#include <memory>
#include <string>

#include "cli/commands.h"
#include "cli/output.h"
#include "repl/repl.h"
#include "repl/session.h"
#include "sdk/session.h"

namespace rcli::cli {
namespace {

std::string HistoryPath() {
    const std::string home(sdk::Session::Instance().home());
    return home.empty() ? std::string(".rcli_history") : home + "/rcli_history";
}

}  // namespace

int Chat(const std::string& model, const std::string& prompt) {
    if (!Start()) {
        return 1;
    }
    repl::Session session;

    if (!model.empty() && !session.Load(model)) {
        return 1;
    }

    // One-shot: `rcli run qwen3 "why is the sky blue"` answers and exits, so it
    // composes with pipes and scripts the way any other command does.
    if (!prompt.empty()) {
        if (!session.loaded()) {
            out::Error("no model — name one, or /load inside the prompt");
            return 1;
        }
        session.Submit(prompt);
        return 0;
    }

    repl::Line line(HistoryPath());
    line.OnComplete([&session](const std::string& text) { return session.Complete(text); });

    if (session.loaded()) {
        out::Status(session.model() + " — /? for commands, /bye to quit");
    } else {
        out::Status("no model loaded — /load <model>, or /? for commands");
    }

    while (true) {
        const std::optional<std::string> input = line.Read(session.Prompt());
        if (!input.has_value()) {
            break;
        }
        if (!session.Submit(*input)) {
            break;
        }
    }
    return 0;
}

void RegisterChat(CLI::App& app, Options& options) {
    // `run` and `chat` are the same thing under two names, because Ollama
    // taught everyone `run` and the SDK's own CLI offers both.
    auto configure = [&options](CLI::App* command) {
        auto model = std::make_shared<std::string>();
        auto prompt = std::make_shared<std::string>();
        command->add_option("model", *model, "catalog id, alias, or a downloaded model");
        command->add_option("prompt", *prompt,
                            "ask once and exit; omit it for the interactive prompt");
        command->callback([&options, model, prompt] {
            options.status = Chat(*model, *prompt);
        });
    };
    configure(app.add_subcommand("run", "talk to a model"));
    configure(app.add_subcommand("chat", "talk to a model"));
}

}  // namespace rcli::cli
