#include "remote_http.h"
#include "service_manager.h"
#include "remote_ipc.h"
#include "remote_storage.h"
#include "security.h"
#include "storage.h"
#include <windows.h>
#include <wtsapi32.h>
#include <sddl.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#pragma comment(lib,"wtsapi32.lib")
#pragma comment(lib,"advapi32.lib")
namespace {
constexpr wchar_t kServiceName[]=L"BluetoothUnlockService";
SERVICE_STATUS_HANDLE g_statusHandle{};
SERVICE_STATUS g_status{};
HANDLE g_stop{};
std::atomic<bool> g_stopping{false};
void Status(DWORD state,DWORD hint=0){
    g_status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState=state;
    g_status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN:0;
    g_status.dwWin32ExitCode=NO_ERROR;
    g_status.dwWaitHint=hint;
    SetServiceStatus(g_statusHandle,&g_status);
}
DWORD WINAPI Control(DWORD code,DWORD,LPVOID,LPVOID){
    if(code==SERVICE_CONTROL_STOP||code==SERVICE_CONTROL_SHUTDOWN){
        g_stopping=true;
        if(g_stop)SetEvent(g_stop);
        Status(SERVICE_STOP_PENDING,45000);
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}
bool Stopping(){return g_stopping.load();}
bool IsConsoleAccount(const kb::Configuration& config,HANDLE& token){
    token=nullptr;
    DWORD session=WTSGetActiveConsoleSessionId();
    if(session==0xffffffff||!WTSQueryUserToken(session,&token))return false;
    DWORD needed=0;GetTokenInformation(token,TokenUser,nullptr,0,&needed);
    if(!needed||needed>16384){CloseHandle(token);token=nullptr;return false;}
    std::vector<std::uint8_t> buffer(needed);
    PSID expected=nullptr;
    const bool valid=GetTokenInformation(token,TokenUser,buffer.data(),needed,&needed)&&
        ConvertStringSidToSidW(config.userSid.c_str(),&expected);
    bool matching=valid&&EqualSid(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,expected);
    if(expected)LocalFree(expected);
    if(!matching){CloseHandle(token);token=nullptr;return false;}
    return true;
}
void LockConsole(const kb::Configuration& config){
    HANDLE token=nullptr;
    if(!IsConsoleAccount(config,token))return;
    wchar_t module[32768]{};
    if(!GetModuleFileNameW(nullptr,module,32768)){CloseHandle(token);return;}
    std::wstring executable=module;
    std::wstring args=L"\""+executable+L"\" --lock";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    startup.lpDesktop=const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION process{};
    const BOOL started=CreateProcessAsUserW(token,executable.c_str(),args.data(),nullptr,nullptr,
                FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);
    if(started){CloseHandle(process.hThread);CloseHandle(process.hProcess);}
    CloseHandle(token);
}
bool SettingsMatch(const kb::RemoteConfiguration& a,const kb::RemoteConfiguration& b){
    return a.enabled==b.enabled&&a.lockOnLastDisconnect==b.lockOnLastDisconnect&&a.trustedEventsOnly==b.trustedEventsOnly&&
        a.serverUrl==b.serverUrl&&a.deviceId==b.deviceId&&a.allowedPeer==b.allowedPeer&&
        a.apiKeyEncrypted==b.apiKeyEncrypted;
}
std::string Ascii(const std::wstring& w){
    std::string s;
    for(auto c:w){if(c<33||c>126)return {};s.push_back(static_cast<char>(c));}
    return s;
}
void Run(){
    kb::RemoteGrant grant(true);
    if(!grant.Available()||!grant.ChangedEvent()||!grant.ConfigurationChangedEvent())return;
    while(!Stopping()){
        kb::RemoteConfiguration config;
        kb::Configuration local;
        if(!kb::ReadRemoteConfiguration(config)||!config.enabled||!kb::ReadConfig(local)){
            grant.Clear();
            HANDLE events[2]{g_stop,grant.ConfigurationChangedEvent()};
            WaitForMultipleObjects(2,events,FALSE,30000);
            continue;
        }
        const auto deviceId=Ascii(config.deviceId),allowedPeer=config.allowedPeer.empty()?std::string{}:Ascii(config.allowedPeer);
        if(deviceId.empty()||(!config.allowedPeer.empty()&&allowedPeer.empty())){
            WaitForSingleObject(g_stop,5000);continue;
        }
        kenvix::remote::ConnectionTracker tracker(deviceId,allowedPeer,config.lockOnLastDisconnect,config.trustedEventsOnly);
        unsigned retry=3000;
        while(!Stopping()){
            kb::RemoteConfiguration current;
            if(!kb::ReadRemoteConfiguration(current)||!SettingsMatch(config,current))break;
            std::wstring error;
            auto onEvent=[&](const kenvix::remote::Event& event){
                const auto transition=tracker.Apply(event,std::chrono::system_clock::now());
                if(transition==kenvix::remote::Action::unlock){
                    // Only a locked, matching console account can receive a one-use grant.
                    HANDLE token=nullptr;
                    if(IsConsoleAccount(local,token)){
                        CloseHandle(token);
                        if(kb::IsTargetSessionLocked(local.username,local.domain)) grant.Publish();
                    }
                }else if(transition==kenvix::remote::Action::lock){
                    grant.Clear();LockConsole(local);
                }
            };
            const bool ended=kb::ReadRustDeskStream(config,[&]{
                return Stopping()||WaitForSingleObject(grant.ConfigurationChangedEvent(),0)==WAIT_OBJECT_0;
            },onEvent,error);
            (void)ended;
            grant.Clear();
            if(tracker.ConnectionLost()==kenvix::remote::Action::lock)LockConsole(local);
            if(Stopping())break;
            if(!kb::ReadRemoteConfiguration(current)||!SettingsMatch(config,current))break;
            if(!error.empty())OutputDebugStringW((L"Bluetooth Unlock RustDesk SSE: "+error+L"\n").c_str());
            HANDLE events[2]{g_stop,grant.ConfigurationChangedEvent()};
            DWORD wait=WaitForMultipleObjects(2,events,FALSE,retry);
            if(wait!=WAIT_TIMEOUT)break;
            retry=std::min(60000u,retry*2);
        }
        grant.Clear();
    }
    grant.Clear();
}
void WINAPI ServiceMain(DWORD,LPWSTR*){
    g_statusHandle=RegisterServiceCtrlHandlerExW(kServiceName,Control,nullptr);
    if(!g_statusHandle)return;
    Status(SERVICE_START_PENDING,30000);
    g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!g_stop){Status(SERVICE_STOPPED);return;}
    Status(SERVICE_RUNNING);
    Run();
    Status(SERVICE_STOPPED);
    CloseHandle(g_stop);g_stop=nullptr;
}
} // namespace
int kb::RunRustDeskService(){
    SERVICE_TABLE_ENTRYW table[]{
        {const_cast<LPWSTR>(kServiceName),ServiceMain},{nullptr,nullptr}};
    return StartServiceCtrlDispatcherW(table)?0:1;
}
