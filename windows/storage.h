#pragma once
#include "protocol.h"
#include <windows.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace kb {
enum class Mode : uint32_t { low = 1, high = 2, remote = 3 };
struct Configuration {
  Mode mode{Mode::high};
  int minRssi{-70};
  uint64_t lowAddress{0};
  kenvix::PublicKey trustedKey{};
  std::wstring username;
  std::wstring domain{L"."};
  std::wstring userSid;
  std::vector<uint8_t> passwordEncrypted;
};
std::wstring ConfigurationPath();
std::wstring LegacyConfigurationPath();
bool ReadConfig(Configuration& result);
bool SaveConfig(const Configuration& value);
bool EncryptPassword(const std::wstring& plain, std::vector<uint8_t>& encrypted);
bool DecryptPassword(const std::vector<uint8_t>& encrypted, std::wstring& plain);
std::wstring ResolveUserSid(const std::wstring& user,const std::wstring& domain);
bool ValidatePassword(const std::wstring& user,const std::wstring& domain,const std::wstring& password);
void Wipe(std::wstring& secret);
} // namespace kb
