#include "remote_ipc.h"
#include <sddl.h>
#include <aclapi.h>
#include <vector>
namespace kb {
namespace {
// Service and LogonUI run as LOCAL SYSTEM. No standard user can publish an unlock grant.
constexpr wchar_t kSddl[]=L"O:SYG:SYD:P(A;;GA;;;SY)(A;;GR;;;BA)";
bool IsSystemOwned(HANDLE handle,SE_OBJECT_TYPE type){
    PSID owner=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
    auto err=GetSecurityInfo(handle,type,OWNER_SECURITY_INFORMATION,&owner,nullptr,nullptr,nullptr,&descriptor);
    if(err!=ERROR_SUCCESS)return false;
    BYTE systemSid[SECURITY_MAX_SID_SIZE]{};DWORD bytes=sizeof(systemSid);
    bool ok=CreateWellKnownSid(WinLocalSystemSid,nullptr,systemSid,&bytes)&&EqualSid(owner,systemSid);
    LocalFree(descriptor);return ok;
}
}
RemoteGrant::RemoteGrant(bool owner){
    if(owner){
        PSECURITY_DESCRIPTOR sd=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(kSddl,SDDL_REVISION_1,&sd,nullptr))return;
        SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(LONG64),kRemoteGrantName);
        if(mapping_&&!IsSystemOwned(mapping_,SE_KERNEL_OBJECT)){CloseHandle(mapping_);mapping_=nullptr;}
        if(mapping_)changed_=CreateEventW(&sa,FALSE,FALSE,kRemoteGrantChangedEvent);
        // Administrators may ask the service to reload its configuration;
        // they cannot modify the one-use unlock grant.
        PSECURITY_DESCRIPTOR configSd=nullptr;
        constexpr wchar_t configSddl[]=L"O:SYG:SYD:P(A;;GA;;;SY)(A;;GA;;;BA)";
        if(ConvertStringSecurityDescriptorToSecurityDescriptorW(configSddl,SDDL_REVISION_1,&configSd,nullptr)){
            SECURITY_ATTRIBUTES configSa{sizeof(configSa),configSd,FALSE};
            if(mapping_)configChanged_=CreateEventW(&configSa,FALSE,FALSE,kRemoteConfigurationChangedEvent);
            LocalFree(configSd);
        }
        if(changed_&&!IsSystemOwned(changed_,SE_KERNEL_OBJECT)){CloseHandle(changed_);changed_=nullptr;}
        if(configChanged_&&!IsSystemOwned(configChanged_,SE_KERNEL_OBJECT)){
            CloseHandle(configChanged_);configChanged_=nullptr;
        }
        LocalFree(sd);
    }else{
        mapping_=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,kRemoteGrantName);
        changed_=OpenEventW(SYNCHRONIZE,FALSE,kRemoteGrantChangedEvent);
    }
    if(mapping_)view_=reinterpret_cast<volatile LONG64*>(MapViewOfFile(mapping_,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(LONG64)));
    if(owner&&view_)Clear();
}
RemoteGrant::~RemoteGrant(){
    if(view_)UnmapViewOfFile(const_cast<LONG64*>(view_));
    if(mapping_)CloseHandle(mapping_);
    if(changed_)CloseHandle(changed_);
    if(configChanged_)CloseHandle(configChanged_);
}
bool RemoteGrant::Valid() const {
    if(!view_)return false;
    const auto expires=InterlockedCompareExchange64(const_cast<volatile LONG64*>(view_),0,0);
    return expires>0&&static_cast<ULONGLONG>(expires)>GetTickCount64();
}
bool RemoteGrant::Claim(){
    if(!view_)return false;
    for(;;){
        auto expires=InterlockedCompareExchange64(view_,0,0);
        if(expires<=0||static_cast<ULONGLONG>(expires)<=GetTickCount64())return false;
        if(InterlockedCompareExchange64(view_,0,expires)==expires)return true;
    }
}
void RemoteGrant::Clear(){if(view_)InterlockedExchange64(view_,0);}
void RemoteGrant::Publish(unsigned milliseconds){
    if(!view_||!changed_)return;
    InterlockedExchange64(view_,static_cast<LONG64>(GetTickCount64()+milliseconds));
    SetEvent(changed_);
}
} // namespace kb
