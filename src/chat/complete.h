#ifndef RCLI_CHAT_COMPLETE_H
#define RCLI_CHAT_COMPLETE_H

#include <string>
#include <vector>

namespace rcli::chat {

struct Completion {
    /// What the input becomes if this is accepted.
    std::string replacement;
    /// Shown in the suggestion strip.
    std::string label;
    std::string detail;
};

/// Suggestions for the line being typed.
///
/// Driven by position, not just prefix: at the start of a line it offers
/// commands, after `/load` it offers model ids, after `/set <name>` it offers
/// that setting's values. Returns nothing for ordinary prose, so the strip
/// stays out of the way while you are just talking.
std::vector<Completion> Suggest(const std::string& line);

/// The longest prefix every candidate shares, which is what Tab should fill in
/// when the choice is still ambiguous.
std::string CommonPrefix(const std::vector<Completion>& options);

}  // namespace rcli::chat

#endif  // RCLI_CHAT_COMPLETE_H
