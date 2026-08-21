#include "chat/complete.h"

#include <algorithm>

#include "catalog/catalog.h"
#include "sdk/llm.h"
#include "settings/settings.h"

namespace rcli::chat {
namespace {

struct Command {
    const char* name;
    const char* detail;
};

/// The command surface, in one list, so help and completion cannot disagree.
constexpr Command kCommands[] = {
    {"/load", "load a model"},
    {"/models", "models on this machine"},
    {"/pull", "download a model from the catalog"},
    {"/set", "change a setting"},
    {"/show", "current settings"},
    {"/image", "ask about an image file"},
    {"/doc", "add a document to the conversation"},
    {"/imagine", "generate an image"},
    {"/mic", "record, then transcribe into the prompt"},
    {"/say", "speak it, or the last answer"},
    {"/run", "run a shell command, after you confirm"},
    {"/tools", "what the assistant can do besides talk"},
    {"/think", "expand or collapse reasoning"},
    {"/clear", "clear the conversation"},
    {"/?", "help"},
    {"/bye", "quit"},
};

std::vector<std::string> Words(const std::string& line) {
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ') {
            ++i;
        }
        if (i > start) {
            words.push_back(line.substr(start, i - start));
        }
    }
    return words;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

/// Replace the last word of `line` with `value`, keeping everything before it.
std::string ReplaceTail(const std::string& line, const std::string& value) {
    const std::size_t space = line.find_last_of(' ');
    return space == std::string::npos ? value : line.substr(0, space + 1) + value;
}

}  // namespace

std::vector<Completion> Suggest(const std::string& line) {
    std::vector<Completion> out;
    if (line.empty() || line.front() != '/') {
        return out;  // ordinary prose; stay out of the way
    }

    const std::vector<std::string> words = Words(line);
    const bool typing_new_word = !line.empty() && line.back() == ' ';
    const std::string tail = typing_new_word ? "" : (words.empty() ? "" : words.back());

    // First word: the command itself.
    if (words.size() <= 1 && !typing_new_word) {
        for (const Command& command : kCommands) {
            if (StartsWith(command.name, tail)) {
                out.push_back({std::string(command.name) + " ", command.name, command.detail});
            }
        }
        return out;
    }

    const std::string& command = words.front();

    if (command == "/load") {
        for (const sdk::LocalModel& model : sdk::LocalModels()) {
            if (StartsWith(model.id, tail)) {
                out.push_back({ReplaceTail(line, model.id), model.id, model.framework});
            }
        }
        return out;
    }

    if (command == "/pull") {
        for (const catalog::Model& model : catalog::All()) {
            if (!catalog::Installable(model)) {
                continue;  // offering one that cannot be fetched is a dead end
            }
            if (StartsWith(model.id, tail)) {
                out.push_back({ReplaceTail(line, std::string(model.id)), std::string(model.id),
                               catalog::HumanSize(model.bytes)});
            }
        }
        return out;
    }

    if (command == "/set") {
        if (words.size() <= 2 && !(typing_new_word && words.size() == 2)) {
            for (const settings::Setting& setting : settings::All()) {
                if (StartsWith(setting.name, tail)) {
                    out.push_back({ReplaceTail(line, setting.name) + " ", setting.name,
                                   setting.summary});
                }
            }
            return out;
        }
        const settings::Setting* setting = settings::Find(words[1]);
        if (setting != nullptr) {
            for (const std::string& value : setting->values) {
                if (StartsWith(value, tail)) {
                    out.push_back({ReplaceTail(line, value), value,
                                   value == setting->get() ? "current" : ""});
                }
            }
        }
        return out;
    }

    return out;
}

std::string CommonPrefix(const std::vector<Completion>& options) {
    if (options.empty()) {
        return {};
    }
    std::string prefix = options.front().label;
    for (const Completion& option : options) {
        std::size_t i = 0;
        while (i < prefix.size() && i < option.label.size() && prefix[i] == option.label[i]) {
            ++i;
        }
        prefix.resize(i);
    }
    return prefix;
}

}  // namespace rcli::chat
