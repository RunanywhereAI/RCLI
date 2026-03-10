#pragma once
// =============================================================================
// RCLI CLI — Help, usage, and banner display
// =============================================================================

#include "cli/cli_common.h"

inline void print_usage(const char* argv0) {
    fprintf(stderr,
        "\n%s%s  RCLI%s  —  On-Device Voice AI + RAG for macOS\n\n"
        "  Speak commands, execute actions, query your docs — 100%% local.\n\n"
        "%s  USAGE%s\n"
        "    %s <command> [options]\n\n"
        "%s  COMMANDS%s\n"
        "    %s(no command)%s      Interactive mode (push-to-talk + text input)\n"
        "    %smetalrt%s            Interactive mode using MetalRT engine\n"
        "    %sllamacpp%s           Interactive mode using llama.cpp engine\n"
        "    %slisten%s             Voice mode — push-to-talk with SPACE\n"
        "    %sask%s <text>         One-shot text command\n"
        "    %sactions%s [name]     List all actions, or show detail for one\n"
        "    %saction%s <n> [json]  Execute a named action directly\n"
        "    %srag%s <sub>          RAG: ingest docs, query, status\n"
        "    %ssetup%s              Download AI models (~1GB)\n"
        "    %smodels%s             Manage all AI models (LLM, STT, TTS)\n"
        "    %smodels llm|stt|tts%s Jump to a specific modality\n"
        "    %svoices%s             Manage TTS voices (alias: models tts)\n"
        "    %sstt%s                Manage STT models (alias: models stt)\n"
        "    %supgrade-stt%s        Upgrade to Parakeet TDT (better accuracy, +640MB)\n"
        "    %supgrade-llm%s        Upgrade LLM (Qwen3.5, LFM2, and more)\n"
        "    %spersonality%s        Change assistant personality (quirky, cynical, nerdy, ...)\n"
        "    %scleanup%s            Remove unused models to free disk space\n"
        "    %sbench%s              Run benchmarks (STT, LLM, TTS, E2E, RAG)\n"
        "    %sinfo%s               Show engine info\n\n"
        "%s  OPTIONS%s\n"
        "    --models <dir>      Models directory (default: ~/Library/RCLI/models)\n"
        "    --rag <index>       Load RAG index for document-grounded answers\n"
        "    --gpu-layers <n>    GPU layers for LLM (default: 99 = all)\n"
        "    --ctx-size <n>      LLM context size (default: 4096)\n"
        "    --no-speak          Don't speak responses (text output only)\n"
        "    --verbose, -v       Show debug logs from engines\n\n"
        "%s  EXAMPLES%s\n"
        "    rcli                                    # interactive mode\n"
        "    rcli metalrt                            # interactive mode (MetalRT)\n"
        "    rcli llamacpp                           # interactive mode (llama.cpp)\n"
        "    rcli listen                             # hands-free voice control\n"
        "    rcli ask \"open Safari\"                  # one-shot command\n"
        "    rcli ask \"create a note called Ideas\"   # triggers action\n"
        "    rcli actions                            # see all actions\n"
        "    rcli actions create_note                # action detail\n"
        "    rcli setup                              # download models\n\n",
        color::bold, color::orange, color::reset,
        color::bold, color::reset,
        argv0,
        color::bold, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::green, color::reset,
        color::dim, color::reset,
        color::dim, color::reset);
}

inline void print_help_listen() {
    fprintf(stderr,
        "\n%s%s  RCLI listen%s  —  Push-to-talk voice mode\n\n"
        "  Press SPACE to start talking, SPACE again to stop.\n"
        "  RCLI transcribes, executes actions, and speaks the result.\n\n"
        "%s  OPTIONS%s\n"
        "    --models <dir>      Models directory\n"
        "    --gpu-layers <n>    GPU layers (default: 99)\n"
        "    --no-speak          Don't speak responses\n\n"
        "%s  HOW IT WORKS%s\n"
        "    1. Press SPACE — microphone starts recording\n"
        "    2. Speak your command\n"
        "    3. Press SPACE — recording stops, speech is transcribed\n"
        "    4. RCLI processes your request (action or conversation)\n"
        "    5. Speaks the result back to you\n\n"
        "  Press Ctrl+C to quit.\n\n",
        color::bold, color::orange, color::reset,
        color::bold, color::reset,
        color::bold, color::reset);
}

inline void print_help_ask() {
    fprintf(stderr,
        "\n%s%s  RCLI ask%s <text>  —  One-shot text command\n\n"
        "  Send a text command, get a response. If the command matches an action,\n"
        "  it executes on your Mac — 100%% locally.\n\n"
        "%s  OPTIONS%s\n"
        "    --models <dir>      Models directory\n"
        "    --gpu-layers <n>    GPU layers (default: 99)\n"
        "    --no-speak          Don't speak the response\n\n"
        "%s  EXAMPLES%s\n"
        "    rcli ask \"open Safari\"\n"
        "    rcli ask \"create a note called Shopping List with milk, eggs\"\n"
        "    rcli ask \"what's on my calendar today?\"\n"
        "    rcli ask \"what is the capital of France?\"\n\n",
        color::bold, color::orange, color::reset,
        color::bold, color::reset,
        color::bold, color::reset);
}

inline void print_help_interactive() {
    fprintf(stderr, "\n%s%s  Voice:%s\n", color::bold, color::orange, color::reset);
    fprintf(stderr, "  %s1.%s Press %sSPACE%s to start recording\n", color::dim, color::reset, color::bold, color::reset);
    fprintf(stderr, "  %s2.%s Speak your command (e.g. \"open Safari\")\n", color::dim, color::reset);
    fprintf(stderr, "  %s3.%s Press %sENTER%s to stop recording & process\n\n", color::dim, color::reset, color::bold, color::reset);
    fprintf(stderr, "  %s%s  Text:%s\n", color::bold, color::orange, color::reset);
    fprintf(stderr, "  Just type a command and press %sENTER%s  (natural language goes to LLM)\n\n", color::bold, color::reset);
    fprintf(stderr, "  %s%s  Commands:%s  %s(work with or without /)%s\n", color::bold, color::orange, color::reset, color::dim, color::reset);
    fprintf(stderr, "  %shelp%s                  show this help\n", color::bold, color::reset);
    fprintf(stderr, "  %sactions%s               list all available actions\n", color::bold, color::reset);
    fprintf(stderr, "  %sactions <name>%s        show details for an action\n", color::bold, color::reset);
    fprintf(stderr, "  %smodels%s                show all active models (LLM, STT, TTS)\n", color::bold, color::reset);
    fprintf(stderr, "  %svoices%s                show active TTS voice\n", color::bold, color::reset);
    fprintf(stderr, "  %sstt%s                   show active STT model\n", color::bold, color::reset);
    fprintf(stderr, "  %sdo <action> [text]%s    execute action directly (no JSON needed)\n", color::bold, color::reset);
    fprintf(stderr, "  %srag status%s            show indexed documents\n", color::bold, color::reset);
    fprintf(stderr, "  %srag ingest <path>%s     index docs for Q&A\n", color::bold, color::reset);
    fprintf(stderr, "  %squit%s                  exit\n\n", color::bold, color::reset);
    fprintf(stderr, "  %s%s  Try:%s\n", color::bold, color::orange, color::reset);
    fprintf(stderr, "  %s\"Open Safari\"  \"What's on my calendar?\"  \"Set volume to 50\"%s\n\n",
            color::dim, color::reset);
}

inline void print_help_bench() {
    fprintf(stderr,
        "\n%s%s  RCLI bench%s  —  Comprehensive Benchmarks\n\n"
        "  Runs benchmarks across all AI subsystems with bundled speech samples.\n\n"
        "%s  SUITES%s\n"
        "    all        Everything below (default)\n"
        "    stt        Speech-to-text latency + accuracy (WER)\n"
        "    llm        TTFT, throughput, tool calling\n"
        "    tts        Text-to-speech latency + RTF\n"
        "    e2e        Full pipeline: STT \xe2\x86\x92 LLM \xe2\x86\x92 TTS\n"
        "    tools      Action keyword matching speed\n"
        "    rag        Embedding, retrieval, full RAG query\n"
        "    memory     Process RSS after model load + generation\n\n"
        "%s  OPTIONS%s\n"
        "    --suite <name>     Suite to run (default: all, comma-separated ok)\n"
        "    --runs <n>         Measured runs per test (default: 3)\n"
        "    --output <file>    Save JSON results to file\n"
        "    --rag <index>      Load RAG index for RAG benchmarks\n"
        "    --models <dir>     Models directory\n"
        "    --llm <id>         Override LLM model for benchmark\n"
        "    --tts <id>         Override TTS model for benchmark\n"
        "    --stt <id>         Override STT model for benchmark\n"
        "    --all-llm          Benchmark all installed LLM models\n"
        "    --all-tts          Benchmark all installed TTS models\n\n"
        "%s  EXAMPLES%s\n"
        "    rcli bench                          # run all benchmarks\n"
        "    rcli bench --suite llm              # LLM only\n"
        "    rcli bench --suite stt,tts          # STT + TTS\n"
        "    rcli bench --output results.json    # save to file\n"
        "    rcli bench --all-llm --suite llm    # compare all installed LLMs\n"
        "    rcli bench --rag ~/Library/RCLI/index --suite rag\n\n",
        color::bold, color::orange, color::reset,
        color::bold, color::reset,
        color::bold, color::reset,
        color::bold, color::reset);
}

inline void print_help_rag() {
    fprintf(stderr,
        "\n%s%s  rcli rag%s <subcommand>  —  Retrieval-Augmented Generation\n\n"
        "  Index your documents for voice Q&A powered by local AI.\n\n"
        "%s  SUBCOMMANDS%s\n"
        "    rcli rag ingest <dir>    Index documents from a directory\n"
        "    rcli rag query <text>    Query your indexed documents\n"
        "    rcli rag status          Show index info\n\n"
        "%s  OPTIONS%s\n"
        "    --models <dir>     Models directory\n"
        "    --gpu-layers <n>   GPU layers (default: 99)\n\n"
        "%s  EXAMPLES%s\n"
        "    rcli rag ingest ~/Documents/notes\n"
        "    rcli rag query \"What were the key decisions from the meeting?\"\n\n"
        "  Once indexed, use --rag with interactive/ask:\n"
        "    rcli --rag ~/Library/RCLI/index\n"
        "    rcli ask --rag ~/Library/RCLI/index \"summarize the project plan\"\n\n",
        color::bold, color::orange, color::reset,
        color::bold, color::reset,
        color::bold, color::reset,
        color::bold, color::reset);
}

inline void print_banner(const Args& /*args*/) {
    static const char* art[] = {
        u8"██████╗  ██████╗██╗     ██╗",
        u8"██╔══██╗██╔════╝██║     ██║",
        u8"██████╔╝██║     ██║     ██║",
        u8"██╔══██╗██║     ██║     ██║",
        u8"██║  ██║╚██████╗███████╗██║",
        u8"╚═╝  ╚═╝ ╚═════╝╚══════╝╚═╝",
    };

    static const int art_visual_width = 28;
    static const char* tagline = "On-device voice AI and RAG for macOS";
    static const int tagline_len = 36;
    static const char* powered = "Powered by RunAnywhere";
    static const int powered_len = 22;

    int tw = get_terminal_width();
    int art_pad = (tw > art_visual_width) ? (tw - art_visual_width) / 2 : 0;
    int tag_pad = (tw > tagline_len) ? (tw - tagline_len) / 2 : 0;
    int pow_pad = (tw > powered_len) ? (tw - powered_len) / 2 : 0;

    fprintf(stderr, "\n");
    for (int i = 0; i < 6; i++) {
        fprintf(stderr, "%*s%s%s%s%s\n", art_pad, "", color::bold, color::orange, art[i], color::reset);
    }
    fprintf(stderr, "%*s%s%s%s\n", tag_pad, "", color::dim, tagline, color::reset);
    fprintf(stderr, "%*s%s%s%s\n\n", pow_pad, "", color::dim, powered, color::reset);
}
