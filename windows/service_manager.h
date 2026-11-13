#pragma once
#include <string>

namespace kb {
int RunRustDeskService();

enum class BackgroundServiceState {
    notInstalled, stopped, starting, running, stopping, paused, unknown, unavailable
};
struct BackgroundServiceInfo {
    BackgroundServiceState state{BackgroundServiceState::unavailable};
    bool credentialProviderRegistered{};
    std::wstring detail;
};

BackgroundServiceInfo QueryBackgroundService();
bool StartBackgroundService(std::wstring& message);
bool StopBackgroundService(std::wstring& message);
bool InstallApplication(std::wstring& message);
bool UninstallApplication(bool keepSettings, std::wstring& message);
} // namespace kb
