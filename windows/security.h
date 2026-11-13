#pragma once
#include "protocol.h"
#include <array>
#include <cstdint>
#include <string>
#include <span>
namespace kb {
bool NewNonce(kenvix::Nonce& nonce);
bool VerifyPhoneSignature(const kenvix::PublicKey& pub,const kenvix::Nonce& nonce,
                          std::span<const uint8_t> signatureDer);
std::wstring Fingerprint(const kenvix::PublicKey& pub);
bool IsTargetSessionLocked(const std::wstring& user, const std::wstring& domain);
} // namespace kb
