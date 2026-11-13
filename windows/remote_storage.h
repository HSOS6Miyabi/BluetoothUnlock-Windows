#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace kb {
struct RemoteConfiguration {
    bool enabled{false};
    bool lockOnLastDisconnect{true};
    bool trustedEventsOnly{false};
    std::wstring serverUrl;
    std::wstring deviceId;
    std::wstring allowedPeer;
    std::vector<std::uint8_t> apiKeyEncrypted;
};
std::wstring RemoteConfigurationPath();
bool ReadRemoteConfiguration(RemoteConfiguration& config);
bool SaveRemoteConfiguration(const RemoteConfiguration& config);
bool EncryptRemoteApiKey(const std::wstring& key, std::vector<std::uint8_t>& encrypted);
bool DecryptRemoteApiKey(const std::vector<std::uint8_t>& encrypted, std::wstring& key);
void SignalRemoteConfigChanged();
} // namespace kb
