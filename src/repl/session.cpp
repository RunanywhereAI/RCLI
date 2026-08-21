#include "repl/session.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <utility>

#include "catalog/catalog.h"
#include "chat/complete.h"
#include "cli/output.h"
#include "cli/preview.h"
#include "sdk/download.h"
#include "sdk/session.h"
#include "settings/settings.h"
#include "tools/shell.h"

namespace rcli::repl {
namespace {

namespace fs = std::filesystem;
using out::Ink;

/// Finds a catalog entry by id or alias.
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

std::pair<std::string, std::string> Split(const std::string& line) {
    const std::size_t space = line.find(' ');
    if (space == std::string::npos) {
        return {line, ""};
    }
    std::string rest = line.substr(space + 1);
    while (!rest.empty() && rest.front() == ' ') {
        rest.erase(rest.begin());
    }
    return {line.substr(0, space), rest};
}

}  // namespace

Session::Session() = default;
Session::~Session() = default;

std::string Session::Prompt() const {
    return out::Paint(Ink::Accent, ">>> ");
}

/// Downloads the model when it is not here, then loads it. The download is
/// synchronous and shows a bar: a prompt that returns before the model is
/// usable would only move the wait somewhere less obvious.
bool Session::Load(const std::string& id) {
    if (!IsLocal(id)) {
        const catalog::Model* entry = Find(id);
        if (entry == nullptr) {
            out::Error("no model called " + id);
            const std::vector<const catalog::Model*> near = catalog::Search(id);
            for (std::size_t i = 0; i < near.size() && i < 5; ++i) {
                out::Status("  did you mean " + std::string(near[i]->id) + "?");
            }
            return false;
        }
        if (!IsLocal(std::string(entry->id))) {
            Pull(std::string(entry->id));
        }
    }
    const std::string resolved =
        IsLocal(id) ? id : (Find(id) != nullptr ? std::string(Find(id)->id) : id);

    out::Progress progress("loading " + resolved);
    progress.Tick("");
    std::string error;
    const bool ok = llm_.Load(resolved, &error);
    progress.Finish(ok ? "" : "failed");
    if (!ok) {
        out::Error(error.empty() ? "could not load " + resolved : error);
        return false;
    }
    if (!error.empty()) {
        out::Status(error);
    }
    return true;
}

bool Session::Submit(const std::string& line) {
    if (line.empty()) {
        return true;
    }
    if (line.front() == '/') {
        return Command(line);
    }
    Ask(line);
    return true;
}

bool Session::Command(const std::string& line) {
    const auto [name, rest] = Split(line);

    if (name == "/bye" || name == "/exit" || name == "/quit") {
        return false;
    }
    if (name == "/?" || name == "/help") {
        Help();
    } else if (name == "/show") {
        ShowSettings();
    } else if (name == "/set") {
        Set(rest);
    } else if (name == "/models" || name == "/list") {
        Models();
    } else if (name == "/load") {
        if (rest.empty()) {
            out::Error("usage: /load <model>");
        } else if (Load(rest)) {
            history_.clear();
            out::Say(Ink::Faint, "loaded " + llm_.model_id());
        }
    } else if (name == "/pull") {
        rest.empty() ? out::Error("usage: /pull <model>") : Pull(rest);
    } else if (name == "/rm") {
        rest.empty() ? out::Error("usage: /rm <model>") : Remove(rest);
    } else if (name == "/image") {
        Attach(rest);
    } else if (name == "/doc") {
        Document(rest);
    } else if (name == "/imagine" || name == "/draw") {
        Draw(rest);
    } else if (name == "/say") {
        Speak(rest.empty() ? last_answer_ : rest);
    } else if (name == "/mic") {
        Listen();
    } else if (name == "/run" || name == "/sh") {
        Shell(rest);
    } else if (name == "/think") {
        show_reasoning_ = !show_reasoning_;
        out::Say(Ink::Faint, show_reasoning_ ? "showing reasoning" : "hiding reasoning");
    } else if (name == "/clear") {
        history_.clear();
        out::Say(Ink::Faint, "context cleared");
    } else if (name == "/history") {
        for (const sdk::Turn& turn : history_) {
            out::Say(turn.from_user ? Ink::Accent : Ink::Plain,
                     (turn.from_user ? ">>> " : "    ") + turn.text);
        }
    } else {
        out::Error("unknown command " + name + " — /? for help");
    }
    return true;
}

void Session::Help() const {
    struct Row {
        const char* name;
        const char* what;
    };
    static const Row kRows[] = {
        {"/load <model>", "load a model, downloading it if needed"},
        {"/models", "what is on this machine"},
        {"/pull <model>", "download from the catalog"},
        {"/rm <model>", "delete a downloaded model"},
        {"/set <k> <v>", "change a setting"},
        {"/show", "current settings"},
        {"/image <path>", "ask about a picture"},
        {"/doc <path>", "put a text file in the context"},
        {"/imagine <text>", "generate an image"},
        {"/mic", "record, transcribe, and send"},
        {"/say [text]", "speak it, or the last answer"},
        {"/run <cmd>", "run a shell command, after you confirm"},
        {"/think", "show or hide reasoning"},
        {"/history", "the conversation so far"},
        {"/clear", "forget the conversation"},
        {"/bye", "quit"},
    };
    for (const Row& row : kRows) {
        char line[96];
        std::snprintf(line, sizeof(line), "  %-17s %s", row.name, row.what);
        out::Say(Ink::Dim, line);
    }
}

void Session::ShowSettings() const {
    for (const settings::Setting& setting : settings::All()) {
        char line[128];
        std::snprintf(line, sizeof(line), "  %-16s %s", setting.name.c_str(),
                      setting.get().c_str());
        out::Line(out::Paint(Ink::Dim, line));
    }
}

void Session::Set(const std::string& rest) {
    const auto [name, value] = Split(rest);
    if (name.empty() || value.empty()) {
        out::Error("usage: /set <setting> <value>");
        return;
    }
    const settings::Setting* setting = settings::Find(name);
    if (setting == nullptr) {
        out::Error("no setting called " + name);
        return;
    }
    if (!setting->set(value)) {
        out::Error(value + " is not a valid " + name);
        if (!setting->values.empty()) {
            std::string allowed;
            for (const std::string& option : setting->values) {
                allowed += allowed.empty() ? option : ", " + option;
            }
            out::Status("  one of: " + allowed);
        }
        return;
    }
    out::Say(Ink::Faint, name + " = " + setting->get());
}

void Session::Models() const {
    const std::vector<sdk::LocalModel> models = sdk::LocalModels();
    if (models.empty()) {
        out::Status("nothing downloaded — /pull <model>");
        return;
    }
    for (const sdk::LocalModel& model : models) {
        char line[128];
        std::snprintf(line, sizeof(line), "  %-42s %-10s %s", model.id.c_str(),
                      model.framework.c_str(), model.complete ? "" : "incomplete");
        out::Line(out::Paint(Ink::Plain, line));
    }
}

void Session::Pull(const std::string& id) {
    const catalog::Model* entry = Find(id);
    if (entry == nullptr) {
        out::Error("no catalog entry called " + id);
        return;
    }
    const std::string model_id(entry->id);
    if (IsLocal(model_id)) {
        out::Say(Ink::Faint, model_id + " is already here");
        return;
    }

    out::Progress progress(model_id);
    std::string error;
    if (!sdk::Downloads::Instance().Start(*entry, &error)) {
        progress.Finish("failed");
        out::Error(error);
        return;
    }
    // Poll rather than subscribe: this call is the whole foreground, and a
    // listener would only hand the work back to this same thread.
    sdk::Phase phase = sdk::Phase::Pending;
    while (true) {
        const sdk::Download state = sdk::Downloads::Instance().Get(model_id);
        phase = state.phase;
        if (phase == sdk::Phase::Done || phase == sdk::Phase::Failed ||
            phase == sdk::Phase::Cancelled) {
            break;
        }
        if (phase == sdk::Phase::Extracting) {
            progress.Tick("extracting");
        } else {
            std::string detail = out::HumanSize(state.bytes);
            if (state.bytes_per_second > 0.0F) {
                detail += "  " + out::HumanSize(static_cast<std::int64_t>(state.bytes_per_second)) +
                          "/s";
            }
            progress.Update(state.fraction, detail);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }
    const sdk::Download final_state = sdk::Downloads::Instance().Get(model_id);
    progress.Finish(phase == sdk::Phase::Done ? "done" : "failed");
    if (phase != sdk::Phase::Done) {
        out::Error(final_state.detail.empty() ? "download failed" : final_state.detail);
    }
}

void Session::Remove(const std::string& id) {
    const catalog::Model* entry = Find(id);
    const std::string model_id = entry != nullptr ? std::string(entry->id) : id;
    std::int64_t freed = 0;
    std::string error;
    if (sdk::Remove(model_id, &freed, &error)) {
        out::Say(Ink::Faint, "deleted " + model_id + ", freed " + out::HumanSize(freed));
    } else {
        out::Error(error);
    }
}

void Session::Attach(const std::string& path) {
    if (path.empty()) {
        out::Error("usage: /image <path>");
        return;
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        out::Error("no file at " + path);
        return;
    }
    if (!llm_.loaded()) {
        out::Error("load a vision model first — /load <model>");
        return;
    }
    if (!llm_.multimodal()) {
        out::Error(llm_.model_id() + " is not a vision model");
        return;
    }
    pending_image_ = path;
    out::Say(Ink::Faint, "attached — ask your question (image turns are single-turn)");
}

void Session::Document(const std::string& path) {
    if (path.empty()) {
        out::Error("usage: /doc <path>");
        return;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        out::Error("cannot read " + path);
        return;
    }
    std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (body.find('\0') != std::string::npos) {
        out::Error(path + " is not text — /image handles pictures, and PDFs are not supported");
        return;
    }
    constexpr std::size_t kLimit = 32000;
    const bool trimmed = body.size() > kLimit;
    if (trimmed) {
        body.resize(kLimit);
    }
    const std::string name = fs::path(path).filename().string();
    history_.push_back({true, "Document " + name + ":\n" + body});
    history_.push_back({false, "Read " + name + "."});
    out::Say(Ink::Faint, std::to_string(body.size()) + " characters added" +
                             (trimmed ? " (truncated)" : "") + " — ask about it");
}

void Session::Ask(const std::string& prompt) {
    if (!llm_.loaded()) {
        out::Error("no model loaded — /load <model>");
        return;
    }
    std::string image = std::exchange(pending_image_, {});

    // Reasoning and answer go to different places on purpose: the answer is the
    // result and belongs on stdout, the thinking is commentary and belongs on
    // stderr, so `rcli run m "q" > out.txt` captures the answer alone.
    bool in_reasoning = false;
    bool reasoned = false;
    std::string answer;
    // Generation runs on its own thread so a UI can stay responsive; a prompt
    // has no such need and must not return before the answer is complete.
    std::promise<std::string> finished;
    std::future<std::string> done = finished.get_future();
    const bool started = llm_.Generate(
        prompt, history_, image,
        [&](sdk::Piece piece, std::string token) {
            const bool thinking = piece == sdk::Piece::Thinking;
            if (thinking) {
                if (!show_reasoning_) {
                    return;
                }
                if (!in_reasoning) {
                    std::fputs(out::Paint(Ink::Faint, "thinking ").c_str(), stderr);
                    in_reasoning = true;
                    reasoned = true;
                }
                std::fputs(out::Paint(Ink::Faint, token).c_str(), stderr);
                std::fflush(stderr);
                return;
            }
            if (in_reasoning) {
                std::fputc('\n', stderr);
                in_reasoning = false;
            }
            answer += token;
            std::fwrite(token.data(), 1, token.size(), stdout);
            std::fflush(stdout);
        },
        [&finished](const std::string& failure, sdk::Metrics) {
            finished.set_value(failure);
        });
    if (!started) {
        out::Error("still generating");
        return;
    }
    const std::string failure = done.get();
    if (in_reasoning) {
        std::fputc('\n', stderr);
    }
    if (!failure.empty()) {
        out::Error(failure);
    }
    if (answer.empty() && failure.empty()) {
        // A model that reasons can spend the whole budget thinking and never
        // reach an answer. Reasoning is on stderr and the answer on stdout, so
        // without this the command prints nothing at all and exits 0, which
        // reads as a broken install rather than a budget that ran out.
        out::Status(reasoned
                        ? "the model used its whole token budget reasoning and did not answer; "
                          "raise max-tokens or turn reasoning off"
                        : "the model returned nothing");
    }
    if (!answer.empty()) {
        std::fputc('\n', stdout);
        std::fflush(stdout);
        history_.push_back({true, prompt});
        history_.push_back({false, answer});
        last_answer_ = answer;
    }
}

void Session::Draw(const std::string& prompt) {
    if (prompt.empty()) {
        out::Error("usage: /imagine <what to draw>");
        return;
    }
    if (!imagine_.loaded()) {
        std::string id = sdk::Imagine::DefaultModel();
        if (id.empty()) {
            for (const catalog::Model& model : catalog::All()) {
                if (model.category == catalog::Category::ImageGeneration &&
                    catalog::Installable(model)) {
                    Pull(std::string(model.id));
                    id = std::string(model.id);
                    break;
                }
            }
        }
        if (id.empty() || !IsLocal(id)) {
            out::Error("no image model available");
            return;
        }
        out::Progress loading("loading " + id);
        loading.Tick("");
        std::string error;
        const bool ok = imagine_.Load(id, &error);
        loading.Finish(ok ? "" : "failed");
        if (!ok) {
            out::Error(error);
            return;
        }
    }

    // A picture takes minutes and the lifecycle entry point reports no steps,
    // so elapsed time is the honest signal.
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
    const sdk::Imagine::Result result = imagine_.Draw(prompt, nullptr, &error);
    running.store(false);
    ticker.join();
    progress.Finish(result.path.empty() ? "failed" : "");

    if (result.path.empty()) {
        out::Error(error);
        return;
    }
    const std::string picture = out::Preview(result.path, 48);
    if (!picture.empty()) {
        std::fputs(picture.c_str(), stdout);
    }
    out::Line(result.path);
}

void Session::Speak(const std::string& text) {
    if (text.empty()) {
        out::Error("nothing to say yet");
        return;
    }
    if (!audio::Available()) {
        out::Error("this build has no audio output");
        return;
    }
    if (!speech_.has_voice()) {
        const std::string id = sdk::Speech::DefaultVoice();
        if (id.empty()) {
            out::Error("no voice downloaded — try /pull vits-piper-en_US-lessac-medium");
            return;
        }
        std::string error;
        if (!speech_.LoadVoice(id, &error)) {
            out::Error(error);
            return;
        }
        out::Status("voice " + id);
    }
    std::string error;
    sdk::Speech::Clip clip = speech_.Speak(text, &error);
    if (clip.samples.empty()) {
        out::Error(error);
        return;
    }
    const auto seconds = static_cast<std::int64_t>(clip.samples.size()) /
                         std::max(1, clip.sample_rate);
    if (!player_.Play(std::move(clip.samples), clip.sample_rate, &error)) {
        out::Error(error);
        return;
    }
    // Playback is the device's own thread; returning to the prompt while it
    // runs is right, but the process must not exit under it.
    out::Status("speaking " + out::HumanDuration(seconds));
}

void Session::Listen() {
    if (!audio::Available()) {
        out::Error("this build has no audio capture");
        return;
    }
    if (!speech_.has_recogniser()) {
        const std::string id = sdk::Speech::DefaultRecogniser();
        if (id.empty()) {
            out::Error("no speech model downloaded — try /pull sherpa-onnx-whisper-tiny.en");
            return;
        }
        std::string error;
        if (!speech_.LoadRecogniser(id, &error)) {
            out::Error(error);
            return;
        }
        out::Status("listening with " + id);
    }
    std::string error;
    if (!recorder_.Start(&error)) {
        out::Error(error);
        return;
    }

    out::Progress meter("recording");
    // Enter ends it. Reading a whole line is what a terminal gives us without
    // taking over the tty, and it matches "press enter to stop".
    std::atomic<bool> waiting{true};
    std::thread key([&] {
        std::string ignored;
        std::getline(std::cin, ignored);
        waiting.store(false);
    });
    while (waiting.load()) {
        const float level = std::min(1.0F, recorder_.level() * 6.0F);
        meter.Update(level, "enter to stop");
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    key.join();
    const std::vector<float> samples = recorder_.Stop();
    meter.Finish(out::HumanDuration(static_cast<std::int64_t>(samples.size()) /
                                    audio::kCaptureRate));

    if (samples.empty()) {
        out::Status("nothing was recorded");
        return;
    }
    const std::string text = speech_.Transcribe(samples, audio::kCaptureRate, &error);
    if (text.empty()) {
        out::Error(error);
        return;
    }
    out::Line(out::Paint(Ink::Accent, ">>> ") + text);
    Ask(text);
}

void Session::Shell(const std::string& command) {
    if (command.empty()) {
        out::Error("usage: /run <command>");
        return;
    }
    out::Say(Ink::Warning, "about to run:  " + command);
    std::fputs(out::Paint(Ink::Dim, "press y to run it, anything else cancels: ").c_str(), stderr);
    std::string answer;
    if (!std::getline(std::cin, answer) || (answer != "y" && answer != "yes")) {
        out::Status("cancelled");
        return;
    }
    const tools::Output result = tools::Run(command);
    if (!result.text.empty()) {
        out::Line(result.text);
    }
    out::Say(result.status == 0 ? Ink::Faint : Ink::Error,
             "exit " + std::to_string(result.status));
}

std::vector<std::string> Session::Complete(const std::string& line) const {
    std::vector<std::string> out;
    for (const chat::Completion& option : chat::Suggest(line)) {
        out.push_back(option.replacement);
    }
    return out;
}

}  // namespace rcli::repl
