#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace kb {
// Protects an existing DPAPI ciphertext with a non-exportable TPM RSA key.
// The key is stored under the machine's Microsoft Platform Crypto Provider.
bool ProtectWithTpm(std::span<const std::uint8_t> input,
                    std::vector<std::uint8_t>& output);
bool UnprotectWithTpm(std::span<const std::uint8_t> input,
                      std::vector<std::uint8_t>& output);
bool IsTpmProtected(std::span<const std::uint8_t> input);
}