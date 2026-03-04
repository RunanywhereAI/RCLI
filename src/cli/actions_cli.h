#pragma once
// =============================================================================
// RCLI CLI — Actions display and execution
// =============================================================================

#include "cli/cli_common.h"

inline void print_actions_rich(const ActionRegistry& registry) {
    auto defs = registry.get_all_defs();

    fprintf(stdout, "\n%s%s  RCLI Actions%s (%d available)\n",
            color::bold, color::orange, color::reset, registry.num_actions());

    std::string current_category;
    for (auto& def : defs) {
        std::string cat = def.category;
        if (!cat.empty()) cat[0] = static_cast<char>(toupper(static_cast<unsigned char>(cat[0])));

        if (cat != current_category) {
            current_category = cat;
            fprintf(stdout, "\n  %s%s%s%s\n", color::bold, color::yellow, cat.c_str(), color::reset);
        }

        fprintf(stdout, "    %s%-20s%s %s%s%s\n",
                color::green, def.name.c_str(), color::reset,
                color::dim, def.description.c_str(), color::reset);
    }

    fprintf(stdout, "\n  %s%sExamples (voice):%s\n", color::bold, color::orange, color::reset);
    fprintf(stdout, "    rcli ask \"create a note called Meeting Notes with today's agenda\"\n");
    fprintf(stdout, "    rcli ask \"send a message to John saying I'll be late\"\n");
    fprintf(stdout, "    rcli ask \"open Safari\"\n");
    fprintf(stdout, "    rcli ask \"what's on my calendar today?\"\n");

    fprintf(stdout, "\n  %s%sExamples (direct):%s\n", color::bold, color::orange, color::reset);
    fprintf(stdout, "    rcli action create_note '{\"title\": \"Test\", \"body\": \"Hello\"}'\n");
    fprintf(stdout, "    rcli action open_app '{\"app\": \"Safari\"}'\n");

    fprintf(stdout, "\n  %sTip:%s Run %srcli actions <name>%s for details on any action.\n\n",
            color::dim, color::reset, color::bold, color::reset);
}

inline void print_action_detail(const ActionDef& def) {
    fprintf(stdout, "\n  %s%s%s%s  —  %s\n",
            color::bold, color::green, def.name.c_str(), color::reset,
            def.description.c_str());

    fprintf(stdout, "\n  %sParameters:%s\n", color::bold, color::reset);
    fprintf(stdout, "    %s%s%s\n", color::dim, def.parameters_json.c_str(), color::reset);

    if (!def.example_cli.empty()) {
        fprintf(stdout, "\n  %sDirect:%s\n", color::bold, color::reset);
        fprintf(stdout, "    %s\n", def.example_cli.c_str());
    }

    if (!def.example_voice.empty()) {
        fprintf(stdout, "\n  %sVoice:%s\n", color::bold, color::reset);
        fprintf(stdout, "    \"%s\"\n", def.example_voice.c_str());
        fprintf(stdout, "\n  %sOr via ask:%s\n", color::bold, color::reset);
        fprintf(stdout, "    rcli ask \"%s\"\n", def.example_voice.c_str());
    }

    fprintf(stdout, "\n");
}

inline void print_actions_interactive() {
    ActionRegistry registry;
    registry.register_defaults();

    auto defs = registry.get_all_defs();
    std::string current_category;

    fprintf(stderr, "\n%s%s  Available Actions (%d)%s\n",
            color::bold, color::orange, registry.num_actions(), color::reset);

    for (auto& def : defs) {
        std::string cat = def.category;
        if (!cat.empty()) cat[0] = static_cast<char>(toupper(static_cast<unsigned char>(cat[0])));
        if (cat != current_category) {
            current_category = cat;
            fprintf(stderr, "\n  %s%s%s%s\n", color::bold, color::yellow, cat.c_str(), color::reset);
        }
        fprintf(stderr, "    %s%-20s%s %s%s%s\n",
                color::green, def.name.c_str(), color::reset,
                color::dim, def.description.c_str(), color::reset);
    }
    fprintf(stderr, "\n  Use %sdo <action> {json}%s to execute directly.\n\n", color::bold, color::reset);
}

inline int cmd_actions(const Args& args) {
    ActionRegistry registry;
    registry.register_defaults();

    if (args.help) {
        fprintf(stderr,
            "\n%s%s  rcli actions%s [name]  —  Explore available actions\n\n"
            "  With no argument, lists all actions grouped by category.\n"
            "  With a name, shows parameters, examples, and voice phrasing.\n\n"
            "%s  EXAMPLES%s\n"
            "    rcli actions                # list all\n"
            "    rcli actions create_note    # detail view\n"
            "    rcli actions open_app       # detail view\n\n",
            color::bold, color::orange, color::reset,
            color::bold, color::reset);
        return 0;
    }

    if (!args.arg1.empty()) {
        const ActionDef* def = registry.get_def(args.arg1);
        if (!def) {
            fprintf(stderr, "%s%sUnknown action: %s%s\n\n", color::bold, color::red, args.arg1.c_str(), color::reset);
            fprintf(stderr, "Run %srcli actions%s to see all available actions.\n\n", color::bold, color::reset);
            return 1;
        }
        print_action_detail(*def);
        return 0;
    }

    print_actions_rich(registry);
    return 0;
}

inline int cmd_action(const Args& args) {
    if (args.arg1.empty() || args.help) {
        fprintf(stderr,
            "\n%s%s  rcli action%s <name> [json_args]\n\n"
            "  Execute a macOS action directly with JSON parameters.\n\n"
            "%s  EXAMPLES%s\n"
            "    rcli action open_app '{\"app\": \"Safari\"}'\n"
            "    rcli action create_note '{\"title\": \"Test\", \"body\": \"Hello\"}'\n"
            "    rcli action set_volume '{\"level\": \"50\"}'\n"
            "    rcli action screenshot '{}'\n\n"
            "  Run %srcli actions%s to see all available actions.\n\n",
            color::bold, color::orange, color::reset,
            color::bold, color::reset,
            color::bold, color::reset);
        return args.help ? 0 : 1;
    }

    ActionRegistry registry;
    registry.register_defaults();

    const ActionDef* def = registry.get_def(args.arg1);
    if (!def) {
        fprintf(stderr, "%s%sUnknown action: %s%s\n\n", color::bold, color::red, args.arg1.c_str(), color::reset);
        fprintf(stderr, "Run %srcli actions%s to see available actions.\n\n", color::bold, color::reset);
        return 1;
    }

    std::string action_args = args.arg2.empty() ? "{}" : args.arg2;
    auto result = registry.execute(args.arg1, action_args);

    if (result.success) {
        fprintf(stdout, "%s%s%s\n", color::green, result.output.c_str(), color::reset);
    } else {
        fprintf(stderr, "%s%sError: %s%s\n", color::bold, color::red, result.error.c_str(), color::reset);
        return 1;
    }
    return 0;
}
