#include "protocol.h"
#include <cassert>
#include <iostream>
using namespace kenvix;
int main(){
  Nonce nonce{}; for(unsigned i=0;i<32;++i)nonce[i]=static_cast<uint8_t>(i);
  auto frame=ChallengeFrame(nonce);assert(frame.size()==33 && frame[0]==1 && frame[32]==31);
  auto fragments=ChallengeFragments(nonce);
  assert(fragments.size()==2 && fragments[0].size()==17 && fragments[1].size()==17);
  assert(fragments[0][0]==0x11 && fragments[1][0]==0x12);
  for(unsigned i=0;i<16;++i){
    assert(fragments[0][i+1]==i && fragments[1][i+1]==i+16);
  }
  auto msg=SignatureMessage(nonce);
  assert(std::string(msg.begin(),msg.begin()+21)=="KENVIX-BLE-UNLOCK/v1:");
  assert(msg.size()==53 && msg.back()==31);
  PublicKey key{}; key[0]=4;key[64]=1;assert(ValidPublicKey(key));key[0]=5;assert(!ValidPublicKey(key));
  assert(PassesSignal(-69,-70,2,true));assert(!PassesSignal(-71,-70,2,true));
  assert(!PassesSignal(-65,-70,1,true));assert(!PassesSignal(-65,-70,2,false));
  Response response{}; const uint8_t pending[]={1,0,0};
  assert(ParseResponse(pending,response));assert(response.status==AuthStatus::pending);
  const uint8_t bad[]={1,1,1,0};assert(!ParseResponse(bad,response));
  const uint8_t der[]={0x30,0x06,0x02,0x01,0x01,0x02,0x01,0x7f};
  std::array<uint8_t,64> sig{};assert(DerToP1363(der,sig));
  assert(sig[31]==1 && sig[63]==0x7f && sig[0]==0 && sig[32]==0);
  std::array<uint8_t,72> fullDer{};
  fullDer[0]=0x30;fullDer[1]=0x46;
  fullDer[2]=0x02;fullDer[3]=0x21;fullDer[4]=0x00;fullDer[5]=0x80;
  fullDer[37]=0x02;fullDer[38]=0x21;fullDer[39]=0x00;fullDer[40]=0xFF;
  assert(DerToP1363(fullDer,sig));
  assert(sig[0]==0x80 && sig[32]==0xFF && sig[1]==0 && sig[33]==0);
  const uint8_t neg[]={0x30,0x06,0x02,0x01,0x81,0x02,0x01,0x01};
  assert(!DerToP1363(neg,sig));
  const uint8_t nonCanonical[]={0x30,0x07,0x02,0x02,0x00,0x01,0x02,0x01,0x01};
  assert(!DerToP1363(nonCanonical,sig));
  const uint8_t shortSeq[]={0x30,0x05,0x02,0x01,0x01,0x02,0x01,0x01};
  assert(!DerToP1363(shortSeq,sig));
  const uint8_t goodResp[]={1,1,8,0x30,0x06,0x02,0x01,0x01,0x02,0x01,0x7f};
  assert(ParseResponse(goodResp,response));assert(response.signatureDer.size()==8);
  std::cout<<"protocol tests passed\n";
}
