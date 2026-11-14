#include "remote_storage.h"
#include "tpm_protection.h"
#include "storage.h"
#include "remote_ipc.h"
#include <windows.h>
#include <dpapi.h>
#include <algorithm>
#include <limits>
namespace kb {
namespace {
constexpr std::uint32_t kMagic=0x5253554b; // KUSR, little-endian
constexpr std::uint32_t kVersion=1;
constexpr std::size_t kMaxBytes=16384;
constexpr wchar_t kEntropy[] = L"BluetoothUnlock.RemoteAPIKey.v2";
// Required for decrypting credentials saved by earlier releases.
constexpr wchar_t kLegacyEntropy[] = L"Kenvix.RemoteAPIKey.2026.v1";
void Put32(std::vector<std::uint8_t>& out,std::uint32_t value){
    for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>((value>>(i*8))&0xff));
}
bool Take32(const std::vector<std::uint8_t>& buf,std::size_t& pos,std::uint32_t& value){
    if(buf.size()-pos<4)return false;
    value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(buf[pos++])<<(i*8);
    return true;
}
void PutText(std::vector<std::uint8_t>& out,const std::wstring& value){
    Put32(out,static_cast<std::uint32_t>(value.size()));
    for(wchar_t c:value){out.push_back(static_cast<std::uint8_t>(c));out.push_back(static_cast<std::uint8_t>(c>>8));}
}
bool TakeText(const std::vector<std::uint8_t>& bytes,std::size_t& pos,std::wstring& value){
    std::uint32_t length=0;
    if(!Take32(bytes,pos,length)||length>1024||length>(bytes.size()-pos)/2)return false;
    value.clear();value.reserve(length);
    for(std::uint32_t i=0;i<length;++i){
        auto c=static_cast<wchar_t>(bytes[pos]|std::uint16_t(bytes[pos+1])<<8);
        if(c==0||c<0x20)return false;
        value.push_back(c);pos+=2;
    }
    return true;
}
bool ValidId(const std::wstring& id,bool required){
    if(id.empty())return !required;
    if(id.size()>253)return false;
    for(auto c:id)if(c<33||c>126||c==L'/'||c==L'\\'||c==L'?'||c==L'#')return false;
    return true;
}
bool Validate(const RemoteConfiguration& c){
    if(c.serverUrl.size()>1024||!ValidId(c.deviceId,c.enabled)||
       !ValidId(c.allowedPeer,false)||c.apiKeyEncrypted.size()>8192)return false;
    if(c.enabled){
        if(c.serverUrl.size()<9||c.serverUrl.substr(0,8)!=L"https://"||c.apiKeyEncrypted.empty())return false;
        if(c.serverUrl.find_first_of(L"\r\n\t")!=std::wstring::npos)return false;
    }
    return true;
}
} // namespace
std::wstring RemoteConfigurationPath(){
    std::wstring path=ConfigurationPath();
    auto pos=path.find_last_of(L"\\/");
    return pos==std::wstring::npos?std::wstring{}:path.substr(0,pos)+L"\\rustdesk.dat";
}
std::wstring LegacyRemoteConfigurationPath(){
    auto path=LegacyConfigurationPath();
    const auto pos=path.find_last_of(L"\\/");
    return pos==std::wstring::npos?std::wstring{}:path.substr(0,pos)+L"\\rustdesk.dat";
}
bool EncryptRemoteApiKey(const std::wstring& key,std::vector<std::uint8_t>& encrypted){
    if(key.size()<10||key.size()>1024||key.rfind(L"rdapi_",0)!=0)return false;
    for(wchar_t c:key)if(c<=32||c>126)return false;
    DATA_BLOB plain{static_cast<DWORD>((key.size()+1)*sizeof(wchar_t)),
         reinterpret_cast<BYTE*>(const_cast<wchar_t*>(key.c_str()))};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy)),
         reinterpret_cast<BYTE*>(const_cast<wchar_t*>(kEntropy))};
    DATA_BLOB cipher{};
    if(!CryptProtectData(&plain,L"Bluetooth Unlock RustDesk API Token",&entropy,nullptr,nullptr,
                         CRYPTPROTECT_LOCAL_MACHINE|CRYPTPROTECT_UI_FORBIDDEN,&cipher))return false;
    std::vector<std::uint8_t> dpapi(cipher.pbData,cipher.pbData+cipher.cbData);
    SecureZeroMemory(cipher.pbData,cipher.cbData);LocalFree(cipher.pbData);
    if(!ProtectWithTpm(dpapi,encrypted))encrypted=dpapi;
    if(!dpapi.empty())SecureZeroMemory(dpapi.data(),dpapi.size());
    return true;
}
bool DecryptRemoteApiKey(const std::vector<std::uint8_t>& encrypted,std::wstring& key){
    key.clear();if(encrypted.empty()||encrypted.size()>8192)return false;
    std::vector<std::uint8_t> unsealed;
    if(IsTpmProtected(encrypted)&&!UnprotectWithTpm(encrypted,unsealed))return false;
    const auto& cipherBytes=IsTpmProtected(encrypted)?unsealed:encrypted;
    DATA_BLOB cipher{static_cast<DWORD>(cipherBytes.size()),const_cast<BYTE*>(cipherBytes.data())};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy)),
        reinterpret_cast<BYTE*>(const_cast<wchar_t*>(kEntropy))};
    DATA_BLOB plain{};
    bool decrypted=CryptUnprotectData(&cipher,nullptr,&entropy,nullptr,nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN,&plain)!=0;
    if(!decrypted){
        DATA_BLOB legacyEntropy{static_cast<DWORD>(sizeof(kLegacyEntropy)),
            reinterpret_cast<BYTE*>(const_cast<wchar_t*>(kLegacyEntropy))};
        decrypted=CryptUnprotectData(&cipher,nullptr,&legacyEntropy,nullptr,nullptr,
                         CRYPTPROTECT_UI_FORBIDDEN,&plain)!=0;
    }
    if(!unsealed.empty())SecureZeroMemory(unsealed.data(),unsealed.size());
    if(!decrypted)return false;
    bool valid=plain.cbData>=sizeof(wchar_t)&&plain.cbData<=4096&&plain.cbData%sizeof(wchar_t)==0;
    if(valid){
        const auto count=plain.cbData/sizeof(wchar_t);
        auto* text=reinterpret_cast<const wchar_t*>(plain.pbData);
        valid=text[count-1]==L'\0'&&wcsnlen_s(text,count)==count-1;
        if(valid)key.assign(text,count-1);
    }
    SecureZeroMemory(plain.pbData,plain.cbData);LocalFree(plain.pbData);
    if(valid&&key.rfind(L"rdapi_",0)!=0){kb::Wipe(key);return false;}
    return valid;
}
bool ReadRemoteConfiguration(RemoteConfiguration& config){
    auto path=RemoteConfigurationPath();if(path.empty())return false;
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_FILE_NOT_FOUND){
        path=LegacyRemoteConfigurationPath();
        h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    }
    if(h==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};
    bool valid=GetFileSizeEx(h,&size)&&size.QuadPart>=20&&size.QuadPart<=kMaxBytes;
    std::vector<std::uint8_t> bytes;
    if(valid){
        bytes.resize(static_cast<std::size_t>(size.QuadPart));DWORD received=0;
        valid=ReadFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&received,nullptr)&&received==bytes.size();
    }
    CloseHandle(h);if(!valid)return false;
    std::size_t pos=0;std::uint32_t magic=0,version=0,flags=0,keyLen=0;
    RemoteConfiguration parsed;
    if(!Take32(bytes,pos,magic)||magic!=kMagic||!Take32(bytes,pos,version)||version!=kVersion||
       !Take32(bytes,pos,flags)||(flags&~7u)!=0||
       !TakeText(bytes,pos,parsed.serverUrl)||!TakeText(bytes,pos,parsed.deviceId)||
       !TakeText(bytes,pos,parsed.allowedPeer)||!Take32(bytes,pos,keyLen)||
       keyLen>8192||keyLen>bytes.size()-pos)return false;
    parsed.enabled=(flags&1)!=0;parsed.lockOnLastDisconnect=(flags&2)!=0;parsed.trustedEventsOnly=(flags&4)!=0;
    parsed.apiKeyEncrypted.assign(bytes.begin()+pos,bytes.begin()+pos+keyLen);pos+=keyLen;
    if(pos!=bytes.size()||!Validate(parsed))return false;
    config=std::move(parsed);return true;
}
bool SaveRemoteConfiguration(const RemoteConfiguration& config){
    if(!Validate(config))return false;
    // A Windows account has to be registered first; SaveConfig creates the restricted folder.
    Configuration local;
    if(!ReadConfig(local)||!EnsureConfigurationDirectory())return false;
    const auto target=RemoteConfigurationPath();if(target.empty())return false;
    std::vector<std::uint8_t> bytes;
    Put32(bytes,kMagic);Put32(bytes,kVersion);Put32(bytes,(config.enabled?1u:0u)|(config.lockOnLastDisconnect?2u:0u)|(config.trustedEventsOnly?4u:0u));
    PutText(bytes,config.serverUrl);PutText(bytes,config.deviceId);PutText(bytes,config.allowedPeer);
    Put32(bytes,static_cast<std::uint32_t>(config.apiKeyEncrypted.size()));
    bytes.insert(bytes.end(),config.apiKeyEncrypted.begin(),config.apiKeyEncrypted.end());
    if(bytes.size()>kMaxBytes)return false;
    const auto temp=target+L".tmp";
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;
    bool good=WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
    good=FlushFileBuffers(h)&&good;CloseHandle(h);
    if(good)good=MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!good)DeleteFileW(temp.c_str());
    if(good)SignalRemoteConfigChanged();
    return good;
}
void SignalRemoteConfigChanged(){
    HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,kRemoteConfigurationChangedEvent);
    if(event){SetEvent(event);CloseHandle(event);}
}
} // namespace kb
