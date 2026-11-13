#include "protocol.h"
#include "security.h"
#include <windows.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace {
std::vector<uint8_t> HexDecode(const std::string& value) {
    assert(value.size() % 2 == 0);
    std::vector<uint8_t> bytes;
    bytes.reserve(value.size()/2);
    for (size_t i=0;i<value.size();i+=2) {
        const auto digit=[](char c)->uint8_t {
            if(c>='0'&&c<='9')return static_cast<uint8_t>(c-'0');
            if(c>='a'&&c<='f')return static_cast<uint8_t>(c-'a'+10);
            if(c>='A'&&c<='F')return static_cast<uint8_t>(c-'A'+10);
            assert(false);
            return 0;
        };
        bytes.push_back(static_cast<uint8_t>((digit(value[i])<<4)|digit(value[i+1])));
    }
    return bytes;
}
}
int main() {
    std::ifstream file("tests/interop_vectors.properties");
    assert(file.is_open());
    std::map<std::string,std::string> properties;
    for(std::string line;std::getline(file,line);) {
        const auto pos=line.find('=');
        if(pos!=std::string::npos)properties[line.substr(0,pos)]=line.substr(pos+1);
    }
    const auto nonceBytes=HexDecode(properties.at("nonce"));
    const auto publicBytes=HexDecode(properties.at("public_key"));
    auto signature=HexDecode(properties.at("signature_der"));
    assert(nonceBytes.size()==32 && publicBytes.size()==65);
    kenvix::Nonce nonce{};
    kenvix::PublicKey pub{};
    std::copy(nonceBytes.begin(),nonceBytes.end(),nonce.begin());
    std::copy(publicBytes.begin(),publicBytes.end(),pub.begin());
    assert(kb::Fingerprint(pub) == std::wstring(properties.at("fingerprint").begin(),properties.at("fingerprint").end()));
    assert(kb::VerifyPhoneSignature(pub,nonce,signature));
    auto frame=std::vector<uint8_t>{1,1,static_cast<uint8_t>(signature.size())};
    frame.insert(frame.end(),signature.begin(),signature.end());
    kenvix::Response answer{};
    assert(kenvix::ParseResponse(frame,answer));
    assert(answer.status==kenvix::AuthStatus::signed_response);
    assert(kb::VerifyPhoneSignature(pub,nonce,answer.signatureDer));
    auto changed=nonce;changed[0]^=1;
    assert(!kb::VerifyPhoneSignature(pub,changed,signature));
    signature.back()^=1;
    assert(!kb::VerifyPhoneSignature(pub,nonce,signature));
    const auto chunks=kenvix::ChallengeFragments(nonce);
    assert(chunks[0][0]==0x11 && chunks[1][0]==0x12);
    for(size_t i=0;i<16;++i) {
        assert(chunks[0][i+1]==nonce[i] && chunks[1][i+1]==nonce[16+i]);
    }
    return 0;
}
