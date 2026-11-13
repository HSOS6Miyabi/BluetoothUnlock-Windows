#pragma once
#include <string>
namespace kb {
int RunRustDeskService();
bool InstallApplication(std::wstring& message);
bool UninstallApplication(bool keepSettings, std::wstring& message);
}
