#pragma once
#include <windows.h>
#include <cstdint>
namespace kb {
inline constexpr wchar_t kRemoteGrantName[]=L"Global\\BluetoothUnlockGrantV1_8F21";
inline constexpr wchar_t kRemoteGrantChangedEvent[]=L"Global\\BluetoothUnlockNotifyV1_8F21";
inline constexpr wchar_t kRemoteConfigurationChangedEvent[]=L"Global\\BluetoothUnlockConfigV1_8F21";
class RemoteGrant {
public:
    explicit RemoteGrant(bool owner);
    ~RemoteGrant();
    RemoteGrant(const RemoteGrant&)=delete;
    RemoteGrant& operator=(const RemoteGrant&)=delete;
    bool Available() const noexcept {return view_!=nullptr;}
    bool Valid() const;
    bool Claim();
    void Clear();
    void Publish(unsigned milliseconds=12000);
    HANDLE ChangedEvent() const noexcept{return changed_;}
    HANDLE ConfigurationChangedEvent() const noexcept{return configChanged_;}
private:
    HANDLE mapping_{};
    HANDLE changed_{};
    HANDLE configChanged_{};
    volatile LONG64* view_{};
};
} // namespace kb
