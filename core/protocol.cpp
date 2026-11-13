#include "protocol.h"
#include <algorithm>
#include <cstring>
namespace kenvix {
bool ValidPublicKey(std::span<const uint8_t> input) {
  if(input.size() != kPublicKeyBytes || input.front() != 0x04) return false;
  return std::any_of(input.begin()+1,input.end(),[](uint8_t b){return b!=0;});
}
std::array<uint8_t,33> ChallengeFrame(const Nonce& n) {
  std::array<uint8_t,33> out{}; out[0]=kVersion;
  std::copy(n.begin(),n.end(),out.begin()+1); return out;
}
std::array<std::array<uint8_t,17>,2> ChallengeFragments(const Nonce& nonce) {
  std::array<std::array<uint8_t,17>,2> result{};
  result[0][0]=0x11;
  result[1][0]=0x12;
  std::copy_n(nonce.begin(),16,result[0].begin()+1);
  std::copy_n(nonce.begin()+16,16,result[1].begin()+1);
  return result;
}
std::vector<uint8_t> SignatureMessage(const Nonce& n) {
  constexpr char prefix[]="KENVIX-BLE-UNLOCK/v1:";
  std::vector<uint8_t> out(prefix, prefix+sizeof(prefix)-1);
  out.insert(out.end(),n.begin(),n.end()); return out;
}
bool ParseResponse(std::span<const uint8_t> bytes, Response& out) {
  if(bytes.size()<3 || bytes[0]!=kVersion) return false;
  auto status=bytes[1];
  if(status>2 || bytes[2] != bytes.size()-3) return false;
  if(status==1) {
    if(bytes[2]<8 || bytes[2]>72) return false;
  } else if(bytes[2]!=0) return false;
  out.status=static_cast<AuthStatus>(status);
  out.signatureDer.assign(bytes.begin()+3,bytes.end());return true;
}
static bool parseInteger(std::span<const uint8_t> der,size_t& pos,std::span<uint8_t> destination) {
  if(pos+2>der.size() || der[pos++]!=0x02) return false;
  const size_t n=der[pos++];
  if(n==0 || n>33 || pos+n>der.size()) return false;
  const auto* p=der.data()+pos;
  if((p[0]&0x80)!=0) return false; // ASN.1 INTEGER must be positive
  if(n>1 && p[0]==0x00 && (p[1]&0x80)==0) return false; // non-canonical leading zero
  size_t skip=(p[0]==0x00)?1:0;
  if(n-skip>32) return false;
  std::fill(destination.begin(),destination.end(),0);
  std::copy(p+skip,p+n,destination.end()-(n-skip));pos+=n;return true;
}
bool DerToP1363(std::span<const uint8_t> der,std::array<uint8_t,64>& signature) {
  if(der.size()<8 || der.size()>72 || der[0]!=0x30 || der[1]!=der.size()-2) return false;
  std::array<uint8_t,64> temp{};
  size_t pos=2;
  if(!parseInteger(der,pos,std::span<uint8_t>(temp.data(),32))) return false;
  if(!parseInteger(der,pos,std::span<uint8_t>(temp.data()+32,32))) return false;
  if(pos!=der.size()) return false;
  signature=temp;return true;
}
bool PassesSignal(int rssi,int minimum,unsigned samples,bool fresh) {
  return fresh && samples>=2 && minimum>=-100 && minimum<=-35 && rssi>=minimum && rssi<=-15;
}
std::string Hex(std::span<const uint8_t> bytes) {
  constexpr char digits[]="0123456789ABCDEF";
  std::string out;out.reserve(bytes.size()*2);
  for(uint8_t x: bytes){out.push_back(digits[x>>4]);out.push_back(digits[x&15]);}
  return out;
}
} // namespace kenvix
