#ifndef WALLY_NET_LOOPBACK_AUTH_H
#define WALLY_NET_LOOPBACK_AUTH_H

#include <string>

namespace wally::net {

/// A fresh random secret for a loopback proxy to hand its wrapped tool.
///
/// The coding-harness proxies bind 127.0.0.1 and forward to the hosted API on
/// the signed-in user's credit. The port is reachable by any other process
/// running as the same local user, so the proxy must prove its caller is the
/// tool it launched and not a bystander. It does that by generating one of
/// these per session, giving it to the tool as its API key, and rejecting any
/// request that does not present it.
///
/// The value is 32 bytes of `std::random_device` output rendered as hex. On the
/// supported platforms `random_device` is the OS CSPRNG; this is not a
/// cryptographic key exchange, only a same-host capability token, so that is
/// enough. Compared byte-for-byte with a constant-time check at the boundary.
std::string GenerateLoopbackToken();

/// A length-independent equality check, so a caller cannot learn the secret one
/// byte at a time from how long a rejection takes.
bool ConstantTimeEquals(const std::string& a, const std::string& b);

}  // namespace wally::net

#endif  // WALLY_NET_LOOPBACK_AUTH_H
