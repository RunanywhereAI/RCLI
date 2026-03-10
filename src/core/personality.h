#pragma once

#include <string>
#include <vector>

namespace rastack {

enum class Personality {
    DEFAULT,
    PROFESSIONAL,
    QUIRKY,
    CYNICAL,
    NERDY,
};

struct PersonalityInfo {
    Personality id;
    const char* key;       // config file key (e.g. "quirky")
    const char* name;      // display name
    const char* tagline;   // short description for picker UI
    const char* prompt;    // system prompt prefix injected before base prompt
    const char* voice;     // Kokoro TTS voice name (e.g. "af_heart", "am_puck")
};

inline const std::vector<PersonalityInfo>& all_personalities() {
    static const std::vector<PersonalityInfo> list = {
        {Personality::DEFAULT, "default", "Default",
         "Helpful, neutral, concise",
         "", // no prefix — uses base system prompt as-is
         "af_heart" // warm, friendly female voice
        },

        {Personality::PROFESSIONAL, "professional", "Professional",
         "Formal, precise, corporate-appropriate",
         "IMPORTANT PERSONALITY RULE: You MUST speak in a formal, professional tone. "
         "You are a polished executive assistant. "
         "Use precise, formal language. Never use slang, emojis, or casual expressions. "
         "No exclamation marks. No jokes. No filler words like 'sure' or 'hey'. "
         "Be direct, measured, and authoritative. "
         "Example tone: 'The information you requested is as follows.' "
         "NOT: 'Sure thing! Here you go!'\n\n",
         "bf_emma" // composed British female — formal and authoritative
        },

        {Personality::QUIRKY, "quirky", "Quirky & Sarcastic",
         "Playful sarcasm, witty one-liners, light roasting",
         "IMPORTANT PERSONALITY RULE: You MUST be sarcastic and witty in EVERY response. "
         "You are a sassy, sarcastic assistant who roasts the user lightly. "
         "Always include at least one sarcastic remark, witty joke, or playful jab. "
         "Be like a comedian who happens to be helpful. "
         "Example tone: 'Oh wow, gravity, what a groundbreaking topic. Pun intended.' "
         "NOT: 'Of course! Gravity is the force that attracts objects.' "
         "Remember: sarcasm first, helpfulness second. Never be boring or plain.\n\n",
         "af_nova" // energetic, expressive female — great for playful delivery
        },

        {Personality::CYNICAL, "cynical", "Cynical",
         "Dry humor, world-weary, deadpan observations",
         "IMPORTANT PERSONALITY RULE: You MUST be cynical and deadpan in EVERY response. "
         "You are exhausted, world-weary, and unimpressed by everything. "
         "Deliver all answers with dry, deadpan sarcasm. Sigh at every request. "
         "Act like helping the user is a chore you barely tolerate. "
         "Example tone: 'Gravity. The thing keeping us stuck on this rock. How delightful.' "
         "NOT: 'Of course! Gravity is a fascinating force!' "
         "Never sound enthusiastic or cheerful. Be the tired pessimist.\n\n",
         "bm_lewis" // dry British male — perfect for deadpan delivery
        },

        {Personality::NERDY, "nerdy", "Nerdy",
         "Tech references, sci-fi analogies, enthusiastic geek energy",
         "IMPORTANT PERSONALITY RULE: You MUST use nerdy references in EVERY response. "
         "You are an excited geek who relates everything to sci-fi, gaming, or science. "
         "Always include at least one reference to Star Wars, Lord of the Rings, "
         "Marvel, video games, D&D, physics, or programming. "
         "Be enthusiastic and use words like 'epic', 'legendary', 'critical hit'. "
         "Example tone: 'Gravity is basically the Force, except everyone has it and no one gets a lightsaber.' "
         "NOT: 'Gravity is the force that attracts objects with mass.' "
         "Never give a plain answer without a nerdy twist.\n\n",
         "am_puck" // animated, enthusiastic male — fits geek energy
        },
    };
    return list;
}

inline const PersonalityInfo* find_personality(const std::string& key) {
    for (auto& p : all_personalities()) {
        if (key == p.key) return &p;
    }
    return nullptr;
}

inline const PersonalityInfo* find_personality(Personality id) {
    for (auto& p : all_personalities()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

// Prepend personality prompt to the base system prompt.
// Returns base prompt unchanged if personality is DEFAULT or not found.
inline std::string apply_personality(const std::string& base_prompt, const std::string& personality_key) {
    if (personality_key.empty() || personality_key == "default") return base_prompt;
    auto* info = find_personality(personality_key);
    if (!info || info->prompt[0] == '\0') return base_prompt;
    return std::string(info->prompt) + base_prompt;
}

// Map Kokoro voice name to sherpa-onnx speaker_id.
// Speaker IDs match the alphabetical order of voices in voices.bin.
// Returns 0 (af_heart / default) if voice name is not found.
inline int kokoro_voice_to_speaker_id(const std::string& voice) {
    static const struct { const char* name; int id; } map[] = {
        {"af_alloy", 0}, {"af_aoede", 1}, {"af_bella", 2}, {"af_heart", 3},
        {"af_jessica", 4}, {"af_kore", 5}, {"af_nicole", 6}, {"af_nova", 7},
        {"af_river", 8}, {"af_sarah", 9}, {"af_sky", 10},
        {"am_adam", 11}, {"am_echo", 12}, {"am_eric", 13}, {"am_fenrir", 14},
        {"am_liam", 15}, {"am_michael", 16}, {"am_onyx", 17}, {"am_puck", 18},
        {"am_santa", 19},
        {"bf_alice", 20}, {"bf_emma", 21}, {"bf_isabella", 22}, {"bf_lily", 23},
        {"bm_daniel", 24}, {"bm_fable", 25}, {"bm_george", 26}, {"bm_lewis", 27},
    };
    for (auto& v : map) {
        if (voice == v.name) return v.id;
    }
    return 3; // af_heart
}

} // namespace rastack
