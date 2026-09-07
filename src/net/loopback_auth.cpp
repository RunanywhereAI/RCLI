#include "net/loopback_auth.h"

#include <cstddef>
#include <random>
#include <string>

namespace wally::net {

std::string GenerateLoopbackToken() {
    std::random_device device;
    std::uniform_int_distribution<unsigned> byte(0, 255);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string token;
    token.reserve(64);
    for (int i = 0; i < 32; ++i) {
        const unsigned value = byte(device);
        token.push_back(kHex[(value >> 4) & 0xF]);
        token.push_back(kHex[value & 0xF]);
    }
    return token;
}

bool ConstantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return difference == 0;
}

}  // namespace wally::net
