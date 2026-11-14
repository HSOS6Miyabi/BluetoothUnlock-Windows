#include "service_manager.h"
#include "storage.h"
#include "remote_storage.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <chrono>

namespace {
constexpr wchar_t serviceName[] = L"BluetoothUnlockService";
constexpr wchar_t legacyServiceName[] = L"KenvixRustDeskUnlock";
constexpr wchar_t providerId[] = L"{12E137F0-91AB-4B74-A37F-019B284A5C8E}";
constexpr wchar_t exeName[] = L"BluetoothUnlock.exe";
constexpr wchar_t dllName[] = L"BluetoothUnlockCredentialProvider.dll";
constexpr wchar_t legacyExeName[] = L"KenvixBluetoothUnlock.exe";
constexpr wchar_t legacyDllName[] = L"KenvixBleCredentialProvider.dll";

std::wstring ErrorText(const wchar_t* operation, DWORD code = GetLastError()) {
    return std::wstring(operation) + L"（Windows 错误 " + std::to_wstring(code) + L"）";
}
std::wstring ExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return path;
}
std::filesystem::path InstallDirectory() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &folder))) return {};
    const std::filesystem::path result = std::filesystem::path(folder) / L"BluetoothUnlock";
    CoTaskMemFree(folder);
    return result;
}
std::filesystem::path LegacyInstallDirectory(){
    auto destination=InstallDirectory();
    return destination.empty()?destination:destination.parent_path()/L"Kenvix"/L"BluetoothUnlock";
}
bool CopyIfDifferent(const std::filesystem::path& source,
                     const std::filesystem::path& destination, std::wstring& error) {
    if (_wcsicmp(source.c_str(), destination.c_str()) == 0) return true;
    if (CopyFileW(source.c_str(), destination.c_str(), FALSE)) return true;
    error = ErrorText(L"无法复制文件：") + destination.wstring();
    return false;
}
bool WriteString(HKEY root, const std::wstring& path, const wchar_t* name,
                 const std::wstring& value, std::wstring& error) {
    HKEY key = nullptr;
    auto status = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0,
                                  KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        error = ErrorText(L"无法创建注册表项", status);
        return false;
    }
    status = RegSetValueExW(key, name, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(value.c_str()),
                           static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) error = ErrorText(L"无法保存注册表项", status);
    return status == ERROR_SUCCESS;
}
bool RegisterProvider(const std::filesystem::path& dll, std::wstring& error) {
    const std::wstring clsid = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + providerId + L"\\InprocServer32";
    const std::wstring provider = std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\") + providerId;
    return WriteString(HKEY_LOCAL_MACHINE, clsid, nullptr, dll.wstring(), error) &&
           WriteString(HKEY_LOCAL_MACHINE, clsid, L"ThreadingModel", L"Apartment", error) &&
           WriteString(HKEY_LOCAL_MACHINE, provider, nullptr, L"Bluetooth Unlock", error);
}
bool StopService(SC_HANDLE service, std::wstring& error) {
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes)) {
        error = ErrorText(L"无法查询服务状态");
        return false;
    }
    if (status.dwCurrentState == SERVICE_STOPPED) return true;
    if (status.dwCurrentState != SERVICE_STOP_PENDING) {
        SERVICE_STATUS previous{};
        if (!ControlService(service, SERVICE_CONTROL_STOP, &previous)) {
            error = ErrorText(L"无法停止服务");
            return false;
        }
    }
    for (int elapsed = 0; elapsed < 30000; elapsed += 250) {
        Sleep(250);
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes)) {
            error = ErrorText(L"无法查询服务停止状态");
            return false;
        }
        if (status.dwCurrentState == SERVICE_STOPPED) return true;
    }
    error = L"等待后台服务停止超时。";
    return false;
}
void RemoveOrSchedule(const std::filesystem::path& path) {
    if (DeleteFileW(path.c_str())) return;
    const auto error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}
bool RemoveRegistry(const std::wstring& path, std::wstring& error) {
    auto result = RegDeleteTreeW(HKEY_LOCAL_MACHINE, path.c_str());
    if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return true;
    error = ErrorText(L"无法删除注册表项", result);
    return false;
}
bool MigrateConfiguration(std::wstring& error) {
    if(GetFileAttributesW(kb::ConfigurationPath().c_str())==INVALID_FILE_ATTRIBUTES &&
       GetFileAttributesW(kb::LegacyConfigurationPath().c_str())!=INVALID_FILE_ATTRIBUTES){
        kb::Configuration settings;
        if(!kb::ReadConfig(settings)||!kb::SaveConfig(settings)){
            error=L"无法迁移原有的 Windows 解锁配置。";return false;
        }
    }
    const auto old=std::filesystem::path(kb::LegacyConfigurationPath()).parent_path()/L"rustdesk.dat";
    if(GetFileAttributesW(kb::RemoteConfigurationPath().c_str())==INVALID_FILE_ATTRIBUTES &&
       GetFileAttributesW(old.c_str())!=INVALID_FILE_ATTRIBUTES){
        kb::RemoteConfiguration remote;
        if(!kb::ReadRemoteConfiguration(remote)||!kb::SaveRemoteConfiguration(remote)){
            error=L"无法迁移原有的 RustDesk API 设置。";return false;
        }
    }
    return true;
}
bool StopAndRemoveLegacyService(SC_HANDLE manager,std::wstring& error){
    SC_HANDLE old=OpenServiceW(manager,legacyServiceName,SERVICE_QUERY_STATUS|SERVICE_STOP|DELETE);
    if(!old)return GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST;
    const bool stopped=StopService(old,error);
    const bool removed=stopped&&DeleteService(old)!=FALSE;
    if(stopped&&!removed)error=ErrorText(L"无法卸载旧版后台服务");
    CloseServiceHandle(old);
    return removed;
}
void RemoveLegacyInstallFiles(){
    const auto old=LegacyInstallDirectory();
    if(old.empty())return;
    RemoveOrSchedule(old/legacyExeName);
    RemoveOrSchedule(old/legacyDllName);
    RemoveDirectoryW(old.c_str());
    RemoveDirectoryW(old.parent_path().c_str());
}
} // namespace

bool kb::InstallApplication(std::wstring& message) {
    const auto source = ExecutablePath();
    const auto destination = InstallDirectory();
    if (source.empty() || destination.empty()) {
        message = L"无法获取当前程序路径或 Program Files 目录。";
        return false;
    }
    const auto sourceDirectory = std::filesystem::path(source).parent_path();
    const auto sourceDll = sourceDirectory / dllName;
    if (GetFileAttributesW(sourceDll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        message = L"未找到 BluetoothUnlockCredentialProvider.dll。请将 EXE 与 DLL 放在同一目录中。";
        return false;
    }
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE | SC_MANAGER_CONNECT);
    if (!manager) { message = ErrorText(L"无法连接 Windows 服务管理器"); return false; }
    SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_QUERY_STATUS | SERVICE_STOP |
                                     SERVICE_CHANGE_CONFIG | SERVICE_START);
    if (!service && GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST) {
        message = ErrorText(L"无法打开后台服务"); CloseServiceHandle(manager); return false;
    }
    if (service && !StopService(service, message)) {
        CloseServiceHandle(service); CloseServiceHandle(manager); return false;
    }
    if(!MigrateConfiguration(message)){
        if(service)CloseServiceHandle(service);
        CloseServiceHandle(manager);return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    if (ec || !CopyIfDifferent(source, destination / exeName, message) ||
        !CopyIfDifferent(sourceDll, destination / dllName, message)) {
        if (ec) message = L"无法创建程序安装目录：" + std::to_wstring(ec.value());
        if (service) CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return false;
    }
    if(!StopAndRemoveLegacyService(manager,message)){
        if(service)CloseServiceHandle(service);
        CloseServiceHandle(manager);return false;
    }
    const std::wstring serviceCommand = L"\"" + (destination / exeName).wstring() + L"\" --service";
    if (service) {
        if (!ChangeServiceConfigW(service, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                SERVICE_ERROR_NORMAL, serviceCommand.c_str(), nullptr, nullptr, nullptr,
                L"LocalSystem", nullptr, L"Bluetooth Unlock")) {
            message = ErrorText(L"无法更新后台服务");
            CloseServiceHandle(service); CloseServiceHandle(manager); return false;
        }
    } else {
        service = CreateServiceW(manager, serviceName, L"Bluetooth Unlock",
            SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_CHANGE_CONFIG | SERVICE_START,
            SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
            serviceCommand.c_str(), nullptr, nullptr, nullptr, L"LocalSystem", nullptr);
        if (!service) {
            message = ErrorText(L"无法创建后台服务"); CloseServiceHandle(manager); return false;
        }
    }
    SERVICE_DESCRIPTIONW description{const_cast<LPWSTR>(L"通过 RustDesk API SSE 监视已认证连接和连接结束事件")};
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);
    SC_ACTION actions[]{{SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 30000}, {SC_ACTION_RESTART, 60000}};
    SERVICE_FAILURE_ACTIONSW failure{};
    failure.dwResetPeriod = 86400;
    failure.cActions = 3;
    failure.lpsaActions = actions;
    ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);
    const bool registered = RegisterProvider(destination / dllName, message);
    if (registered && !StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        message = ErrorText(L"服务安装成功，但启动失败");
    }
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    if (!registered || !message.empty()) return false;
    RemoveLegacyInstallFiles();
    message = L"安装完成。已注册原生 Windows 登录选项并安装后台服务。请在设置窗口填写解锁信息。";
    return true;
}

bool kb::UninstallApplication(bool keepSettings, std::wstring& message) {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { message = ErrorText(L"无法连接 Windows 服务管理器"); return false; }
    for(const wchar_t* name:{serviceName,legacyServiceName}){
        SC_HANDLE service=OpenServiceW(manager,name,SERVICE_STOP|SERVICE_QUERY_STATUS|DELETE);
        if(service){
            if(!StopService(service,message)||!DeleteService(service)){
                if(message.empty())message=ErrorText(L"无法删除后台服务");
                CloseServiceHandle(service);CloseServiceHandle(manager);return false;
            }
            CloseServiceHandle(service);
        }else if(GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST){
            message=ErrorText(L"无法查询后台服务");CloseServiceHandle(manager);return false;
        }
    }
    CloseServiceHandle(manager);
    const std::wstring provider = std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\") + providerId;
    const std::wstring clsid = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + providerId;
    if (!RemoveRegistry(provider, message) || !RemoveRegistry(clsid, message)) return false;
    const auto dir = InstallDirectory();
    if (!dir.empty()) {
        RemoveOrSchedule(dir / dllName);
        RemoveOrSchedule(dir / exeName);
        RemoveDirectoryW(dir.c_str());
    }
    RemoveLegacyInstallFiles();
    if (!keepSettings) {
        PWSTR data = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &data))) {
            std::error_code ec;
            std::filesystem::remove_all(std::filesystem::path(data) / L"BluetoothUnlock", ec);
            if(!ec)std::filesystem::remove_all(std::filesystem::path(data) / L"Kenvix" / L"BluetoothUnlock", ec);
            CoTaskMemFree(data);
            if (ec) { message = L"已卸载程序，但无法删除配置目录。"; return false; }
        }
    }
    message = L"已删除本程序的登录提供程序与后台服务。请重新启动 Windows 以完成卸载。";
    return true;
}

namespace {
kb::BackgroundServiceState DecodeState(DWORD value) {
    switch (value) {
        case SERVICE_RUNNING: return kb::BackgroundServiceState::running;
        case SERVICE_STOPPED: return kb::BackgroundServiceState::stopped;
        case SERVICE_START_PENDING: return kb::BackgroundServiceState::starting;
        case SERVICE_STOP_PENDING: return kb::BackgroundServiceState::stopping;
        case SERVICE_PAUSED: case SERVICE_PAUSE_PENDING: case SERVICE_CONTINUE_PENDING:
            return kb::BackgroundServiceState::paused;
        default: return kb::BackgroundServiceState::unknown;
    }
}

bool WaitForRunning(SC_HANDLE service, std::wstring& message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        SERVICE_STATUS_PROCESS status{};
        DWORD bytes = 0;
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes)) {
            message = ErrorText(L"无法读取后台服务状态");
            return false;
        }
        if (status.dwCurrentState == SERVICE_RUNNING) return true;
        if (status.dwCurrentState == SERVICE_STOPPED) {
            message = L"后台服务启动失败（Windows 错误 " + std::to_wstring(status.dwWin32ExitCode) + L"）。";
            return false;
        }
        Sleep(250);
    }
    message = L"等待后台服务启动超时。";
    return false;
}
} // namespace

kb::BackgroundServiceInfo kb::QueryBackgroundService() {
    BackgroundServiceInfo info{};
    const std::wstring provider = std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\") + providerId;
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, provider.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        info.credentialProviderRegistered = true;
        RegCloseKey(key);
    }
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        info.detail = ErrorText(L"无法查询 Windows 服务");
        return info;
    }
    SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_QUERY_STATUS);
    bool legacy=false;
    if(!service&&GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST){
        service=OpenServiceW(manager,legacyServiceName,SERVICE_QUERY_STATUS);
        legacy=service!=nullptr;
    }
    if (!service) {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            info.state = BackgroundServiceState::notInstalled;
        } else info.detail = ErrorText(L"无法读取后台服务", error);
        return info;
    }
    SERVICE_STATUS_PROCESS status{};
    DWORD needed{};
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed)) {
        info.state = DecodeState(status.dwCurrentState);
    } else info.detail = ErrorText(L"无法读取后台服务状态");
    if(legacy)info.detail=L"检测到旧版服务，请使用“重新安装 / 修复”迁移。";
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return info;
}

bool kb::StartBackgroundService(std::wstring& message) {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { message = ErrorText(L"无法连接 Windows 服务管理器"); return false; }
    SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service) {
        message = ErrorText(L"请先安装后台服务");
        CloseServiceHandle(manager);
        return false;
    }
    bool success = StartServiceW(service, 0, nullptr) != FALSE;
    if (!success && GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) success = true;
    if (!success) message = ErrorText(L"无法启动后台服务");
    else success = WaitForRunning(service, message);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    if (success) message = L"后台服务已启动。";
    return success;
}

bool kb::StopBackgroundService(std::wstring& message) {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { message = ErrorText(L"无法连接 Windows 服务管理器"); return false; }
    SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!service) {
        message = ErrorText(L"请先安装后台服务");
        CloseServiceHandle(manager);
        return false;
    }
    const bool success = StopService(service, message);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    if (success) message = L"后台服务已停止。";
    return success;
}
