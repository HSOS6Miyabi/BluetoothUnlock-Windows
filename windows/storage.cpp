#include "storage.h"
#include "tpm_protection.h"
#include <dpapi.h>
#include <sddl.h>
#include <aclapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cstring>
#include <limits>
namespace kb {
namespace {
constexpr uint32_t kMagic = 0x4B42554Cu; // KBUL
constexpr uint32_t kFormat = 2;
constexpr wchar_t kDirectory[] = L"\\BluetoothUnlock";
constexpr wchar_t kLegacyDirectory[] = L"\\Kenvix\\BluetoothUnlock";
constexpr wchar_t kFileName[] = L"\\configuration.dat";
constexpr size_t kMaxConfigSize = 32768;
void Put32(std::vector<uint8_t>& out,uint32_t value) {
  for(unsigned i=0;i<4;++i) out.push_back(static_cast<uint8_t>((value>>(8*i))&255));
}
void Put64(std::vector<uint8_t>& out,uint64_t value) {
  for(unsigned i=0;i<8;++i) out.push_back(static_cast<uint8_t>((value>>(8*i))&255));
}
bool Take32(const std::vector<uint8_t>& in,size_t& pos,uint32_t& out){
  if(pos+4>in.size())return false;
  out=0;for(unsigned i=0;i<4;++i)out|=uint32_t(in[pos++])<<(8*i);return true;
}
bool Take64(const std::vector<uint8_t>& in,size_t& pos,uint64_t& out){
  if(pos+8>in.size())return false;
  out=0;for(unsigned i=0;i<8;++i)out|=uint64_t(in[pos++])<<(8*i);return true;
}
void PutString(std::vector<uint8_t>& out,const std::wstring& s) {
  Put32(out,static_cast<uint32_t>(s.size()));
  for(wchar_t c:s){out.push_back(uint8_t(c&255));out.push_back(uint8_t((c>>8)&255));}
}
bool TakeString(const std::vector<uint8_t>& in,size_t& pos,std::wstring& s) {
  uint32_t len=0; if(!Take32(in,pos,len)||len>256||len>(in.size()-pos)/2)return false;
  s.clear();s.reserve(len);
  for(uint32_t i=0;i<len;++i){s.push_back(wchar_t(in[pos]|(uint16_t(in[pos+1])<<8)));pos+=2;}return true;
}
void PutBlob(std::vector<uint8_t>& out,const std::vector<uint8_t>& blob){
  Put32(out,static_cast<uint32_t>(blob.size()));out.insert(out.end(),blob.begin(),blob.end());
}
bool TakeBlob(const std::vector<uint8_t>& in,size_t& pos,std::vector<uint8_t>& out){
  uint32_t len=0;if(!Take32(in,pos,len)||len>kMaxConfigSize||len>in.size()-pos)return false;
  out.assign(in.begin()+pos,in.begin()+pos+len);pos+=len;return true;
}
std::wstring Root(){
  PWSTR temp=nullptr;
  if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData,0,nullptr,&temp)))return {};
  std::wstring out=temp;CoTaskMemFree(temp);return out;
}
bool SecureFolder(const std::wstring& path){
  auto root=Root(); if(root.empty())return false;
  // All files are restricted to SYSTEM and local administrators.
  PSECURITY_DESCRIPTOR descriptor=nullptr;
  constexpr wchar_t sddl[]=L"O:BAG:BAD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&descriptor,nullptr))return false;
  SECURITY_ATTRIBUTES sa{sizeof(sa),descriptor,FALSE};
   if(!CreateDirectoryW(path.c_str(),&sa) && GetLastError()!=ERROR_ALREADY_EXISTS){LocalFree(descriptor);return false;}
  PACL acl=nullptr; BOOL present=FALSE,defaulted=FALSE;
  bool ok=GetSecurityDescriptorDacl(descriptor,&present,&acl,&defaulted)&&present&&acl;
  if(ok)
    ok=SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,
      nullptr,nullptr,acl,nullptr)==ERROR_SUCCESS;
  LocalFree(descriptor);return ok;
}
} // namespace
std::wstring ConfigurationPath(){auto root=Root();return root.empty()?std::wstring{}:root+kDirectory+kFileName;}
std::wstring LegacyConfigurationPath(){auto root=Root();return root.empty()?std::wstring{}:root+kLegacyDirectory+kFileName;}
void Wipe(std::wstring& s){
  if(!s.empty())SecureZeroMemory(s.data(),s.size()*sizeof(wchar_t));s.clear();
}
bool EncryptPassword(const std::wstring& plain,std::vector<uint8_t>& encrypted){
  if(plain.empty()||plain.size()>1024)return false;
  DATA_BLOB input{static_cast<DWORD>((plain.size()+1)*sizeof(wchar_t)),
    reinterpret_cast<BYTE*>(const_cast<wchar_t*>(plain.c_str()))};
  DATA_BLOB output{};
  if(!CryptProtectData(&input,L"Kenvix Bluetooth Unlock",nullptr,nullptr,nullptr,
       CRYPTPROTECT_LOCAL_MACHINE|CRYPTPROTECT_UI_FORBIDDEN,&output)) return false;
  std::vector<uint8_t> dpapi(output.pbData,output.pbData+output.cbData);
  SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);
  if(!ProtectWithTpm(dpapi,encrypted)) encrypted=dpapi;
  if(!dpapi.empty())SecureZeroMemory(dpapi.data(),dpapi.size());
  return true;
}
bool DecryptPassword(const std::vector<uint8_t>& encrypted,std::wstring& plain){
  plain.clear();if(encrypted.empty()||encrypted.size()>kMaxConfigSize)return false;
  std::vector<uint8_t> unsealed;
  if(IsTpmProtected(encrypted)&&!UnprotectWithTpm(encrypted,unsealed))return false;
  const auto& cipher=IsTpmProtected(encrypted)?unsealed:encrypted;
  DATA_BLOB in{static_cast<DWORD>(cipher.size()),const_cast<BYTE*>(cipher.data())},out{};
  const bool decrypted=CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,
       CRYPTPROTECT_UI_FORBIDDEN,&out)!=0;
  if(!unsealed.empty())SecureZeroMemory(unsealed.data(),unsealed.size());
  if(!decrypted)return false;
  bool good=out.cbData>=sizeof(wchar_t)&&out.cbData%sizeof(wchar_t)==0&&out.cbData<=4096;
  if(good){
    auto count=out.cbData/sizeof(wchar_t);
    auto pw=reinterpret_cast<const wchar_t*>(out.pbData);
    good=(pw[count-1]==L'\0')&&wcsnlen_s(pw,count)==count-1;
    if(good)plain.assign(pw,count-1);
  }
  SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return good;
}
std::wstring ResolveUserSid(const std::wstring& user,const std::wstring& domain){
  std::wstring qualified=domain+L"\\"+user;
  DWORD sidBytes=0,domainLength=0;SID_NAME_USE use{};
  LookupAccountNameW(nullptr,qualified.c_str(),nullptr,&sidBytes,nullptr,&domainLength,&use);
  if(sidBytes==0||sidBytes>8192||domainLength>512)return {};
  std::vector<uint8_t> sid(sidBytes);std::vector<wchar_t> domainName(domainLength+1);
  if(!LookupAccountNameW(nullptr,qualified.c_str(),sid.data(),&sidBytes,
        domainName.data(),&domainLength,&use))return {};
  LPWSTR sidText=nullptr;
  if(!ConvertSidToStringSidW(sid.data(),&sidText))return {};
  std::wstring result=sidText;LocalFree(sidText);return result;
}
bool ValidatePassword(const std::wstring& user,const std::wstring& domain,const std::wstring& password){
  HANDLE token=nullptr;
  bool result=LogonUserW(user.c_str(),domain.c_str(),password.c_str(),
    LOGON32_LOGON_INTERACTIVE,LOGON32_PROVIDER_DEFAULT,&token)!=0;
  if(token)CloseHandle(token);return result;
}
bool ReadConfig(Configuration& out){
  auto path=ConfigurationPath();if(path.empty())return false;
  HANDLE handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(handle==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_FILE_NOT_FOUND){
    path=LegacyConfigurationPath();
    handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  }
  if(handle==INVALID_HANDLE_VALUE)return false;
  LARGE_INTEGER bytes{}; bool ok=GetFileSizeEx(handle,&bytes)&&bytes.QuadPart>0&&bytes.QuadPart<=kMaxConfigSize;
  std::vector<uint8_t> file;
  if(ok){file.resize(static_cast<size_t>(bytes.QuadPart));DWORD got=0;
    ok=ReadFile(handle,file.data(),static_cast<DWORD>(file.size()),&got,nullptr)&&got==file.size();}
  CloseHandle(handle);if(!ok)return false;
  size_t pos=0;uint32_t magic=0,version=0,mode=0,rssi=0;
  Configuration cfg;
  if(!Take32(file,pos,magic)||magic!=kMagic||!Take32(file,pos,version)||version!=kFormat||
     !Take32(file,pos,mode)||(mode!=1&&mode!=2&&mode!=3)||!Take32(file,pos,rssi)||
     !Take64(file,pos,cfg.lowAddress)||pos+65>file.size())return false;
  cfg.mode=static_cast<Mode>(mode);cfg.minRssi=static_cast<int32_t>(rssi);
  std::copy_n(file.begin()+pos,65,cfg.trustedKey.begin());pos+=65;
  if(cfg.minRssi<-100||cfg.minRssi>-35||!TakeString(file,pos,cfg.username)||
     !TakeString(file,pos,cfg.domain)||!TakeString(file,pos,cfg.userSid)||!TakeBlob(file,pos,cfg.passwordEncrypted)||
     pos!=file.size()||cfg.username.empty()||cfg.domain.empty()||cfg.userSid.empty()||cfg.passwordEncrypted.empty())return false;
  if(cfg.mode==Mode::high&&!kenvix::ValidPublicKey(cfg.trustedKey))return false;
  if(cfg.mode==Mode::low&&cfg.lowAddress==0)return false;
  out=std::move(cfg);return true;
}
bool SaveConfig(const Configuration& cfg){
  if(cfg.minRssi<-100||cfg.minRssi>-35||cfg.username.empty()||cfg.username.size()>128||
     cfg.domain.empty()||cfg.domain.size()>128||cfg.userSid.empty()||cfg.userSid.size()>256||cfg.passwordEncrypted.empty()||
     cfg.passwordEncrypted.size()>8192||
     (cfg.mode!=Mode::low&&cfg.mode!=Mode::high&&cfg.mode!=Mode::remote)||
     (cfg.mode==Mode::high&&!kenvix::ValidPublicKey(cfg.trustedKey))||
     (cfg.mode==Mode::low&&cfg.lowAddress==0))return false;
  auto root=Root();if(root.empty())return false;
  const auto directory=root+kDirectory;
  if(!SecureFolder(directory))return false;
  std::vector<uint8_t> data;
  Put32(data,kMagic);Put32(data,kFormat);Put32(data,static_cast<uint32_t>(cfg.mode));
  Put32(data,static_cast<uint32_t>(cfg.minRssi));Put64(data,cfg.lowAddress);
  data.insert(data.end(),cfg.trustedKey.begin(),cfg.trustedKey.end());
  PutString(data,cfg.username);PutString(data,cfg.domain);PutString(data,cfg.userSid);PutBlob(data,cfg.passwordEncrypted);
  const auto target=ConfigurationPath();const auto temp=target+L".tmp";
  HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
  if(file==INVALID_HANDLE_VALUE)return false;
  DWORD wrote=0;bool ok=WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&wrote,nullptr)&&wrote==data.size();
  ok=FlushFileBuffers(file)&&ok;CloseHandle(file);
  if(ok)ok=MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
  if(!ok)DeleteFileW(temp.c_str());return ok;
}
} // namespace kb
