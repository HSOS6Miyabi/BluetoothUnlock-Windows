#include "ble.h"
#include "security.h"
#include "storage.h"
#include "remote_storage.h"
#include "remote_ipc.h"
#include <windows.h>
#include <credentialprovider.h>
#include <ntsecapi.h>
#include <wincred.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <unordered_map>
#include <cstring>
#pragma comment(lib,"ole32.lib")
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;
namespace {
// {12E137F0-91AB-4B74-A37F-019B284A5C8E}
const GUID kProviderClsid={0x12e137f0,0x91ab,0x4b74,{0xa3,0x7f,0x01,0x9b,0x28,0x4a,0x5c,0x8e}};
std::atomic<long> g_objects{0};
std::atomic<long> g_locks{0};
enum Field : DWORD { fTitle=0, fStatus=1, fSubmit=2, fCount=3 };
struct Shared {
  // 0: none; 1: BLE; 2: RustDesk API. Only one method may claim readiness.
  std::atomic<int> authorization{0};
  std::atomic<bool> consumed{false};
  std::atomic<bool> failed{false};
};
LPWSTR CopyWide(const std::wstring& text){
  const size_t bytes=(text.size()+1)*sizeof(wchar_t);
  auto p=static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
  if(p)memcpy(p,text.c_str(),bytes);return p;
}
HRESULT PackLogon(const kb::Configuration& config,CREDENTIAL_PROVIDER_USAGE_SCENARIO scenario,
                  CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* serialized){
  std::wstring password;
  if(!kb::DecryptPassword(config.passwordEncrypted,password))return E_ACCESSDENIED;
  std::wstring protectedPass;
  DWORD count=0;CRED_PROTECTION_TYPE type{};
  CredProtectW(FALSE,const_cast<LPWSTR>(password.c_str()),static_cast<DWORD>(password.size()+1),
               nullptr,&count,&type);
  if(count>0&&count<16384){
    std::vector<wchar_t> result(count+1);
    if(CredProtectW(FALSE,const_cast<LPWSTR>(password.c_str()),
          static_cast<DWORD>(password.size()+1),result.data(),&count,&type)){
      protectedPass.assign(result.data());
    }
    SecureZeroMemory(result.data(),result.size()*sizeof(wchar_t));
  }
  // Some policies do not permit CredProtectW; the standard Kerberos packing
  // format also accepts a password without a CredProtectW wrapper.
  if(protectedPass.empty())protectedPass=password;
  kb::Wipe(password);
  if(config.username.size()>256||config.domain.size()>256||protectedPass.size()>4096){
    kb::Wipe(protectedPass);return E_INVALIDARG;
  }
  // KERB_INTERACTIVE_UNLOCK_LOGON expects the real machine name for local users.
  std::wstring logonDomain=config.domain;
  if(logonDomain==L"."){
    wchar_t computer[MAX_COMPUTERNAME_LENGTH+1]{};
    DWORD length=MAX_COMPUTERNAME_LENGTH+1;
    if(!GetComputerNameW(computer,&length)){
      kb::Wipe(protectedPass);return HRESULT_FROM_WIN32(GetLastError());
    }
    logonDomain.assign(computer,length);
  }
  constexpr size_t base=sizeof(KERB_INTERACTIVE_UNLOCK_LOGON);
  const size_t dBytes=logonDomain.size()*sizeof(wchar_t);
  const size_t uBytes=config.username.size()*sizeof(wchar_t);
  const size_t pBytes=protectedPass.size()*sizeof(wchar_t);
  const size_t total=base+dBytes+uBytes+pBytes;
  if(total>0xffff){kb::Wipe(protectedPass);return E_INVALIDARG;}
  auto* blob=static_cast<BYTE*>(CoTaskMemAlloc(total));
  if(!blob){kb::Wipe(protectedPass);return E_OUTOFMEMORY;}
  ZeroMemory(blob,total);
  auto* login=reinterpret_cast<KERB_INTERACTIVE_UNLOCK_LOGON*>(blob);
  login->Logon.MessageType=scenario==CPUS_UNLOCK_WORKSTATION?
        KerbWorkstationUnlockLogon:KerbInteractiveLogon;
  auto pack=[&](UNICODE_STRING& target,const std::wstring& content,size_t offset){
    const auto length=static_cast<USHORT>(content.size()*sizeof(wchar_t));
    target.Length=length;target.MaximumLength=length;
    target.Buffer=reinterpret_cast<PWSTR>(offset);
    if(length)memcpy(blob+offset,content.data(),length);
  };
  pack(login->Logon.LogonDomainName,logonDomain,base);
  pack(login->Logon.UserName,config.username,base+dBytes);
  pack(login->Logon.Password,protectedPass,base+dBytes+uBytes);
  kb::Wipe(protectedPass);
  LSA_HANDLE lsa{};
  NTSTATUS status=LsaConnectUntrusted(&lsa);
  if(status!=0){SecureZeroMemory(blob,total);CoTaskMemFree(blob);return HRESULT_FROM_NT(status);}
  LSA_STRING pkg{};char name[]="Negotiate";
  pkg.Buffer=name;pkg.Length=static_cast<USHORT>(strlen(name));pkg.MaximumLength=pkg.Length+1;
  ULONG package{};status=LsaLookupAuthenticationPackage(lsa,&pkg,&package);
  LsaDeregisterLogonProcess(lsa);
  if(status!=0){SecureZeroMemory(blob,total);CoTaskMemFree(blob);return HRESULT_FROM_NT(status);}
  serialized->ulAuthenticationPackage=package;
  serialized->clsidCredentialProvider=kProviderClsid;
  serialized->cbSerialization=static_cast<ULONG>(total);
  serialized->rgbSerialization=blob;
  return S_OK;
}
class Credential final:public ICredentialProviderCredential2 {
public:
  Credential(std::shared_ptr<Shared> state,const kb::Configuration& cfg,CREDENTIAL_PROVIDER_USAGE_SCENARIO scenario)
    :state_(std::move(state)),cfg_(cfg),scenario_(scenario){++g_objects;}
  ~Credential(){--g_objects;}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** obj)override{
    if(!obj)return E_POINTER;*obj=nullptr;
    if(iid==IID_IUnknown||iid==__uuidof(ICredentialProviderCredential)||
       iid==__uuidof(ICredentialProviderCredential2)){
      *obj=static_cast<ICredentialProviderCredential2*>(this);AddRef();return S_OK;}
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef()override{return static_cast<ULONG>(InterlockedIncrement(&refs_));}
  ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE Advise(ICredentialProviderCredentialEvents*)override{return S_OK;}
  HRESULT STDMETHODCALLTYPE UnAdvise()override{return S_OK;}
  HRESULT STDMETHODCALLTYPE SetSelected(BOOL* autoLogon)override{
    if(!autoLogon)return E_POINTER;
    *autoLogon=(state_->authorization.load()!=0&&!state_->consumed.load())?TRUE:FALSE;return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetDeselected()override{return S_OK;}
  HRESULT STDMETHODCALLTYPE GetFieldState(DWORD field,CREDENTIAL_PROVIDER_FIELD_STATE* state,
                                         CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* interactive)override{
    if(field>=fCount||!state||!interactive)return E_INVALIDARG;
    *state=field==fTitle?CPFS_DISPLAY_IN_BOTH:CPFS_DISPLAY_IN_SELECTED_TILE;
    *interactive=CPFIS_NONE;return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetStringValue(DWORD field,LPWSTR* text)override{
    if(!text)return E_POINTER;*text=nullptr;
    std::wstring value;
    switch(field){
      case fTitle:value=L"Kenvix · 智能解锁";break;
      case fStatus:
        value=state_->failed.load()?L"身份验证失败，请使用密码登录并检查设置":
         state_->authorization.load()==2?L"RustDesk 连接认证成功，正在解锁":
         state_->authorization.load()==1?L"蓝牙认证成功，正在解锁":L"正在等待已授权设备或远程连接…";break;
      case fSubmit:value=L"检查认证";break;
      default:return E_INVALIDARG;
    }
    *text=CopyWide(value);return *text?S_OK:E_OUTOFMEMORY;
  }
  HRESULT STDMETHODCALLTYPE GetBitmapValue(DWORD,HBITMAP*)override{return E_INVALIDARG;}
  HRESULT STDMETHODCALLTYPE GetCheckboxValue(DWORD,BOOL*,LPWSTR*)override{return E_INVALIDARG;}
  HRESULT STDMETHODCALLTYPE GetSubmitButtonValue(DWORD field,DWORD* adjacent)override{
    if(field!=fSubmit||!adjacent)return E_INVALIDARG;*adjacent=fStatus;return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetComboBoxValueCount(DWORD,DWORD*,DWORD*)override{return E_INVALIDARG;}
  HRESULT STDMETHODCALLTYPE GetComboBoxValueAt(DWORD,DWORD,LPWSTR*)override{return E_INVALIDARG;}
  HRESULT STDMETHODCALLTYPE SetStringValue(DWORD,LPCWSTR)override{return E_INVALIDARG;}
  HRESULT STDMETHODCALLTYPE SetCheckboxValue(DWORD,BOOL)override{return E_INVALIDARG;}
  HRESULT STDMETHODCALLTYPE SetComboBoxSelectedValue(DWORD,DWORD)override{return E_INVALIDARG;}
  HRESULT STDMETHODCALLTYPE CommandLinkClicked(DWORD)override{return E_INVALIDARG;}
  HRESULT STDMETHODCALLTYPE GetSerialization(CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* response,
    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* serialized,LPWSTR* status,
    CREDENTIAL_PROVIDER_STATUS_ICON* icon)override{
    if(!response||!serialized||!status||!icon)return E_POINTER;
    *response=CPGSR_NO_CREDENTIAL_NOT_FINISHED;*status=nullptr;*icon=CPSI_NONE;
    ZeroMemory(serialized,sizeof(*serialized));
    const int authorization=state_->authorization.exchange(0);
    if(authorization==0||state_->consumed.exchange(true)){
      *status=CopyWide(L"正在等待已认证的解锁事件");return S_OK;
    }
    // Check the target console session and consume a one-use system-service grant.
    if(!kb::IsTargetSessionLocked(cfg_.username,cfg_.domain)){
      *status=CopyWide(L"当前没有匹配的已锁定用户会话");return S_OK;
    }
    if(authorization==2){
      kb::RemoteGrant grant(false);
      if(!grant.Claim()){
        *status=CopyWide(L"RustDesk 解锁许可已过期，请重新建立已认证连接");return S_OK;
      }
    }
    const auto hr=PackLogon(cfg_,scenario_,serialized);
    if(SUCCEEDED(hr)) *response=CPGSR_RETURN_CREDENTIAL_FINISHED;
    else {*status=CopyWide(L"无法读取登录凭据，请使用系统密码解锁");*icon=CPSI_ERROR;}
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE ReportResult(NTSTATUS status,NTSTATUS,
           LPWSTR* out,CREDENTIAL_PROVIDER_STATUS_ICON* icon)override{
    if(out)*out=nullptr;if(icon)*icon=CPSI_NONE;
    if(status!=0){state_->failed.store(true);state_->authorization.store(0);}
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetUserSid(LPWSTR* sid)override{
    if(!sid)return E_POINTER;*sid=CopyWide(cfg_.userSid);return *sid?S_OK:E_OUTOFMEMORY;
  }
private:
  LONG refs_{1};
  std::shared_ptr<Shared> state_;
  kb::Configuration cfg_;
  CREDENTIAL_PROVIDER_USAGE_SCENARIO scenario_;
};
class Provider final:public ICredentialProvider,public ICredentialProviderSetUserArray {
public:
  Provider():state_(std::make_shared<Shared>()){++g_objects;configured_=kb::ReadConfig(cfg_);}
  ~Provider(){UnAdvise();if(credential_)credential_->Release();--g_objects;}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** value)override{
    if(!value)return E_POINTER;*value=nullptr;
    if(iid==IID_IUnknown||iid==__uuidof(ICredentialProvider))
      *value=static_cast<ICredentialProvider*>(this);
    else if(iid==__uuidof(ICredentialProviderSetUserArray))
      *value=static_cast<ICredentialProviderSetUserArray*>(this);
    else return E_NOINTERFACE;
    AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef()override{return static_cast<ULONG>(InterlockedIncrement(&refs_));}
  ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO scenario,DWORD)override{
    if(scenario!=CPUS_LOGON&&scenario!=CPUS_UNLOCK_WORKSTATION)return E_NOTIMPL;
    scenario_=scenario;
    // Windows 10+ also sends CPUS_LOGON when a previously signed-in user unlocks.
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetSerialization(const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*)override{
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE SetUserArray(ICredentialProviderUserArray* users)override{
    users_=users;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Advise(ICredentialProviderEvents* events,UINT_PTR context)override{
    if(!events)return E_POINTER;
    UnAdvise();
    if(!configured_||scenario_==CPUS_INVALID)return S_OK;
    const auto cfg=cfg_;auto state=state_;
    if(cfg_.mode!=kb::Mode::remote){
    IStream* stream=nullptr;
    HRESULT hr=CoMarshalInterThreadInterfaceInStream(IID_ICredentialProviderEvents,events,&stream);
    if(FAILED(hr))return hr;
    worker_=std::jthread([cfg,state,context,stream](std::stop_token stop){
      struct ComApartment {
        HRESULT hr{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
        ~ComApartment(){if(SUCCEEDED(hr))CoUninitialize();}
      } apartment;
      if(FAILED(apartment.hr)){stream->Release();return;}
      ComPtr<ICredentialProviderEvents> proxy;
      ICredentialProviderEvents* raw=nullptr;
      if(SUCCEEDED(CoGetInterfaceAndReleaseStream(stream,IID_ICredentialProviderEvents,
                                                 reinterpret_cast<void**>(&raw))))proxy.Attach(raw);
      try{
        if(!kb::IsTargetSessionLocked(cfg.username,cfg.domain))return;
        kb::BleScanner scanner;scanner.Start();
        std::unordered_map<uint64_t,std::chrono::steady_clock::time_point> last;
        while(!stop.stop_requested()&&!state->consumed.load()&&state->authorization.load()==0){
          for(const auto& dev:scanner.Snapshot()){
            if(stop.stop_requested())break;
            if(!kenvix::PassesSignal(dev.rssi,cfg.minRssi,dev.observations,dev.fresh))continue;
            if(cfg.mode==kb::Mode::low && dev.address!=cfg.lowAddress)continue;
            if(cfg.mode==kb::Mode::high&&!dev.phoneService)continue;
            const auto now=std::chrono::steady_clock::now();
            if(last.contains(dev.address)&&now-last[dev.address]<8s)continue;
            last[dev.address]=now;
            bool verified=false;
            if(cfg.mode==kb::Mode::low)verified=kb::IsPairedBleDevice(dev.address);
            else{
              kenvix::Nonce nonce{};
              if(kb::NewNonce(nonce)) verified=kb::AuthenticatePhone(dev.address,cfg.trustedKey,nonce,
                         [&]{return stop.stop_requested();});
            }
            if(!verified||stop.stop_requested()||state->consumed.load())continue;
            // Connected peripherals may stop advertising during an authenticated GATT session.
            // In high-security mode, a successful live GATT challenge is still required.
            bool signalStillGood=cfg.mode==kb::Mode::high;
            for(const auto& current:scanner.Snapshot()){
              if(current.address==dev.address)
                signalStillGood=kenvix::PassesSignal(current.rssi,
                  cfg.minRssi,current.observations,current.fresh);
            }
            if(!signalStillGood||!kb::IsTargetSessionLocked(cfg.username,cfg.domain))continue;
            int expected=0;
            if(state->authorization.compare_exchange_strong(expected,1)){
              if(proxy)proxy->CredentialsChanged(context);
            }
            scanner.Stop();return;
          }
          // Bound the retry map to nearby devices seen recently.
          if(last.size()>128){
            for(auto it=last.begin();it!=last.end();){
              if(std::chrono::steady_clock::now()-it->second>30s)it=last.erase(it);
              else ++it;
            }
          }
          std::this_thread::sleep_for(350ms);
        }
        scanner.Stop();
      }catch(...){}
      proxy.Reset();
    });
    }
    kb::RemoteConfiguration remoteConfig;
    if(kb::ReadRemoteConfiguration(remoteConfig)&&remoteConfig.enabled){
      IStream* remoteStream=nullptr;
      if(SUCCEEDED(CoMarshalInterThreadInterfaceInStream(IID_ICredentialProviderEvents,events,&remoteStream))){
        remoteWorker_=std::jthread([cfg,state,context,remoteStream](std::stop_token stop){
          struct ComApartment {
            HRESULT hr{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
            ~ComApartment(){if(SUCCEEDED(hr))CoUninitialize();}
          } apartment;
          if(FAILED(apartment.hr)){remoteStream->Release();return;}
          ComPtr<ICredentialProviderEvents> proxy;
          ICredentialProviderEvents* raw=nullptr;
          if(SUCCEEDED(CoGetInterfaceAndReleaseStream(remoteStream,IID_ICredentialProviderEvents,
                                                     reinterpret_cast<void**>(&raw))))proxy.Attach(raw);
          while(!stop.stop_requested()&&!state->consumed.load()&&state->authorization.load()==0){
            kb::RemoteGrant grant(false);
            if(grant.Available()&&grant.Valid()&&kb::IsTargetSessionLocked(cfg.username,cfg.domain)){
              int expected=0;
              if(state->authorization.compare_exchange_strong(expected,2)){
                if(proxy)proxy->CredentialsChanged(context);
              }
              return;
            }
            if(grant.ChangedEvent())WaitForSingleObject(grant.ChangedEvent(),750);
            else std::this_thread::sleep_for(750ms);
          }
        });
      }
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE UnAdvise()override{
    if(remoteWorker_.joinable()){remoteWorker_.request_stop();remoteWorker_.join();}
    if(worker_.joinable()){worker_.request_stop();worker_.join();}
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetFieldDescriptorCount(DWORD* count)override{
    if(!count)return E_POINTER;*count=fCount;return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetFieldDescriptorAt(DWORD field,CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** descriptor)override{
    if(!descriptor)return E_POINTER;*descriptor=nullptr;if(field>=fCount)return E_INVALIDARG;
    constexpr CREDENTIAL_PROVIDER_FIELD_TYPE types[]={CPFT_LARGE_TEXT,CPFT_SMALL_TEXT,CPFT_SUBMIT_BUTTON};
    constexpr const wchar_t* names[]={L"Kenvix 智能解锁",L"认证状态",L"检查认证"};
    auto* value=static_cast<CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*>(CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR)));
    if(!value)return E_OUTOFMEMORY;ZeroMemory(value,sizeof(*value));
    value->dwFieldID=field;value->cpft=types[field];value->pszLabel=CopyWide(names[field]);
    if(!value->pszLabel){CoTaskMemFree(value);return E_OUTOFMEMORY;}
    *descriptor=value;return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetCredentialCount(DWORD* count,DWORD* selected,BOOL* autoLogon)override{
    if(!count||!selected||!autoLogon)return E_POINTER;
    *count=0;*selected=CREDENTIAL_PROVIDER_NO_DEFAULT;*autoLogon=FALSE;
    if(!configured_||scenario_==CPUS_INVALID)return S_OK;
    if(users_){
      DWORD number=0;if(FAILED(users_->GetCount(&number)))return S_OK;
      bool matching=false;
      for(DWORD i=0;i<number;++i){
        ComPtr<ICredentialProviderUser> user;
        if(FAILED(users_->GetAt(i,user.GetAddressOf())))continue;
        PWSTR sid=nullptr;
        if(SUCCEEDED(user->GetSid(&sid))&&sid){
          if(_wcsicmp(sid,cfg_.userSid.c_str())==0)matching=true;
          CoTaskMemFree(sid);
        }
      }
      if(!matching)return S_OK;
    }
    *count=1;*selected=0;
    *autoLogon=state_->authorization.load()!=0&&!state_->consumed.load()?TRUE:FALSE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetCredentialAt(DWORD index,ICredentialProviderCredential** value)override{
    if(!value)return E_POINTER;*value=nullptr;if(index!=0||!configured_)return E_INVALIDARG;
    if(!credential_)credential_=new(std::nothrow)Credential(state_,cfg_,scenario_);
    if(!credential_)return E_OUTOFMEMORY;
    credential_->AddRef();*value=static_cast<ICredentialProviderCredential*>(credential_);return S_OK;
  }
private:
  LONG refs_{1};
  kb::Configuration cfg_{};
  bool configured_{false};
  CREDENTIAL_PROVIDER_USAGE_SCENARIO scenario_{CPUS_INVALID};
  ComPtr<ICredentialProviderUserArray> users_;
  Credential* credential_{};
  std::shared_ptr<Shared> state_;
  std::jthread worker_{};
  std::jthread remoteWorker_{};
};
class Factory final:public IClassFactory {
public:
  Factory(){++g_objects;}~Factory(){--g_objects;}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** value)override{
    if(!value)return E_POINTER;*value=nullptr;
    if(iid!=IID_IUnknown&&iid!=IID_IClassFactory)return E_NOINTERFACE;
    *value=static_cast<IClassFactory*>(this);AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef()override{return static_cast<ULONG>(InterlockedIncrement(&refs_));}
  ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** value)override{
    if(outer)return CLASS_E_NOAGGREGATION;
    auto* provider=new(std::nothrow)Provider;
    if(!provider)return E_OUTOFMEMORY;
    const HRESULT result=provider->QueryInterface(iid,value);provider->Release();return result;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock)override{
    if(lock)++g_locks;else --g_locks;return S_OK;
  }
private:LONG refs_{1};
};
} // namespace
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid,REFIID iid,void** value){
  if(!value)return E_POINTER;*value=nullptr;
  if(clsid!=kProviderClsid)return CLASS_E_CLASSNOTAVAILABLE;
  auto* factory=new(std::nothrow)Factory;if(!factory)return E_OUTOFMEMORY;
  const HRESULT hr=factory->QueryInterface(iid,value);factory->Release();return hr;
}
extern "C" HRESULT __stdcall DllCanUnloadNow(){
  return g_objects.load()==0&&g_locks.load()==0?S_OK:S_FALSE;
}
