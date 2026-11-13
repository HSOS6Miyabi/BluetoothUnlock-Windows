#include "protocol.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace {
std::vector<uint8_t> Decode(const std::string& value){
    assert(value.size()%2==0);
    std::vector<uint8_t> bytes;
    for(size_t i=0;i<value.size();i+=2){
        auto one=[](char c){return c<='9'?c-'0':c<='F'?c-'A'+10:c-'a'+10;};
        bytes.push_back(static_cast<uint8_t>((one(value[i])<<4)|one(value[i+1])));
    }
    return bytes;
}
}
int main(){
    std::ifstream input("tests/interop_vectors.properties");
    assert(input.is_open());
    std::map<std::string,std::string> fixture;
    for(std::string line;std::getline(input,line);){
        const auto equals=line.find('=');
        if(equals!=std::string::npos)fixture[line.substr(0,equals)]=line.substr(equals+1);
    }
    const auto nonceBytes=Decode(fixture.at("nonce"));
    const auto pub=Decode(fixture.at("public_key"));
    const auto sig=Decode(fixture.at("signature_der"));
    assert(nonceBytes.size()==32&&pub.size()==65);
    kenvix::Nonce nonce{};
    std::copy(nonceBytes.begin(),nonceBytes.end(),nonce.begin());
    const auto message=kenvix::SignatureMessage(nonce);
    const auto md=Decode(fixture.at("message_sha256"));
    uint8_t digest[SHA256_DIGEST_LENGTH]{};
    SHA256(message.data(),message.size(),digest);
    assert(std::equal(md.begin(),md.end(),digest));
    auto context=std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)>(
        EVP_PKEY_CTX_new_from_name(nullptr,"EC",nullptr),EVP_PKEY_CTX_free);
    assert(context && EVP_PKEY_fromdata_init(context.get())==1);
    OSSL_PARAM params[]={
        OSSL_PARAM_utf8_string("group",const_cast<char*>("prime256v1"),0),
        OSSL_PARAM_octet_string("pub",const_cast<uint8_t*>(pub.data()),pub.size()),
        OSSL_PARAM_END
    };
    EVP_PKEY* raw=nullptr;
    assert(EVP_PKEY_fromdata(context.get(),&raw,EVP_PKEY_PUBLIC_KEY,params)==1);
    auto key=std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)>(raw,EVP_PKEY_free);
    auto digestContext=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    assert(digestContext&&EVP_DigestVerifyInit(digestContext.get(),nullptr,EVP_sha256(),nullptr,key.get())==1);
    assert(EVP_DigestVerify(digestContext.get(),sig.data(),sig.size(),message.data(),message.size())==1);
    assert(EVP_DigestVerifyInit(digestContext.get(),nullptr,EVP_sha256(),nullptr,key.get())==1);
    auto changed=nonce;changed[0]^=1;
    const auto other=kenvix::SignatureMessage(changed);
    assert(EVP_DigestVerify(digestContext.get(),sig.data(),sig.size(),other.data(),other.size())!=1);
}
