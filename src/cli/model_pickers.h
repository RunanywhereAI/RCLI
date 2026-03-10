#pragma once
// =============================================================================
// RCLI CLI — Model pickers for LLM, STT, TTS
// =============================================================================
//
// Unified model management: dashboard, per-modality pickers, info display.
// DRY: shared helpers for table rendering, user input, and download prompts.
//
// =============================================================================

#include "cli/cli_common.h"
#include "models/model_registry.h"
#include "models/tts_model_registry.h"
#include "models/stt_model_registry.h"
#include "engines/metalrt_loader.h"

// =============================================================================
// Shared picker helpers — DRY across LLM / STT / TTS
// =============================================================================

// Read a picker choice from STDIN. Returns 0 for "no input / enter",
// -1 for q/Q (cancel), -2 for a/A (auto-detect), or 1..N for numeric choice.
inline int read_picker_choice() {
    char buf[32] = {};
    if (read(STDIN_FILENO, buf, sizeof(buf) - 1) <= 0 || buf[0] == '\n')
        return 0;
    if (buf[0] == 'q' || buf[0] == 'Q') return -1;
    if (buf[0] == 'a' || buf[0] == 'A') return -2;
    return atoi(buf);
}

// Ask Y/n confirmation. Returns true for yes (default).
inline bool confirm_download() {
    char yn[8] = {};
    if (read(STDIN_FILENO, yn, sizeof(yn) - 1) <= 0) yn[0] = 'y';
    if (yn[0] == '\n') yn[0] = 'y';
    return yn[0] == 'y' || yn[0] == 'Y';
}

// Print the "no changes / cancelled" feedback.
inline void picker_no_changes() { fprintf(stderr, "\n  No changes.\n\n"); }
inline void picker_cancelled()  { fprintf(stderr, "\n  Cancelled.\n\n"); }

// =============================================================================
// LLM picker
// =============================================================================

inline int pick_llm(const std::string& models_dir) {
    auto all = rcli::all_models();
    const auto* active = rcli::resolve_active_model(models_dir, all);
    std::string user_choice = rcli::read_selected_model_id();

    fprintf(stderr, "\n  %s%s  LLM Models%s", color::bold, color::orange, color::reset);
    if (!user_choice.empty())
        fprintf(stderr, "  (pinned: %s)", user_choice.c_str());
    else
        fprintf(stderr, "  (auto-detect)");
    fprintf(stderr, "\n\n");

    fprintf(stderr, "    %s#  %-28s  %-7s  %-10s  %-12s  %-13s  %s%s\n",
            color::bold, "Model", "Size", "Speed", "Tool Call", "Engine", "Status", color::reset);
    fprintf(stderr, "    %s──  %-28s  %-7s  %-10s  %-12s  %-13s  %s%s\n",
            color::dim, "────────────────────────────", "───────", "──────────", "────────────", "─────────────", "──────────", color::reset);

    for (size_t i = 0; i < all.size(); i++) {
        auto& m = all[i];
        std::string path = models_dir + "/" + m.filename;
        bool installed = (access(path.c_str(), R_OK) == 0);
        bool mrt_installed = rcli::is_metalrt_model_installed(m);
        bool is_active = active && active->id == m.id;
        std::string status;
        if (is_active)       status = "\033[32m* active\033[0m";
        else if (installed || mrt_installed) status = "installed";
        else                 status = "\033[2mnot installed\033[0m";
        std::string label = m.name;
        if (m.is_default) label += " (default)";
        if (m.is_recommended) label += " *";

        std::string eng_col = rcli::engine_label(m);
        std::string speed = m.metalrt_supported && !m.metalrt_speed_est.empty()
            ? m.metalrt_speed_est : m.speed_est;

        fprintf(stderr, "    %s%-2zu%s %-28s  %-7s  %-10s  %-12s  %-13s  %s\n",
                is_active ? "\033[32m" : "", i + 1, is_active ? "\033[0m" : "",
                label.c_str(), rcli::format_size(m.size_mb).c_str(),
                speed.c_str(), m.tool_calling.c_str(), eng_col.c_str(), status.c_str());
    }
    fprintf(stderr, "\n  %sCommands:%s  [1-%zu] select  |  a auto-detect  |  q cancel\n  Choice: ",
            color::bold, color::reset, all.size());
    fflush(stderr);

    int choice = read_picker_choice();
    if (choice == 0)  { picker_no_changes(); return 0; }
    if (choice == -1) { picker_cancelled(); return 0; }
    if (choice == -2) {
        rcli::clear_selected_model();
        fprintf(stderr, "\n  %s%sReverted to auto-detect.%s\n", color::bold, color::green, color::reset);
        const auto* best = rcli::find_best_installed(models_dir, all);
        if (best) fprintf(stderr, "  Will use: %s\n", best->name.c_str());
        fprintf(stderr, "  Restart RCLI to apply.\n\n");
        return 0;
    }
    if (choice < 1 || choice > (int)all.size()) { fprintf(stderr, "\n  Invalid choice.\n\n"); return 1; }

    auto& sel = all[choice - 1];
    std::string path = models_dir + "/" + sel.filename;
    if (access(path.c_str(), R_OK) != 0) {
        fprintf(stderr, "\n  %s%s%s%s is not installed (%s). Download? [Y/n]: ",
                color::bold, color::yellow, sel.name.c_str(), color::reset,
                rcli::format_size(sel.size_mb).c_str());
        fflush(stderr);
        if (!confirm_download()) { picker_cancelled(); return 0; }
        std::string cmd = "bash -c 'set -e; echo \"  Downloading " + sel.name + " (~" +
            rcli::format_size(sel.size_mb) + ")...\"; echo \"\"; curl -L -# -o \"" +
            path + "\" \"" + sel.url + "\"; echo \"\"; echo \"  Done!\"; '";
        fprintf(stderr, "\n");
        if (system(cmd.c_str()) != 0) {
            fprintf(stderr, "\n  %s%sDownload failed.%s\n\n", color::bold, color::red, color::reset);
            return 1;
        }
    }
    rcli::write_selected_model_id(sel.id);
    fprintf(stderr, "\n  %s%sSelected: %s%s\n  Restart RCLI to apply.\n\n",
            color::bold, color::green, sel.name.c_str(), color::reset);
    return 0;
}

// =============================================================================
// STT picker (offline models only — Zipformer streaming is always-on)
// =============================================================================

inline int pick_stt(const std::string& models_dir) {
    auto all = rcli::all_stt_models();
    const auto* active = rcli::resolve_active_stt(models_dir, all);
    std::string user_choice = rcli::read_selected_stt_id();
    auto offline = rcli::get_offline_stt_models(all);

    fprintf(stderr, "\n  %s%s  STT Models%s", color::bold, color::orange, color::reset);
    if (!user_choice.empty())
        fprintf(stderr, "  (pinned: %s)", user_choice.c_str());
    else
        fprintf(stderr, "  (auto-detect)");
    fprintf(stderr, "\n\n");

    const auto* zip = rcli::find_stt_by_id("zipformer", all);
    if (zip) {
        fprintf(stderr, "    %sStreaming (live mic):%s  %s%s%s (always active)\n\n",
                color::bold, color::reset, color::green, zip->name.c_str(), color::reset);
    }

    fprintf(stderr, "    %s#  %-28s  %-7s  %-12s  %s%s\n",
            color::bold, "Offline Model", "Size", "Accuracy", "Status", color::reset);
    fprintf(stderr, "    %s──  %-28s  %-7s  %-12s  %s%s\n",
            color::dim, "────────────────────────────", "───────", "────────────", "──────────", color::reset);

    for (size_t i = 0; i < offline.size(); i++) {
        auto& m = *offline[i];
        bool installed = rcli::is_stt_installed(models_dir, m);
        bool is_active = active && active->id == m.id;
        std::string status;
        if (is_active)       status = "\033[32m* active\033[0m";
        else if (installed)  status = "installed";
        else                 status = "\033[2mnot installed\033[0m";
        std::string label = m.name;
        if (m.is_default) label += " (default)";
        if (m.is_recommended) label += " *";
        fprintf(stderr, "    %s%-2zu%s %-28s  %-7s  %-12s  %s\n",
                is_active ? "\033[32m" : "", i + 1, is_active ? "\033[0m" : "",
                label.c_str(), rcli::format_size(m.size_mb).c_str(),
                m.accuracy.c_str(), status.c_str());
    }
    fprintf(stderr, "\n  %sCommands:%s  [1-%zu] select  |  a auto-detect  |  q cancel\n  Choice: ",
            color::bold, color::reset, offline.size());
    fflush(stderr);

    int choice = read_picker_choice();
    if (choice == 0)  { picker_no_changes(); return 0; }
    if (choice == -1) { picker_cancelled(); return 0; }
    if (choice == -2) {
        rcli::clear_selected_stt();
        fprintf(stderr, "\n  %s%sReverted to auto-detect.%s\n", color::bold, color::green, color::reset);
        const auto* best = rcli::find_best_installed_stt(models_dir, all);
        if (best) fprintf(stderr, "  Will use: %s\n", best->name.c_str());
        fprintf(stderr, "  Restart RCLI to apply.\n\n");
        return 0;
    }
    if (choice < 1 || choice > (int)offline.size()) { fprintf(stderr, "\n  Invalid choice.\n\n"); return 1; }

    auto& sel = *offline[choice - 1];
    bool installed = rcli::is_stt_installed(models_dir, sel);

    if (!installed) {
        if (sel.download_url.empty()) {
            fprintf(stderr, "\n  %s%s%s%s is bundled with setup. Run %srcli setup%s first.\n\n",
                    color::bold, color::yellow, sel.name.c_str(), color::reset, color::bold, color::reset);
            return 1;
        }
        fprintf(stderr, "\n  %s%s%s%s is not installed (%s). Download? [Y/n]: ",
                color::bold, color::yellow, sel.name.c_str(), color::reset,
                rcli::format_size(sel.size_mb).c_str());
        fflush(stderr);
        if (!confirm_download()) { picker_cancelled(); return 0; }

        std::string archive = "/tmp/" + sel.dir_name + ".tar.bz2";
        std::string extract_dir = sel.archive_dir.empty() ? sel.dir_name : sel.archive_dir;
        std::string cmd = "bash -c '"
            "set -e; "
            "echo \"  Downloading " + sel.name + " (~" + rcli::format_size(sel.size_mb) + ")...\"; "
            "echo \"\"; "
            "curl -L -# -o \"" + archive + "\" \"" + sel.download_url + "\"; "
            "echo \"\"; echo \"  Extracting...\"; "
            "cd /tmp && tar xjf \"" + archive + "\"; "
            "mkdir -p \"" + models_dir + "/" + sel.dir_name + "\"; "
            "cp /tmp/" + extract_dir + "/*.onnx \"" + models_dir + "/" + sel.dir_name + "/\"; "
            "cp /tmp/" + extract_dir + "/tokens.txt \"" + models_dir + "/" + sel.dir_name + "/\"; "
            "rm -rf /tmp/" + extract_dir + " \"" + archive + "\"; "
            "echo \"  Done!\"; '";
        fprintf(stderr, "\n");
        if (system(cmd.c_str()) != 0) {
            fprintf(stderr, "\n  %s%sDownload failed.%s\n\n", color::bold, color::red, color::reset);
            return 1;
        }
    }
    rcli::write_selected_stt_id(sel.id);
    fprintf(stderr, "\n  %s%sSelected: %s%s\n  Restart RCLI to apply.\n\n",
            color::bold, color::green, sel.name.c_str(), color::reset);
    return 0;
}

// =============================================================================
// TTS picker
// =============================================================================

inline int pick_tts(const std::string& models_dir) {
    auto all = rcli::all_tts_models();
    const auto* active = rcli::resolve_active_tts(models_dir, all);
    std::string user_choice = rcli::read_selected_tts_id();

    fprintf(stderr, "\n  %s%s  TTS Voices%s", color::bold, color::orange, color::reset);
    if (!user_choice.empty())
        fprintf(stderr, "  (pinned: %s)", user_choice.c_str());
    else
        fprintf(stderr, "  (auto-detect)");
    fprintf(stderr, "\n\n");

    fprintf(stderr, "    %s#  %-30s  %-8s  %-8s  %-10s  %-10s  %s%s\n",
            color::bold, "Voice", "Size", "Arch", "Quality", "Speakers", "Status", color::reset);
    fprintf(stderr, "    %s──  %-30s  %-8s  %-8s  %-10s  %-10s  %s%s\n",
            color::dim, "──────────────────────────────", "────────", "────────",
            "──────────", "──────────", "──────────", color::reset);

    for (size_t i = 0; i < all.size(); i++) {
        auto& v = all[i];
        bool installed = rcli::is_tts_installed(models_dir, v);
        bool is_active = active && active->id == v.id;
        std::string status;
        if (is_active)       status = "\033[32m* active\033[0m";
        else if (installed)  status = "installed";
        else                 status = "\033[2mnot installed\033[0m";
        std::string label = v.name;
        if (v.is_default) label += " (default)";
        if (v.is_recommended) label += " *";
        char spk[16]; snprintf(spk, sizeof(spk), "%d", v.num_speakers);
        fprintf(stderr, "    %s%-2zu%s %-30s  %-8s  %-8s  %-10s  %-10s  %s\n",
                is_active ? "\033[32m" : "", i + 1, is_active ? "\033[0m" : "",
                label.c_str(), rcli::format_size(v.size_mb).c_str(),
                v.architecture.c_str(), v.quality.c_str(), spk, status.c_str());
    }
    fprintf(stderr, "\n  %sCommands:%s  [1-%zu] select  |  a auto-detect  |  q cancel\n  Choice: ",
            color::bold, color::reset, all.size());
    fflush(stderr);

    int choice = read_picker_choice();
    if (choice == 0)  { picker_no_changes(); return 0; }
    if (choice == -1) { picker_cancelled(); return 0; }
    if (choice == -2) {
        rcli::clear_selected_tts();
        fprintf(stderr, "\n  %s%sReverted to auto-detect.%s\n", color::bold, color::green, color::reset);
        const auto* best = rcli::find_best_installed_tts(models_dir, all);
        if (best) fprintf(stderr, "  Will use: %s (%s)\n", best->name.c_str(), best->architecture.c_str());
        fprintf(stderr, "  Restart RCLI to apply.\n\n");
        return 0;
    }
    if (choice < 1 || choice > (int)all.size()) { fprintf(stderr, "\n  Invalid choice.\n\n"); return 1; }

    auto& sel = all[choice - 1];
    bool installed = rcli::is_tts_installed(models_dir, sel);
    if (!installed) {
        if (sel.download_url.empty()) {
            fprintf(stderr, "\n  %s%s%s%s is bundled with setup. Run %srcli setup%s first.\n\n",
                    color::bold, color::yellow, sel.name.c_str(), color::reset, color::bold, color::reset);
            return 1;
        }
        fprintf(stderr, "\n  %s%s%s%s is not installed (%s). Download? [Y/n]: ",
                color::bold, color::yellow, sel.name.c_str(), color::reset,
                rcli::format_size(sel.size_mb).c_str());
        fflush(stderr);
        if (!confirm_download()) { picker_cancelled(); return 0; }
        std::string archive = models_dir + "/" + sel.dir_name + ".tar.bz2";
        std::string extract_name = sel.archive_dir.empty() ? sel.dir_name : sel.archive_dir;
        std::string rename_step;
        if (!sel.archive_dir.empty() && sel.archive_dir != sel.dir_name) {
            rename_step = "if [ -d '" + models_dir + "/" + extract_name + "' ] && "
                "[ ! -d '" + models_dir + "/" + sel.dir_name + "' ]; then "
                "mv '" + models_dir + "/" + extract_name + "' '" + models_dir + "/" + sel.dir_name + "'; fi; ";
        }
        std::string cmd = "bash -c '"
            "set -e; echo \"  Downloading " + sel.name + " (~" + rcli::format_size(sel.size_mb) +
            ")...\"; echo \"\"; curl -L -# -o \"" + archive + "\" \"" + sel.download_url +
            "\"; echo \"\"; echo \"  Extracting...\"; cd \"" + models_dir +
            "\" && tar xjf \"" + archive + "\"; " + rename_step +
            "rm -f \"" + archive + "\"; echo \"  Done!\"; '";
        fprintf(stderr, "\n");
        if (system(cmd.c_str()) != 0) {
            fprintf(stderr, "\n  %s%sDownload failed.%s\n\n", color::bold, color::red, color::reset);
            return 1;
        }
    }
    rcli::write_selected_tts_id(sel.id);
    fprintf(stderr, "\n  %s%sSelected: %s%s (%s, %d speakers)\n  Restart RCLI to apply.\n\n",
            color::bold, color::green, sel.name.c_str(), color::reset,
            sel.architecture.c_str(), sel.num_speakers);
    return 0;
}

// =============================================================================
// MetalRT Whisper STT picker
// =============================================================================

inline int pick_metalrt_stt() {
    auto comps = rcli::metalrt_component_models();
    std::string current = rcli::read_selected_metalrt_stt_id();

    std::vector<const rcli::MetalRTComponentModel*> stt_models;
    for (auto& cm : comps) {
        if (cm.component == "stt") stt_models.push_back(&cm);
    }

    fprintf(stderr, "\n  %s%s  MetalRT Whisper STT Models%s", color::bold, color::orange, color::reset);
    if (!current.empty())
        fprintf(stderr, "  (selected: %s)", current.c_str());
    else
        fprintf(stderr, "  (auto — picks first installed)");
    fprintf(stderr, "\n\n");

    fprintf(stderr, "    %s#  %-30s  %-7s  %-40s  %s%s\n",
            color::bold, "Model", "Size", "Description", "Status", color::reset);
    fprintf(stderr, "    %s──  %-30s  %-7s  %-40s  %s%s\n",
            color::dim, "──────────────────────────────", "───────",
            "────────────────────────────────────────", "──────────", color::reset);

    bool found_active = false;
    for (size_t i = 0; i < stt_models.size(); i++) {
        auto& m = *stt_models[i];
        bool installed = rcli::is_metalrt_component_installed(m);
        bool is_active = false;
        if (!current.empty()) {
            is_active = (m.id == current && installed);
        } else if (installed && !found_active) {
            is_active = true;  // Auto mode: first installed wins
            found_active = true;
        }
        std::string status;
        if (is_active && installed) status = "\033[32m* active\033[0m";
        else if (installed)         status = "installed";
        else                        status = "\033[2mnot installed\033[0m";

        char size_str[16];
        if (m.size_mb >= 1000)
            snprintf(size_str, sizeof(size_str), "%.1fG", m.size_mb / 1000.0);
        else
            snprintf(size_str, sizeof(size_str), "%dM", m.size_mb);

        fprintf(stderr, "    %s%-2zu%s %-30s  %-7s  %-40s  %s\n",
                is_active ? "\033[32m" : "", i + 1, is_active ? "\033[0m" : "",
                m.name.c_str(), size_str, m.description.c_str(), status.c_str());
    }

    fprintf(stderr, "\n  %sCommands:%s  [1-%zu] select  |  a auto-detect  |  q cancel\n  Choice: ",
            color::bold, color::reset, stt_models.size());
    fflush(stderr);

    int choice = read_picker_choice();
    if (choice == 0)  { picker_no_changes(); return 0; }
    if (choice == -1) { picker_cancelled(); return 0; }
    if (choice == -2) {
        rcli::write_selected_metalrt_stt_id("");
        fprintf(stderr, "\n  %s%sReverted to auto-detect.%s\n  Restart RCLI to apply.\n\n",
                color::bold, color::green, color::reset);
        return 0;
    }
    if (choice < 1 || choice > (int)stt_models.size()) {
        fprintf(stderr, "\n  Invalid choice.\n\n");
        return 1;
    }

    auto& sel = *stt_models[choice - 1];
    if (!rcli::is_metalrt_component_installed(sel)) {
        fprintf(stderr, "\n  %s%s%s%s is not installed. Install with: rcli setup --metalrt%s\n\n",
                color::bold, color::yellow, sel.name.c_str(), color::reset, "");
        return 1;
    }

    rcli::write_selected_metalrt_stt_id(sel.id);
    fprintf(stderr, "\n  %s%sSelected: %s%s\n  Restart RCLI to apply.\n\n",
            color::bold, color::green, sel.name.c_str(), color::reset);
    return 0;
}

// =============================================================================
// Unified models dashboard
// =============================================================================

inline int cmd_models(const Args& args) {
    std::string models_dir = args.models_dir;

    if (args.arg1 == "llm") return pick_llm(models_dir);
    if (args.arg1 == "stt") return pick_stt(models_dir);
    if (args.arg1 == "tts") return pick_tts(models_dir);
    if (args.arg1 == "metalrt-stt" || args.arg1 == "whisper") return pick_metalrt_stt();

    if (args.help) {
        fprintf(stderr,
            "\n%s%s  rcli models%s  —  Manage all AI models\n\n"
            "  %sSUBCOMMANDS%s\n"
            "    models              Unified model dashboard\n"
            "    models llm          LLM model picker\n"
            "    models stt          STT model picker\n"
            "    models tts          TTS voice picker\n\n"
            "  %sEXAMPLES%s\n"
            "    rcli models              # dashboard — pick a modality\n"
            "    rcli models llm          # switch LLM directly\n"
            "    rcli models stt          # switch offline STT directly\n"
            "    rcli models tts          # switch TTS voice directly\n\n",
            color::bold, color::orange, color::reset,
            color::bold, color::reset,
            color::bold, color::reset);
        return 0;
    }

    auto llm_all = rcli::all_models();
    auto stt_all = rcli::all_stt_models();
    auto tts_all = rcli::all_tts_models();

    const auto* llm_active = rcli::resolve_active_model(models_dir, llm_all);
    const auto* stt_active = rcli::resolve_active_stt(models_dir, stt_all);
    const auto* tts_active = rcli::resolve_active_tts(models_dir, tts_all);

    std::string llm_name = llm_active ? llm_active->name : "Qwen3 0.6B";
    std::string stt_name = stt_active ? stt_active->name : "Whisper base.en";
    std::string tts_name = tts_active ? tts_active->name : "Piper Lessac";

    int llm_inst = 0;
    for (auto& m : llm_all) {
        std::string p = models_dir + "/" + m.filename;
        if (access(p.c_str(), R_OK) == 0) llm_inst++;
    }
    int stt_inst = 0;
    for (auto& m : stt_all) {
        if (m.category == "offline" && rcli::is_stt_installed(models_dir, m)) stt_inst++;
    }
    int tts_inst = 0;
    for (auto& m : tts_all) {
        if (rcli::is_tts_installed(models_dir, m)) tts_inst++;
    }

    fprintf(stderr, "\n%s%s  RCLI Models%s\n\n", color::bold, color::orange, color::reset);

    fprintf(stderr, "    %s#  Modality       Active Model                  Installed  Available%s\n",
            color::bold, color::reset);
    fprintf(stderr, "    %s──  ─────────────  ────────────────────────────  ─────────  ────────%s\n",
            color::dim, color::reset);
    fprintf(stderr, "    %s1%s  %sLLM%s            %s%-28s%s  %d / %zu\n",
            color::green, color::reset, color::bold, color::reset,
            color::green, llm_name.c_str(), color::reset,
            llm_inst, llm_all.size());
    fprintf(stderr, "    %s2%s  %sSTT (offline)%s  %s%-28s%s  %d / %zu\n",
            color::green, color::reset, color::bold, color::reset,
            color::green, stt_name.c_str(), color::reset,
            stt_inst, rcli::get_offline_stt_models(stt_all).size());
    fprintf(stderr, "    %s3%s  %sTTS%s            %s%-28s%s  %d / %zu\n",
            color::green, color::reset, color::bold, color::reset,
            color::green, tts_name.c_str(), color::reset,
            tts_inst, tts_all.size());

    // MetalRT Whisper row
    auto mrt_comps = rcli::metalrt_component_models();
    std::string mrt_stt_pref = rcli::read_selected_metalrt_stt_id();
    std::string mrt_stt_name = "auto";
    int mrt_stt_inst = 0;
    int mrt_stt_total = 0;
    for (auto& cm : mrt_comps) {
        if (cm.component != "stt") continue;
        mrt_stt_total++;
        if (rcli::is_metalrt_component_installed(cm)) {
            mrt_stt_inst++;
            if (cm.id == mrt_stt_pref) mrt_stt_name = cm.name;
        }
    }
    if (mrt_stt_pref.empty() && mrt_stt_inst > 0) mrt_stt_name = "auto (first installed)";
    fprintf(stderr, "    %s4%s  %sMetalRT STT%s    %s%-28s%s  %d / %d\n",
            color::green, color::reset, color::bold, color::reset,
            color::green, mrt_stt_name.c_str(), color::reset,
            mrt_stt_inst, mrt_stt_total);

    // Show recommendation notes from the registries
    fprintf(stderr, "\n");
    for (auto& m : llm_all) {
        if (m.is_recommended) {
            fprintf(stderr, "  %sTip: Recommended LLM: %s -- %s%s\n",
                    color::dim, m.name.c_str(), m.description.c_str(), color::reset);
            break;
        }
    }
    for (auto& m : tts_all) {
        if (m.is_recommended) {
            fprintf(stderr, "  %sTip: Recommended TTS: %s -- %s%s\n",
                    color::dim, m.name.c_str(), m.description.c_str(), color::reset);
            break;
        }
    }
    fprintf(stderr, "  %sNote: STT streaming (Zipformer) is always active for live mic.%s\n\n",
            color::dim, color::reset);
    fprintf(stderr, "  %sSelect modality:%s  1 LLM  |  2 STT  |  3 TTS  |  4 MetalRT STT  |  q cancel\n  Choice: ",
            color::bold, color::reset);
    fflush(stderr);

    int choice = read_picker_choice();
    if (choice == 0 || choice == -1) { picker_no_changes(); return 0; }
    if (choice == 1 || choice == -2) return pick_llm(models_dir); // -2 (a) → LLM as first
    if (choice == 2) return pick_stt(models_dir);
    if (choice == 3) return pick_tts(models_dir);
    if (choice == 4) return pick_metalrt_stt();

    fprintf(stderr, "\n  Invalid choice.\n\n");
    return 1;
}

inline int cmd_voices(const Args& args) {
    if (args.help) {
        fprintf(stderr,
            "\n%s%s  rcli voices%s  —  Manage TTS voices\n\n"
            "  Interactive picker to list, download, and switch TTS voices.\n"
            "  Same as: rcli models tts\n\n",
            color::bold, color::orange, color::reset);
        return 0;
    }
    return pick_tts(args.models_dir);
}

inline int cmd_stt_picker(const Args& args) {
    if (args.help) {
        fprintf(stderr,
            "\n%s%s  rcli stt%s  —  Manage STT models\n\n"
            "  Interactive picker to list, download, and switch offline STT models.\n"
            "  Same as: rcli models stt\n\n",
            color::bold, color::orange, color::reset);
        return 0;
    }
    return pick_stt(args.models_dir);
}

// =============================================================================
// Info — show engine info and all installed models
// =============================================================================

inline int cmd_info() {
    ActionRegistry registry;
    registry.register_defaults();

    std::string models_dir = default_models_dir();
    bool has_models = models_exist(models_dir);

    auto all = rcli::all_models();
    const auto* active_llm = rcli::resolve_active_model(models_dir, all);
    std::string llm_info = active_llm
        ? (active_llm->name + " (Q4_K_M, Metal GPU)")
        : "Qwen3 0.6B (Q4_K_M, Metal GPU)";

    auto tts_all = rcli::all_tts_models();
    const auto* active_tts = rcli::resolve_active_tts(models_dir, tts_all);
    std::string tts_info = active_tts
        ? (active_tts->name + " (" + active_tts->architecture + ")")
        : "Piper Lessac (vits)";

    auto stt_all = rcli::all_stt_models();
    const auto* active_stt = rcli::resolve_active_stt(models_dir, stt_all);
    std::string stt_info = "Zipformer (streaming) + " +
        (active_stt ? (active_stt->name + " (offline)") : std::string("Whisper base.en (offline)"));

    std::string engine_pref = rcli::read_engine_preference();
    bool mrt_available = rastack::MetalRTLoader::instance().is_available();
    bool use_metalrt = (engine_pref == "metalrt" && mrt_available)
                    || ((engine_pref == "auto" || engine_pref.empty()) && mrt_available);
    std::string engine_info = use_metalrt
        ? "MetalRT (Metal GPU — LLM, STT, TTS on-device)"
        : "llama.cpp + sherpa-onnx (ONNX Runtime)";

    fprintf(stdout,
        "\n%s%s  RCLI%s %s%s%s\n\n"
        "  %sEngine:%s       %s\n"
        "  %sLLM:%s          %s\n"
        "  %sSTT:%s          %s\n"
        "  %sTTS:%s          %s\n"
        "  %sVAD:%s          Silero VAD\n"
        "  %sActions:%s      %d macOS actions\n"
        "  %sRAG:%s          USearch HNSW + BM25 hybrid retrieval\n"
        "  %sOn-device:%s    100%% local, zero cloud dependency\n"
        "  %sModels:%s       %s%s%s (%s)\n\n",
        color::bold, color::orange, color::reset,
        color::dim, RA_VERSION, color::reset,
        color::bold, color::reset, engine_info.c_str(),
        color::bold, color::reset, llm_info.c_str(),
        color::bold, color::reset, stt_info.c_str(),
        color::bold, color::reset, tts_info.c_str(),
        color::bold, color::reset,
        color::bold, color::reset, registry.num_actions(),
        color::bold, color::reset,
        color::bold, color::reset,
        color::bold, color::reset,
        has_models ? color::green : color::red,
        has_models ? "installed" : "not found",
        color::reset,
        models_dir.c_str());

    // Installed LLMs
    fprintf(stdout, "  %sInstalled LLMs:%s\n", color::bold, color::reset);
    bool any_llm = false;
    for (auto& m : all) {
        std::string path = models_dir + "/" + m.filename;
        if (access(path.c_str(), R_OK) == 0) {
            bool is_active = active_llm && active_llm->id == m.id;
            fprintf(stdout, "    %s%-28s  %-7s  %s%s%s\n",
                    is_active ? "\033[32m" : "",
                    m.name.c_str(), rcli::format_size(m.size_mb).c_str(),
                    is_active ? "active" : "installed",
                    is_active ? "\033[0m" : "", "");
            any_llm = true;
        }
    }
    if (!any_llm) fprintf(stdout, "    (none — run: rcli setup)\n");
    fprintf(stdout, "\n");

    // Installed STT
    fprintf(stdout, "  %sInstalled STT:%s\n", color::bold, color::reset);
    bool any_stt = false;
    for (auto& m : stt_all) {
        if (rcli::is_stt_installed(models_dir, m)) {
            bool is_active = (m.category == "streaming") ||
                             (active_stt && active_stt->id == m.id);
            fprintf(stdout, "    %s%-28s  %-7s  %-12s  %s%s%s\n",
                    is_active ? "\033[32m" : "",
                    m.name.c_str(), rcli::format_size(m.size_mb).c_str(),
                    m.category == "streaming" ? "always on" : m.accuracy.c_str(),
                    is_active ? "active" : "installed",
                    is_active ? "\033[0m" : "", "");
            any_stt = true;
        }
    }
    if (!any_stt) fprintf(stdout, "    (none — run: rcli setup)\n");
    fprintf(stdout, "\n");

    // Installed TTS
    fprintf(stdout, "  %sInstalled Voices:%s\n", color::bold, color::reset);
    bool any_tts = false;
    for (auto& v : tts_all) {
        if (rcli::is_tts_installed(models_dir, v)) {
            bool is_active = active_tts && active_tts->id == v.id;
            char spk[16]; snprintf(spk, sizeof(spk), "%d spk", v.num_speakers);
            fprintf(stdout, "    %s%-28s  %-8s  %-8s  %s%s%s\n",
                    is_active ? "\033[32m" : "",
                    v.name.c_str(), v.architecture.c_str(), spk,
                    is_active ? "active" : "installed",
                    is_active ? "\033[0m" : "", "");
            any_tts = true;
        }
    }
    if (!any_tts) fprintf(stdout, "    (none — run: rcli setup)\n");
    fprintf(stdout, "\n");

    return 0;
}
