#include "security.h"
#include <windows.h>
#include <bcrypt.h>
#include <wtsapi32.h>
#include <algorithm>
#include <vector>
#include <cwchar>
namespace kb {
namespace {
struct Algorithm { BCRYPT_ALG_HANDLE h{};
 explicit Algorithm(LPCWSTR name){BCryptOpenAlgorithmProvider(&h,name,nullptr,0);}
 ~Algorithm(){if(h)BCryptCloseAlgorithmProvider(h,0);} };
bool Sha256(std::span<const uint8_t> bytes,std::array<uint8_t,32>& digest){
  Algorithm alg(BCRYPT_SHA256_ALGORITHM);if(!alg.h)return false;
  return BCRYPT_SUCCESS(BCryptHash(alg.h,nullptr,0,const_cast<PUCHAR>(bytes.data()),
           static_cast<ULONG>(bytes.size()),digest.data(),static_cast<ULONG>(digest.size())));
}
std::wstring WideLower(std::wstring s){
  std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return s;
}
bool QuerySessionString(DWORD session,WTS_INFO_CLASS type,std::wstring& out){
  LPWSTR text=nullptr;DWORD len=0;
  if(!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,type,&text,&len))return false;
  out=text?text:L"";if(text)WTSFreeMemory(text);return !out.empty();
}
} // namespace
bool NewNonce(kenvix::Nonce& nonce){return BCRYPT_SUCCESS(BCryptGenRandom(nullptr,nonce.data(),
  static_cast<ULONG>(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG));}
bool VerifyPhoneSignature(const kenvix::PublicKey& pub,const kenvix::Nonce& nonce,
                          std::span<const uint8_t> der){
  if(!kenvix::ValidPublicKey(pub))return false;
  std::array<uint8_t,64> raw{};
  if(!kenvix::DerToP1363(der,raw))return false;
  std::array<uint8_t,32> hash{};
  const auto msg=kenvix::SignatureMessage(nonce);
  if(!Sha256(msg,hash))return false;
  Algorithm alg(BCRYPT_ECDSA_P256_ALGORITHM);if(!alg.h)return false;
  std::array<uint8_t,sizeof(BCRYPT_ECCKEY_BLOB)+64> blob{};
  auto* header=reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
  header->dwMagic=BCRYPT_ECDSA_PUBLIC_P256_MAGIC;header->cbKey=32;
  std::copy(pub.begin()+1,pub.end(),blob.begin()+sizeof(BCRYPT_ECCKEY_BLOB));
  BCRYPT_KEY_HANDLE key{};
  if(!BCRYPT_SUCCESS(BCryptImportKeyPair(alg.h,nullptr,BCRYPT_ECCPUBLIC_BLOB,&key,
      blob.data(),static_cast<ULONG>(blob.size()),0)))return false;
  const bool valid=BCRYPT_SUCCESS(BCryptVerifySignature(key,nullptr,hash.data(),
       static_cast<ULONG>(hash.size()),raw.data(),static_cast<ULONG>(raw.size()),0));
  BCryptDestroyKey(key);return valid;
}
std::wstring Fingerprint(const kenvix::PublicKey& pub){
  if(!kenvix::ValidPublicKey(pub))return L"";
  std::array<uint8_t,32> hash{};if(!Sha256(pub,hash))return L"";
  auto value=kenvix::Hex(std::span<const uint8_t>(hash.data(),8));
  return std::wstring(value.begin(),value.end());
}
bool IsTargetSessionLocked(const std::wstring& username,const std::wstring& domain){
  DWORD session=WTSGetActiveConsoleSessionId();if(session==0xFFFFFFFF)return false;
  std::wstring currentUser,currentDomain;
  if(!QuerySessionString(session,WTSUserName,currentUser)||
     !QuerySessionString(session,WTSDomainName,currentDomain))return false;
  if(WideLower(username)!=WideLower(currentUser))return false;
  std::wstring dom=domain;
  if(dom==L"."){
    wchar_t computer[MAX_COMPUTERNAME_LENGTH+1]{};DWORD len=MAX_COMPUTERNAME_LENGTH+1;
    if(!GetComputerNameW(computer,&len))return false;
    dom=computer;
  }
  if(WideLower(dom)!=WideLower(currentDomain))return false;
  LPWSTR infoRaw=nullptr;DWORD count=0;
  if(!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,WTSSessionInfoEx,
         &infoRaw,&count))return false;
  bool locked=false;
  if(infoRaw&&count>=sizeof(WTSINFOEXW)){
    auto* info=reinterpret_cast<const WTSINFOEXW*>(infoRaw);
    locked=(info->Level==1&&info->Data.WTSInfoExLevel1.SessionFlags==WTS_SESSIONSTATE_LOCK);
  }
  if(infoRaw)WTSFreeMemory(infoRaw);return locked;
}
} // namespace kb
