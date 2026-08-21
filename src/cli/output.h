#ifndef RCLI_CLI_OUTPUT_H
#define RCLI_CLI_OUTPUT_H

#include <cstdint>
#include <string>
#include <string_view>

namespace rcli::out {

/// Ambient's tokens, named by intent, emitted as ANSI rather than drawn.
///
/// The palette is the same one the app has always used; only the way it
/// reaches the terminal changed. Truecolor when the terminal admits to it,
/// 256-colour otherwise, and nothing at all when output is a pipe — a log file
/// full of escape codes helps no one.
enum class Ink {
    Plain,
    Dim,
    Faint,
    Accent,
    AccentBright,
    Live,
    Success,
    Info,
    Warning,
    Error,
};

/// Wraps `text` in the colour, or returns it untouched when colour is off.
std::string Paint(Ink ink, std::string_view text);
std::string Bold(std::string_view text);

/// Colour is decided once: NO_COLOR, a non-tty stdout, or TERM=dumb all
/// disable it. `Force` exists for --color=always.
void DetectColor();
void ForceColor(bool on);
bool Colored();

/// Keeps writing our own notices to the real stderr after the engines' stderr
/// has been redirected away.
///
/// Silencing llama.cpp means freopen on stderr, which would take the progress
/// bars and status lines with it — they are on stderr precisely so stdout stays
/// pipeable. The descriptor is duplicated before the redirect and everything
/// below writes to the copy.
void KeepNotices();

/// stdout with a newline. Everything the user asked for goes here.
void Line(std::string_view text);
void Say(Ink ink, std::string_view text);
/// A note about what is happening, not a result: goes to stderr so `rcli list`
/// can be piped without the chatter.
void Status(std::string_view text);
void Error(std::string_view text);

/// "639 MB", "2.5 GB".
std::string HumanSize(std::int64_t bytes);
std::string HumanDuration(std::int64_t seconds);

/// A single-line progress bar rewritten in place, for a download or a long
/// generation. `Finish` leaves the last state on screen and moves on.
class Progress {
   public:
    explicit Progress(std::string label);
    ~Progress();

    void Update(float fraction, std::string_view detail);
    /// No fraction to report: shows a spinner and the elapsed time.
    void Tick(std::string_view detail);
    void Finish(std::string_view detail);

   private:
    void Draw(std::string_view body);

    std::string label_;
    bool live_;
    int frame_ = 0;
    bool finished_ = false;
};

}  // namespace rcli::out

#endif  // RCLI_CLI_OUTPUT_H
