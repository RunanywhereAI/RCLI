#pragma once
// =============================================================================
// RCLI CLI — Setup and model upgrade commands
// =============================================================================

#include "cli/cli_common.h"
#include "models/model_registry.h"
#include "models/tts_model_registry.h"
#include "models/stt_model_registry.h"
#include "engines/metalrt_loader.h"
#include <dirent.h>

inline int cmd_setup(const Args& args) {
    if (args.help) {
        fprintf(stderr,
            "\n%s%s  rcli setup%s  —  Download AI models\n\n"
            "  Downloads ~1GB of models to ~/Library/RCLI/models/:\n"
            "    - Liquid LFM2 1.2B Tool LLM (731MB)\n"
            "    - Zipformer streaming STT (~50MB)\n"
            "    - Whisper base.en offline STT (~140MB)\n"
            "    - Piper TTS voice (~60MB)\n"
            "    - Silero VAD (~1MB)\n"
            "    - Snowflake Arctic Embed S (RAG, ~34MB)\n\n",
            color::bold, color::orange, color::reset);
        return 0;
    }

    std::string models_dir = args.models_dir;
    fprintf(stderr, "\n%s%s  RCLI Setup%s\n\n", color::bold, color::orange, color::reset);

    if (models_exist(models_dir)) {
        fprintf(stderr, "  %s%sModels already downloaded%s at %s\n\n", color::bold, color::green, color::reset, models_dir.c_str());
        fprintf(stderr, "  You're ready to go! Try:\n");
        fprintf(stderr, "    rcli              # interactive mode\n");
        fprintf(stderr, "    rcli listen       # voice mode\n");
        fprintf(stderr, "    rcli actions      # see what's possible\n\n");
        return 0;
    }

    // --- Engine choice ---
    fprintf(stderr, "  Choose your inference engine:\n\n");
    fprintf(stderr, "  %s1%s  %sOpen Source%s (llama.cpp + sherpa-onnx)              ~1 GB\n",
            color::bold, color::reset, color::green, color::reset);
    fprintf(stderr, "     Community-maintained, all models supported.\n");
    fprintf(stderr, "     Speed: ~180 tok/s (LFM2 1.2B)\n\n");
    fprintf(stderr, "  %s2%s  %sMetalRT%s (Apple Silicon GPU acceleration)           ~1.5 GB\n",
            color::bold, color::reset, color::cyan, color::reset);
    fprintf(stderr, "     Closed-source engine optimized for Metal GPU.\n");
    fprintf(stderr, "     Speed: ~486 tok/s (Qwen3 0.6B), ~180 tok/s (Qwen3 4B)\n");
    fprintf(stderr, "     Supports: Qwen3, Llama 3.2, LFM2.5\n\n");
    fprintf(stderr, "  %s3%s  %sBoth%s (recommended)                                 ~2.5 GB\n",
            color::bold, color::reset, color::orange, color::reset);
    fprintf(stderr, "     Install both engines. Use MetalRT when available,\n");
    fprintf(stderr, "     fall back to llama.cpp for unsupported models.\n\n");
    fprintf(stderr, "  Enter choice [1-3]: ");
    fflush(stderr);

    char engine_buf[16] = {};
    if (read(STDIN_FILENO, engine_buf, sizeof(engine_buf) - 1) <= 0) engine_buf[0] = '3';
    if (engine_buf[0] == '\n') engine_buf[0] = '3';
    int engine_choice = engine_buf[0] - '0';
    if (engine_choice < 1 || engine_choice > 3) engine_choice = 3;

    bool install_llamacpp = (engine_choice == 1 || engine_choice == 3);
    bool install_metalrt  = (engine_choice == 2 || engine_choice == 3);

    std::string engine_pref = "auto";
    if (engine_choice == 1) engine_pref = "llamacpp";
    if (engine_choice == 2) engine_pref = "metalrt";
    rcli::write_engine_preference(engine_pref);

    fprintf(stderr, "\n");

    // Install MetalRT binary if requested
    if (install_metalrt) {
        fprintf(stderr, "  %sInstalling MetalRT engine...%s\n", color::dim, color::reset);
        if (!rastack::MetalRTLoader::install()) {
            fprintf(stderr, "  %s%sMetalRT installation failed.%s Continuing with llama.cpp...\n",
                    color::bold, color::yellow, color::reset);
            if (!install_llamacpp) {
                install_llamacpp = true;
                rcli::write_engine_preference("llamacpp");
            }
        } else {
            fprintf(stderr, "  %s%sMetalRT installed!%s\n\n", color::bold, color::green, color::reset);

            // Download default MetalRT LLM weights (Qwen3-0.6B)
            auto all = rcli::all_models();
            for (auto& m : all) {
                if (m.metalrt_id == "metalrt-qwen3-0.6b") {
                    std::string mrt_dir = rcli::metalrt_models_dir() + "/" + m.metalrt_dir_name;
                    fprintf(stderr, "  %sDownloading MetalRT LLM: %s...%s\n", color::dim, m.name.c_str(), color::reset);
                    std::string config_url = m.metalrt_url;
                    auto pos = config_url.rfind("model.safetensors");
                    if (pos != std::string::npos) config_url.replace(pos, 17, "config.json");
                    std::string dl_cmd = "bash -c '"
                        "set -e; mkdir -p \"" + mrt_dir + "\"; "
                        "curl -fL -# -o \"" + mrt_dir + "/model.safetensors\" \"" + m.metalrt_url + "\"; "
                        "curl -fL -# -o \"" + mrt_dir + "/tokenizer.json\" \"" + m.metalrt_tokenizer_url + "\"; "
                        "curl -fL -# -o \"" + mrt_dir + "/config.json\" \"" + config_url + "\"; "
                        "'";
                    if (system(dl_cmd.c_str()) != 0) {
                        fprintf(stderr, "  %s%sMetalRT LLM download failed.%s\n",
                                color::bold, color::yellow, color::reset);
                    }
                    break;
                }
            }

            // Download MetalRT STT/TTS component models (Whisper + Kokoro)
            auto comp = rcli::metalrt_component_models();
            for (auto& cm : comp) {
                std::string cm_dir = rcli::metalrt_models_dir() + "/" + cm.dir_name;
                if (rcli::is_metalrt_component_installed(cm)) continue;

                std::string type_label = (cm.component == "stt") ? "STT" : "TTS";
                fprintf(stderr, "  %sDownloading MetalRT %s: %s (~%s)...%s\n",
                        color::dim, type_label.c_str(), cm.name.c_str(),
                        rcli::format_size(cm.size_mb).c_str(), color::reset);

                std::string hf_base = "https://huggingface.co/" + cm.hf_repo + "/resolve/main/";
                std::string subdir = cm.hf_subdir.empty() ? "" : cm.hf_subdir + "/";

                if (cm.component == "tts") {
                    std::string dl_cmd = "bash -c '"
                        "set -e; mkdir -p \"" + cm_dir + "/voices\"; "
                        "echo \"  Fetching config.json...\"; "
                        "curl -fL -# -o \"" + cm_dir + "/config.json\" \"" + hf_base + subdir + "config.json\"; "
                        "echo \"  Fetching model weights...\"; "
                        "curl -fL -# -o \"" + cm_dir + "/kokoro-v1_0.safetensors\" \"" + hf_base + subdir + "kokoro-v1_0.safetensors\"; "
                        "echo \"  Fetching voice embeddings...\"; "
                        "for v in af_heart af_alloy af_aoede af_bella af_jessica af_kore af_nicole af_nova af_river af_sarah af_sky "
                        "am_adam am_echo am_eric am_fenrir am_liam am_michael am_onyx am_puck am_santa "
                        "bf_alice bf_emma bf_isabella bf_lily bm_daniel bm_fable bm_george bm_lewis; do "
                        "curl -fL -s -o \"" + cm_dir + "/voices/${v}.safetensors\" \"" + hf_base + subdir + "voices/${v}.safetensors\"; "
                        "done; "
                        "echo \"  Downloaded $(ls \"" + cm_dir + "/voices/\" | wc -l | tr -d \" \") voice files\"; "
                        "'";
                    if (system(dl_cmd.c_str()) != 0) {
                        fprintf(stderr, "  %s%sMetalRT %s download failed.%s\n",
                                color::bold, color::yellow, type_label.c_str(), color::reset);
                    }
                } else {
                    std::string dl_cmd = "bash -c '"
                        "set -e; mkdir -p \"" + cm_dir + "\"; "
                        "curl -fL -# -o \"" + cm_dir + "/config.json\" \"" + hf_base + subdir + "config.json\"; "
                        "curl -fL -# -o \"" + cm_dir + "/model.safetensors\" \"" + hf_base + subdir + "model.safetensors\"; "
                        "curl -fL -# -o \"" + cm_dir + "/tokenizer.json\" \"" + hf_base + subdir + "tokenizer.json\"; "
                        "'";
                    if (system(dl_cmd.c_str()) != 0) {
                        fprintf(stderr, "  %s%sMetalRT %s download failed.%s\n",
                                color::bold, color::yellow, type_label.c_str(), color::reset);
                    }
                }
            }
        }
    }

    // Skip llama.cpp download if user only wants MetalRT and it succeeded
    if (!install_llamacpp) {
        fprintf(stderr, "\n  %s%sSetup complete!%s (MetalRT only)\n\n", color::bold, color::green, color::reset);
        fprintf(stderr, "  Get started:\n");
        fprintf(stderr, "    rcli              # interactive mode\n");
        fprintf(stderr, "    rcli metalrt status  # check MetalRT\n\n");
        return 0;
    }

    std::string script_path;
    std::string candidates[] = {
        "./scripts/download_models.sh",
        "../scripts/download_models.sh",
    };

    if (const char* root = getenv("RCLI_ROOT")) {
        script_path = std::string(root) + "/scripts/download_models.sh";
        struct stat st;
        if (stat(script_path.c_str(), &st) != 0) script_path.clear();
    }

    if (script_path.empty()) {
        for (auto& c : candidates) {
            struct stat st;
            if (stat(c.c_str(), &st) == 0) { script_path = c; break; }
        }
    }

    if (script_path.empty()) {
        fprintf(stderr, "  Downloading models to %s ...\n\n", models_dir.c_str());
        auto all = rcli::all_models();
        const auto* default_llm = rcli::get_default_model(all);
        std::string llm_filename = default_llm ? default_llm->filename : "lfm2-1.2b-tool-q4_k_m.gguf";
        std::string llm_url      = default_llm ? default_llm->url : "https://huggingface.co/LiquidAI/LFM2-1.2B-Tool-GGUF/resolve/main/LFM2-1.2B-Tool-Q4_K_M.gguf";
        std::string llm_name     = default_llm ? default_llm->name : "Liquid LFM2 1.2B Tool";

        std::string cmd = "bash -c '"
            "set -e; "
            "MODELS_DIR=\"" + models_dir + "\"; "
            "mkdir -p \"$MODELS_DIR\"; "
            "echo \"  Downloading " + llm_name + " LLM...\"; "
            "curl -L -# -o \"$MODELS_DIR/" + llm_filename + "\" "
            "\"" + llm_url + "\"; "
            "echo \"  Downloading Zipformer STT...\"; "
            "mkdir -p \"$MODELS_DIR/zipformer\"; "
            "cd \"$MODELS_DIR/zipformer\" && "
            "curl -L -# -O \"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-en-20M-2023-02-17.tar.bz2\" && "
            "tar xjf *.tar.bz2 --strip-components=1 && rm -f *.tar.bz2; "
            "cd \"$MODELS_DIR\"; "
            "echo \"  Downloading Whisper base.en STT...\"; "
            "mkdir -p \"$MODELS_DIR/whisper-base.en\"; "
            "cd \"$MODELS_DIR/whisper-base.en\" && "
            "curl -L -# -O \"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-base.en.tar.bz2\" && "
            "tar xjf *.tar.bz2 --strip-components=1 && rm -f *.tar.bz2; "
            "cd \"$MODELS_DIR\"; "
            "echo \"  Downloading Piper TTS voice...\"; "
            "mkdir -p \"$MODELS_DIR/piper-voice\"; "
            "cd \"$MODELS_DIR\" && "
            "curl -L -# -O \"https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-en_US-lessac-medium.tar.bz2\" && "
            "tar xjf vits-piper-en_US-lessac-medium.tar.bz2 && "
            "if [ -d vits-piper-en_US-lessac-medium ]; then "
            "mv vits-piper-en_US-lessac-medium/* piper-voice/ 2>/dev/null; "
            "rm -rf vits-piper-en_US-lessac-medium; fi; "
            "rm -f vits-piper-en_US-lessac-medium.tar.bz2; "
            "echo \"  Downloading Silero VAD...\"; "
            "curl -L -# -o \"$MODELS_DIR/silero_vad.onnx\" "
            "\"https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx\"; "
            "echo \"  Downloading espeak-ng data...\"; "
            "mkdir -p \"$MODELS_DIR/espeak-ng-data\"; "
            "cd \"$MODELS_DIR\" && "
            "curl -L -# -O \"https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/espeak-ng-data.tar.bz2\" && "
            "tar xjf espeak-ng-data.tar.bz2 && rm -f espeak-ng-data.tar.bz2; "
            "echo \"  Downloading Snowflake Arctic Embed (RAG, ~35MB)...\"; "
            "curl -L -# -o \"$MODELS_DIR/snowflake-arctic-embed-s-q8_0.gguf\" "
            "\"https://huggingface.co/ChristianAzinn/snowflake-arctic-embed-s-gguf/resolve/main/snowflake-arctic-embed-s-Q8_0.GGUF\"; "
            "echo \"\"; echo \"  Done! Models downloaded to $MODELS_DIR\"; "
            "'";
        int rc = system(cmd.c_str());
        if (rc != 0) {
            fprintf(stderr, "\n  %s%sDownload failed.%s Check your internet connection and try again.\n\n",
                    color::bold, color::red, color::reset);
            return 1;
        }
    } else {
        fprintf(stderr, "  Running %s ...\n\n", script_path.c_str());
        std::string cmd = "bash '" + script_path + "' '" + models_dir + "'";
        int rc = system(cmd.c_str());
        if (rc != 0) {
            fprintf(stderr, "\n  %s%sSetup failed.%s\n\n", color::bold, color::red, color::reset);
            return 1;
        }
    }

    fprintf(stderr, "\n  %s%sSetup complete!%s\n\n", color::bold, color::green, color::reset);
    fprintf(stderr, "  Get started:\n");
    fprintf(stderr, "    rcli              # interactive mode\n");
    fprintf(stderr, "    rcli listen       # continuous voice mode\n");
    fprintf(stderr, "    rcli actions      # see all available actions\n");
    fprintf(stderr, "    rcli ask \"open Safari\"\n\n");
    fprintf(stderr, "  %sTip:%s Run %srcli upgrade-stt%s for better speech recognition (Parakeet TDT, +640MB)\n\n",
            color::dim, color::reset, color::bold, color::reset);
    return 0;
}

inline int cmd_upgrade_stt(const Args& args) {
    std::string models_dir = args.models_dir;
    std::string parakeet_dir = models_dir + "/parakeet-tdt";
    std::string encoder_path = parakeet_dir + "/encoder.int8.onnx";

    fprintf(stderr, "\n%s%s  RCLI — Upgrade Speech Recognition%s\n\n", color::bold, color::orange, color::reset);

    if (access(encoder_path.c_str(), R_OK) == 0) {
        fprintf(stderr, "  %s%sParakeet TDT already installed!%s\n", color::bold, color::green, color::reset);
        fprintf(stderr, "  Location: %s\n\n", parakeet_dir.c_str());
        fprintf(stderr, "  Will be used automatically on next launch.\n\n");
        return 0;
    }

    fprintf(stderr, "  This will download NVIDIA Parakeet TDT 0.6B v3 (~640MB).\n");
    fprintf(stderr, "  It provides significantly better speech recognition:\n\n");
    fprintf(stderr, "    %s%-18s  %-12s  %s%s\n", color::bold, "Model", "Accuracy", "Size", color::reset);
    fprintf(stderr, "    %-18s  %-12s  %s\n", "Whisper base.en", "~5%% WER", "140MB (current)");
    fprintf(stderr, "    %-18s  %-12s  %s\n", "Parakeet TDT v3", "~1.9%% WER", "640MB (upgrade)");
    fprintf(stderr, "\n  Plus: auto punctuation, capitalization, 25 languages.\n");
    fprintf(stderr, "  License: CC-BY-4.0 (free for commercial use)\n\n");

    fprintf(stderr, "  Download to %s? [Y/n] ", parakeet_dir.c_str());
    fflush(stderr);

    char response = 0;
    if (read(STDIN_FILENO, &response, 1) <= 0 || response == '\n') response = 'y';
    if (response != 'y' && response != 'Y') {
        fprintf(stderr, "\n  Cancelled.\n\n");
        return 0;
    }
    fprintf(stderr, "\n");

    std::string cmd = "bash -c '"
        "set -e; "
        "DEST=\"" + parakeet_dir + "\"; "
        "mkdir -p \"$DEST\"; "
        "echo \"  Downloading Parakeet TDT 0.6B v3 INT8 (~640MB)...\"; "
        "echo \"  (this may take a few minutes)\"; "
        "echo \"\"; "
        "cd /tmp && "
        "curl -L -# -O \"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8.tar.bz2\" && "
        "echo \"  Extracting...\"; "
        "tar xjf sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8.tar.bz2 && "
        "cp sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/encoder.int8.onnx \"$DEST/\" && "
        "cp sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/decoder.int8.onnx \"$DEST/\" && "
        "cp sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/joiner.int8.onnx \"$DEST/\" && "
        "cp sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/tokens.txt \"$DEST/\" && "
        "rm -rf sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8 sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8.tar.bz2; "
        "echo \"\"; echo \"  Done!\"; "
        "'";

    int rc = system(cmd.c_str());
    if (rc != 0) {
        fprintf(stderr, "\n  %s%sDownload failed.%s Check your internet connection and try again.\n\n",
                color::bold, color::red, color::reset);
        return 1;
    }

    fprintf(stderr, "\n  %s%sParakeet TDT installed!%s\n\n", color::bold, color::green, color::reset);
    fprintf(stderr, "  Will be used automatically on next launch.\n");
    fprintf(stderr, "  Just run: %srcli%s\n\n", color::bold, color::reset);
    return 0;
}

inline int cmd_upgrade_llm(const Args& args) {
    std::string models_dir = args.models_dir;

    std::string engine_pref = rcli::read_engine_preference();
    bool is_metalrt = (engine_pref == "metalrt");

    auto all = rcli::all_models();

    if (is_metalrt) {
        // MetalRT mode — show only MetalRT-supported models with MLX sizes/speeds
        auto mrt = rcli::models_for_engine(rcli::LlmBackendType::METALRT);

        fprintf(stderr, "\n%s%s  RCLI — Upgrade Language Model (MetalRT)%s\n\n", color::bold, color::orange, color::reset);
        fprintf(stderr, "  Engine: %sMetalRT%s (Apple Silicon GPU)\n", color::cyan, color::reset);
        fprintf(stderr, "  Models are MLX 4-bit safetensors from mlx-community.\n\n");

        fprintf(stderr, "    %s#  %-28s  %-8s  %-12s  %-12s  %s%s\n",
                color::bold, "Model", "Size", "Speed", "Tool Call", "Status", color::reset);
        fprintf(stderr, "    %s──  %-28s  %-8s  %-12s  %-12s  %s%s\n",
                color::dim, "────────────────────────────", "────────", "────────────", "────────────", "──────", color::reset);

        for (size_t i = 0; i < mrt.size(); i++) {
            auto& m = mrt[i];
            bool installed = rcli::is_metalrt_model_installed(m);
            std::string status;
            if (installed) status = "\033[32minstalled\033[0m";

            fprintf(stderr, "    %s%zu%s  %-28s  %-8s  %-12s  %-12s  %s\n",
                    color::bold, i + 1, color::reset,
                    m.name.c_str(),
                    rcli::format_size(m.metalrt_size_mb).c_str(),
                    m.metalrt_speed_est.c_str(),
                    m.tool_calling.c_str(),
                    status.c_str());
        }

        fprintf(stderr, "\n");
        for (size_t i = 0; i < mrt.size(); i++) {
            auto& m = mrt[i];
            fprintf(stderr, "  %s%zu — %s%s\n",
                    color::bold, i + 1, m.name.c_str(), color::reset);
            fprintf(stderr, "       %s\n\n", m.description.c_str());
        }

        fprintf(stderr, "  Enter choice [1-%zu/q]: ", mrt.size());
        fflush(stderr);

        char buf[16] = {};
        if (read(STDIN_FILENO, buf, sizeof(buf) - 1) <= 0 || buf[0] == '\n') {
            fprintf(stderr, "\n  Cancelled.\n\n"); return 0;
        }
        if (buf[0] == 'q' || buf[0] == 'Q') {
            fprintf(stderr, "\n  Cancelled.\n\n"); return 0;
        }

        int choice = atoi(buf);
        if (choice < 1 || choice > (int)mrt.size()) {
            fprintf(stderr, "\n  Invalid choice.\n\n"); return 1;
        }

        auto& sel = mrt[choice - 1];
        if (rcli::is_metalrt_model_installed(sel)) {
            fprintf(stderr, "\n  %s%s%s already installed!%s\n\n",
                    color::bold, color::green, sel.name.c_str(), color::reset);
            return 0;
        }

        std::string mrt_dir = rcli::metalrt_models_dir() + "/" + sel.metalrt_dir_name;
        std::string size_str = rcli::format_size(sel.metalrt_size_mb);
        fprintf(stderr, "\n  Downloading %s MLX (~%s)...\n\n", sel.name.c_str(), size_str.c_str());

        std::string config_url = sel.metalrt_url;
        auto pos = config_url.rfind("model.safetensors");
        if (pos != std::string::npos) config_url.replace(pos, 17, "config.json");
        std::string dl_cmd = "bash -c '"
            "set -e; mkdir -p \"" + mrt_dir + "\"; "
            "curl -fL -# -o \"" + mrt_dir + "/model.safetensors\" \"" + sel.metalrt_url + "\"; "
            "curl -fL -# -o \"" + mrt_dir + "/tokenizer.json\" \"" + sel.metalrt_tokenizer_url + "\"; "
            "curl -fL -# -o \"" + mrt_dir + "/config.json\" \"" + config_url + "\"; "
            "'";

        if (system(dl_cmd.c_str()) != 0) {
            fprintf(stderr, "\n  %s%sDownload failed.%s Check your internet connection and try again.\n\n",
                    color::bold, color::red, color::reset);
            return 1;
        }

        fprintf(stderr, "\n  %s%s%s installed!%s\n", color::bold, color::green, sel.name.c_str(), color::reset);
        fprintf(stderr, "  Location: %s\n\n", mrt_dir.c_str());
        return 0;
    }

    // llama.cpp / auto mode — original behavior (GGUF models)
    auto models = all;
    auto options = rcli::get_upgrade_options(models);
    const auto* current_best = rcli::find_best_installed(models_dir, models);

    fprintf(stderr, "\n%s%s  RCLI — Upgrade Language Model%s\n\n", color::bold, color::orange, color::reset);
    if (engine_pref == "auto" || engine_pref.empty()) {
        fprintf(stderr, "  Engine: auto (llama.cpp). Tip: switch to MetalRT with %srcli setup%s for GPU speed.\n\n",
                color::bold, color::reset);
    }
    fprintf(stderr, "  Choose a model to download for smarter voice commands.\n");
    fprintf(stderr, "  The highest-priority installed model is used automatically.\n\n");

    fprintf(stderr, "    %s#  %-28s  %-7s  %-10s  %-12s  %s%s\n",
            color::bold, "Model", "Size", "Speed", "Tool Call", "Status", color::reset);
    fprintf(stderr, "    %s──  %-28s  %-7s  %-10s  %-12s  %s%s\n",
            color::dim, "────────────────────────────", "───────", "──────────", "────────────", "──────", color::reset);

    const auto* def = rcli::get_default_model(models);
    if (def) {
        bool is_active = current_best && current_best->id == def->id;
        fprintf(stderr, "    -  %-28s  %-7s  %-10s  %-12s  %s%s%s\n",
                (def->name + " (default)").c_str(),
                rcli::format_size(def->size_mb).c_str(),
                def->speed_est.c_str(), def->tool_calling.c_str(),
                is_active ? "\033[32mactive\033[0m" : "installed", "", "");
    }

    for (size_t i = 0; i < options.size(); i++) {
        auto* m = options[i];
        std::string path = models_dir + "/" + m->filename;
        bool installed = (access(path.c_str(), R_OK) == 0);
        bool is_active = current_best && current_best->id == m->id;

        std::string status;
        if (is_active) status = "\033[32mactive\033[0m";
        else if (installed) status = "\033[32minstalled\033[0m";

        std::string label = m->name;
        if (m->is_recommended) label += " *";

        fprintf(stderr, "    %s%zu%s  %-28s  %-7s  %-10s  %-12s  %s\n",
                color::bold, i + 1, color::reset,
                label.c_str(), rcli::format_size(m->size_mb).c_str(),
                m->speed_est.c_str(), m->tool_calling.c_str(), status.c_str());
    }

    fprintf(stderr, "\n");
    for (size_t i = 0; i < options.size(); i++) {
        auto* m = options[i];
        fprintf(stderr, "  %s%zu — %s%s%s\n",
                color::bold, i + 1, m->name.c_str(),
                m->is_recommended ? "  (recommended)" : "", color::reset);
        fprintf(stderr, "       %s\n\n", m->description.c_str());
    }

    fprintf(stderr, "  Enter choice [1-%zu/q]: ", options.size());
    fflush(stderr);

    char buf[16] = {};
    if (read(STDIN_FILENO, buf, sizeof(buf) - 1) <= 0 || buf[0] == '\n') {
        fprintf(stderr, "\n  Cancelled.\n\n"); return 0;
    }
    if (buf[0] == 'q' || buf[0] == 'Q') {
        fprintf(stderr, "\n  Cancelled.\n\n"); return 0;
    }

    int choice = atoi(buf);
    if (choice < 1 || choice > (int)options.size()) {
        fprintf(stderr, "\n  Invalid choice.\n\n"); return 1;
    }

    auto* selected = options[choice - 1];
    std::string dest_path = models_dir + "/" + selected->filename;
    if (access(dest_path.c_str(), R_OK) == 0) {
        fprintf(stderr, "\n  %s%s%s already installed!%s\n\n",
                color::bold, color::green, selected->name.c_str(), color::reset);
        bool is_active = current_best && current_best->id == selected->id;
        if (is_active) {
            fprintf(stderr, "  It's the active model. You're all set.\n\n");
        } else {
            fprintf(stderr, "  A higher-priority model is active (%s).\n",
                    current_best ? current_best->name.c_str() : "unknown");
            fprintf(stderr, "  Remove it to switch: rm \"%s/%s\"\n\n",
                    models_dir.c_str(), current_best ? current_best->filename.c_str() : "");
        }
        return 0;
    }

    fprintf(stderr, "\n");
    std::string size_str = rcli::format_size(selected->size_mb);
    std::string cmd = "bash -c '"
        "set -e; "
        "DEST=\"" + models_dir + "\"; "
        "echo \"  Downloading " + selected->name + " Q4_K_M (~" + size_str + ")...\"; "
        "echo \"  (this may take a few minutes)\"; "
        "echo \"\"; "
        "curl -L -# -o \"$DEST/" + selected->filename + "\" "
        "\"" + selected->url + "\"; "
        "echo \"\"; echo \"  Done!\"; "
        "'";

    int rc = system(cmd.c_str());
    if (rc != 0) {
        fprintf(stderr, "\n  %s%sDownload failed.%s Check your internet connection and try again.\n\n",
                color::bold, color::red, color::reset);
        return 1;
    }

    fprintf(stderr, "\n  %s%s%s installed!%s\n\n", color::bold, color::green, selected->name.c_str(), color::reset);
    fprintf(stderr, "  Will be used automatically on next launch (priority: %d).\n", selected->priority);
    fprintf(stderr, "  Just run: %srcli%s\n\n", color::bold, color::reset);
    return 0;
}

// Get total size of a directory in bytes (recursive)
static int64_t dir_size_bytes(const std::string& path) {
    int64_t total = 0;
    DIR* d = opendir(path.c_str());
    if (!d) return 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string full = path + "/" + entry->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode))
                total += dir_size_bytes(full);
            else
                total += st.st_size;
        }
    }
    closedir(d);
    return total;
}

static std::string format_bytes(int64_t bytes) {
    if (bytes >= 1024LL * 1024 * 1024)
        return std::to_string(bytes / (1024 * 1024 * 1024)) + "." +
               std::to_string((bytes / (1024 * 1024 * 100)) % 10) + " GB";
    if (bytes >= 1024 * 1024)
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    if (bytes >= 1024)
        return std::to_string(bytes / 1024) + " KB";
    return std::to_string(bytes) + " B";
}

inline int cmd_cleanup(const Args& args) {
    if (args.help) {
        fprintf(stderr,
            "\n%s%s  rcli cleanup%s  —  Remove unused models\n\n"
            "  Lists all installed models (LLM, STT, TTS) with sizes.\n"
            "  Active models cannot be deleted.\n"
            "  Use --all-unused to remove everything except active models.\n\n",
            color::bold, color::orange, color::reset);
        return 0;
    }

    std::string models_dir = args.models_dir;
    fprintf(stderr, "\n%s%s  RCLI Cleanup%s\n\n", color::bold, color::orange, color::reset);

    auto llm_all = rcli::all_models();
    auto stt_all = rcli::all_stt_models();
    auto tts_all = rcli::all_tts_models();

    const auto* llm_active = rcli::resolve_active_model(models_dir, llm_all);
    const auto* stt_active = rcli::resolve_active_stt(models_dir, stt_all);
    const auto* tts_active = rcli::resolve_active_tts(models_dir, tts_all);

    struct ModelEntry {
        std::string name;
        std::string path;
        std::string id;
        std::string modality;
        int64_t size_bytes;
        bool is_active;
        bool is_dir;
    };
    std::vector<ModelEntry> removable;

    // LLM models (single .gguf files)
    for (auto& m : llm_all) {
        std::string p = models_dir + "/" + m.filename;
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            bool active = llm_active && llm_active->id == m.id;
            removable.push_back({m.name, p, m.id, "LLM", st.st_size, active, false});
        }
    }

    // STT models (directories)
    for (auto& m : stt_all) {
        if (m.category == "streaming") continue;
        if (!rcli::is_stt_installed(models_dir, m)) continue;
        std::string p = models_dir + "/" + m.dir_name;
        bool active = stt_active && stt_active->id == m.id;
        int64_t sz = dir_size_bytes(p);
        removable.push_back({m.name, p, m.id, "STT", sz, active, true});
    }

    // TTS models (directories)
    for (auto& v : tts_all) {
        if (!rcli::is_tts_installed(models_dir, v)) continue;
        std::string p = models_dir + "/" + v.dir_name;
        bool active = tts_active && tts_active->id == v.id;
        int64_t sz = dir_size_bytes(p);
        removable.push_back({v.name, p, v.id, "TTS", sz, active, true});
    }

    if (removable.empty()) {
        fprintf(stderr, "  No models installed.\n\n");
        return 0;
    }

    fprintf(stderr, "  %s#  %-30s  %-5s  %-10s  Status%s\n",
            color::bold, "Model", "Type", "Size", color::reset);
    fprintf(stderr, "  %-3s%-30s  %-5s  %-10s  %-10s\n",
            "──", "──────────────────────────────", "─────", "──────────", "──────────");

    int idx = 0;
    for (auto& e : removable) {
        idx++;
        const char* status = e.is_active ? "\033[32mactive (protected)\033[0m" : "removable";
        fprintf(stderr, "  %-3d%-30s  %-5s  %-10s  %s\n",
                idx, e.name.c_str(), e.modality.c_str(),
                format_bytes(e.size_bytes).c_str(), status);
    }

    // Count removable (non-active)
    int64_t removable_bytes = 0;
    int removable_count = 0;
    for (auto& e : removable) {
        if (!e.is_active) { removable_bytes += e.size_bytes; removable_count++; }
    }

    if (removable_count == 0) {
        fprintf(stderr, "\n  All installed models are active. Nothing to remove.\n\n");
        return 0;
    }

    fprintf(stderr, "\n  %d removable model(s), %s total.\n",
            removable_count, format_bytes(removable_bytes).c_str());

    bool remove_all = (args.arg1 == "--all-unused");

    if (remove_all) {
        fprintf(stderr, "\n  Remove all %d unused models? [y/N]: ", removable_count);
    } else {
        fprintf(stderr, "  Enter model number to remove (0 to cancel): ");
    }
    fflush(stderr);

    char buf[64] = {0};
    if (!fgets(buf, sizeof(buf), stdin)) return 0;
    std::string input(buf);
    while (!input.empty() && (input.back() == '\n' || input.back() == '\r'))
        input.pop_back();

    if (remove_all) {
        if (input != "y" && input != "Y") {
            fprintf(stderr, "  Cancelled.\n\n");
            return 0;
        }
        for (auto& e : removable) {
            if (e.is_active) continue;
            std::string rm_cmd = e.is_dir
                ? "rm -rf '" + e.path + "'"
                : "rm -f '" + e.path + "'";
            system(rm_cmd.c_str());
            fprintf(stderr, "  Removed: %s (%s)\n", e.name.c_str(), format_bytes(e.size_bytes).c_str());

            // Clear pinned selection if this was the user's chosen model
            if (e.modality == "LLM") {
                std::string sel = rcli::read_selected_model_id();
                if (sel == e.id) rcli::clear_selected_model();
            } else if (e.modality == "STT") {
                std::string sel = rcli::read_selected_stt_id();
                if (sel == e.id) rcli::clear_selected_stt();
            } else if (e.modality == "TTS") {
                std::string sel = rcli::read_selected_tts_id();
                if (sel == e.id) rcli::clear_selected_tts();
            }
        }
        fprintf(stderr, "\n  %s%sDone! Freed %s.%s\n\n",
                color::bold, color::green, format_bytes(removable_bytes).c_str(), color::reset);
        return 0;
    }

    int choice = atoi(input.c_str());
    if (choice < 1 || choice > (int)removable.size()) {
        fprintf(stderr, "  Cancelled.\n\n");
        return 0;
    }

    auto& target = removable[choice - 1];
    if (target.is_active) {
        fprintf(stderr, "  %s%sCannot remove active model.%s Switch to another model first.\n\n",
                color::bold, color::red, color::reset);
        return 1;
    }

    fprintf(stderr, "  Remove %s (%s)? [y/N]: ", target.name.c_str(), format_bytes(target.size_bytes).c_str());
    fflush(stderr);
    if (!fgets(buf, sizeof(buf), stdin)) return 0;
    input = buf;
    while (!input.empty() && (input.back() == '\n' || input.back() == '\r'))
        input.pop_back();
    if (input != "y" && input != "Y") {
        fprintf(stderr, "  Cancelled.\n\n");
        return 0;
    }

    std::string rm_cmd = target.is_dir
        ? "rm -rf '" + target.path + "'"
        : "rm -f '" + target.path + "'";
    system(rm_cmd.c_str());

    if (target.modality == "LLM") {
        std::string sel = rcli::read_selected_model_id();
        if (sel == target.id) rcli::clear_selected_model();
    } else if (target.modality == "STT") {
        std::string sel = rcli::read_selected_stt_id();
        if (sel == target.id) rcli::clear_selected_stt();
    } else if (target.modality == "TTS") {
        std::string sel = rcli::read_selected_tts_id();
        if (sel == target.id) rcli::clear_selected_tts();
    }

    fprintf(stderr, "\n  %s%sRemoved: %s (%s)%s\n\n",
            color::bold, color::green, target.name.c_str(),
            format_bytes(target.size_bytes).c_str(), color::reset);
    return 0;
}
