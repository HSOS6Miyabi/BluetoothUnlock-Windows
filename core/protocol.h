#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace kenvix {
inline constexpr char kServiceUuid[] = "5b0d6834-e3e9-48c0-977a-877a5d1f2b45";
inline constexpr char kPublicKeyUuid[] = "5b0d6835-e3e9-48c0-977a-877a5d1f2b45";
inline constexpr char kChallengeUuid[] = "5b0d6836-e3e9-48c0-977a-877a5d1f2b45";
inline constexpr char kResponseUuid[] = "5b0d6837-e3e9-48c0-977a-877a5d1f2b45";
inline constexpr uint8_t kVersion = 1;
inline constexpr size_t kNonceBytes = 32;
inline constexpr size_t kPublicKeyBytes = 65;
using Nonce = std::array<uint8_t, kNonceBytes>;
using PublicKey = std::array<uint8_t, kPublicKeyBytes>;
enum class AuthStatus : uint8_t { pending = 0, signed_response = 1, rejected = 2 };
struct Response {
  AuthStatus status{AuthStatus::pending};
  std::vector<uint8_t> signatureDer;
};
bool ValidPublicKey(std::span<const uint8_t> input);
bool ParseResponse(std::span<const uint8_t> input, Response &out);
std::array<uint8_t, 33> ChallengeFrame(const Nonce& nonce);
std::array<std::array<uint8_t,17>,2> ChallengeFragments(const Nonce& nonce);
std::vector<uint8_t> SignatureMessage(const Nonce& nonce);
bool DerToP1363(std::span<const uint8_t> der, std::array<uint8_t,64>& signature);
bool PassesSignal(int rssi, int minimum, unsigned samples, bool fresh);
std::string Hex(std::span<const uint8_t> input);
} // namespace kenvix
