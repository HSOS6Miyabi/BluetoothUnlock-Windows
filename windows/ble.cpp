#include "ble.h"
#include "security.h"
#include "../core/ble_presence.h"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
namespace kb {
using namespace std::chrono_literals;
namespace W = winrt::Windows;
namespace B = W::Devices::Bluetooth;
namespace A = W::Devices::Bluetooth::Advertisement;
namespace G = W::Devices::Bluetooth::GenericAttributeProfile;
namespace S = W::Storage::Streams;
namespace {
constexpr winrt::guid serviceId{0x5b0d6834,0xe3e9,0x48c0,{0x97,0x7a,0x87,0x7a,0x5d,0x1f,0x2b,0x45}};
constexpr winrt::guid publicKeyId{0x5b0d6835,0xe3e9,0x48c0,{0x97,0x7a,0x87,0x7a,0x5d,0x1f,0x2b,0x45}};
constexpr winrt::guid challengeId{0x5b0d6836,0xe3e9,0x48c0,{0x97,0x7a,0x87,0x7a,0x5d,0x1f,0x2b,0x45}};
constexpr winrt::guid responseId{0x5b0d6837,0xe3e9,0x48c0,{0x97,0x7a,0x87,0x7a,0x5d,0x1f,0x2b,0x45}};
template<class T> bool Await(winrt::Windows::Foundation::IAsyncOperation<T> operation,T& value,
                            std::chrono::milliseconds timeout=3500ms){
  try {
    if(operation.wait_for(timeout)!=W::Foundation::AsyncStatus::Completed){operation.Cancel();return false;}
    value=operation.GetResults();return true;
  }catch(...){return false;}
}
std::vector<uint8_t> ToBytes(const S::IBuffer& buffer){
  std::vector<uint8_t> result(buffer.Length());
  S::DataReader reader=S::DataReader::FromBuffer(buffer);
  if(!result.empty())reader.ReadBytes(winrt::array_view<uint8_t>(result));return result;
}
S::IBuffer ToBuffer(std::span<const uint8_t> bytes){
  S::DataWriter writer;
  writer.WriteBytes(winrt::array_view<const uint8_t>(bytes.data(),bytes.data()+bytes.size()));
  return writer.DetachBuffer();
}
struct PhoneGatt {
  B::BluetoothLEDevice device{nullptr};
  G::GattDeviceService service{nullptr};
  G::GattCharacteristic pub{nullptr},challenge{nullptr},response{nullptr};
  ~PhoneGatt(){if(service)service.Close();if(device)device.Close();}
};
bool GetCharacteristic(G::GattDeviceService service,const winrt::guid& id,G::GattCharacteristic& out){
  G::GattCharacteristicsResult res{nullptr};
  if(!Await(service.GetCharacteristicsForUuidAsync(id,B::BluetoothCacheMode::Uncached),res))return false;
  if(res.Status()!=G::GattCommunicationStatus::Success||res.Characteristics().Size()==0)return false;
  out=res.Characteristics().GetAt(0);return true;
}
bool OpenPhone(uint64_t address,PhoneGatt& out){
  if(!Await(B::BluetoothLEDevice::FromBluetoothAddressAsync(address),out.device)||!out.device)return false;
  G::GattDeviceServicesResult services{nullptr};
  if(!Await(out.device.GetGattServicesForUuidAsync(serviceId,B::BluetoothCacheMode::Uncached),services)||
     services.Status()!=G::GattCommunicationStatus::Success||services.Services().Size()==0)return false;
  out.service=services.Services().GetAt(0);
  return GetCharacteristic(out.service,publicKeyId,out.pub)&&
         GetCharacteristic(out.service,challengeId,out.challenge)&&
         GetCharacteristic(out.service,responseId,out.response);
}
bool ReadChar(G::GattCharacteristic& characteristic,std::vector<uint8_t>& result){
  G::GattReadResult read{nullptr};
  if(!Await(characteristic.ReadValueAsync(B::BluetoothCacheMode::Uncached),read)||
       read.Status()!=G::GattCommunicationStatus::Success)return false;
  result=ToBytes(read.Value());return true;
}
bool ReadKey(PhoneGatt& gatt,kenvix::PublicKey& key){
  std::vector<uint8_t> bytes;
  if(!ReadChar(gatt.pub,bytes)||!kenvix::ValidPublicKey(bytes))return false;
  std::copy(bytes.begin(),bytes.end(),key.begin());return true;
}
} // namespace
struct BleScanner::Impl {
  struct State {
    std::mutex mutex;
    ble::PresenceCache cache;
  };
  std::shared_ptr<State> state=std::make_shared<State>();
  A::BluetoothLEAdvertisementWatcher watcher{nullptr};
  winrt::event_token received{};
  std::chrono::steady_clock::time_point lastRetry{};
  bool started{};
};
BleScanner::BleScanner():impl_(std::make_unique<Impl>()){}
BleScanner::~BleScanner(){Stop();}
void BleScanner::Start(){
  if(impl_->started)return;
  auto watcher=A::BluetoothLEAdvertisementWatcher{};
  // Active scanning requests the scan response, which normally carries the name.
  watcher.ScanningMode(A::BluetoothLEScanningMode::Active);
  auto weak=std::weak_ptr<Impl::State>(impl_->state);
  const auto token=watcher.Received([weak](auto const&,A::BluetoothLEAdvertisementReceivedEventArgs const& args){
    try{
      auto state=weak.lock();
      if(!state)return;
      const int rssi=args.RawSignalStrengthInDBm();
      const std::wstring name(args.Advertisement().LocalName().c_str());
      bool phone=false;
      for(const auto& uuid:args.Advertisement().ServiceUuids()){
        if(uuid==serviceId){phone=true;break;}
      }
      std::lock_guard lock(state->mutex);
      state->cache.Observe(args.BluetoothAddress(),rssi,name,phone,
                            std::chrono::steady_clock::now());
    }catch(...){/* Ignore malformed radio events. */}
  });
  try{
    watcher.Start();
    impl_->watcher=watcher;
    impl_->received=token;
    impl_->started=true;
    impl_->lastRetry=std::chrono::steady_clock::now();
  }catch(...){
    watcher.Received(token);
    throw;
  }
}
bool BleScanner::EnsureRunning(){
  try{
    if(impl_->started &&
        impl_->watcher.Status()==A::BluetoothLEAdvertisementWatcherStatus::Started)return true;
    const auto now=std::chrono::steady_clock::now();
    if(impl_->lastRetry!=std::chrono::steady_clock::time_point{} &&
       now-impl_->lastRetry<10s)return false;
    impl_->lastRetry=now;
    if(!impl_->started){Start();return true;}
    if(impl_->watcher.Status()==A::BluetoothLEAdvertisementWatcherStatus::Stopping)return false;
    impl_->watcher.Start();
    return impl_->watcher.Status()==A::BluetoothLEAdvertisementWatcherStatus::Started;
  }catch(...){return false;}
}
void BleScanner::Stop(){
  if(!impl_->started)return;
  impl_->watcher.Received(impl_->received);
  try{impl_->watcher.Stop();}catch(...){}
  impl_->watcher=nullptr;
  impl_->started=false;
  {std::lock_guard lock(impl_->state->mutex);impl_->state->cache.Clear();}
}
std::vector<SeenDevice> BleScanner::Snapshot() const {
  std::vector<SeenDevice> result;
  auto& state=*impl_->state;
  std::lock_guard lock(state.mutex);
  for(auto& item:state.cache.Snapshot(std::chrono::steady_clock::now())){
    result.push_back({item.address,std::move(item.name),item.rssi,item.observations,
                      item.phoneService,item.fresh,item.missedWindows});
  }
  return result;
}
bool IsPairedBleDevice(uint64_t address){
  try{
    B::BluetoothLEDevice device{nullptr};
    if(!Await(B::BluetoothLEDevice::FromBluetoothAddressAsync(address),device)||!device)return false;
    const bool paired=device.DeviceInformation().Pairing().IsPaired();device.Close();return paired;
  }catch(...){return false;}
}
bool ReadPhonePublicKey(uint64_t address,kenvix::PublicKey& key){
  try{PhoneGatt connection;return OpenPhone(address,connection)&&ReadKey(connection,key);}
  catch(...){return false;}
}
bool AuthenticatePhone(uint64_t address,const kenvix::PublicKey& expected,
                       const kenvix::Nonce& nonce,const std::function<bool()>& stop){
  try{
    if(stop())return false;
    PhoneGatt connection;
    if(!OpenPhone(address,connection)||stop())return false;
    kenvix::PublicKey actual{};
    if(!ReadKey(connection,actual)||actual!=expected||stop())return false;
    // Keep each ATT write below the default 23-byte MTU (20-byte payload).
    for(const auto& fragment:kenvix::ChallengeFragments(nonce)) {
      G::GattWriteResult result{nullptr};
      if(!Await(connection.challenge.WriteValueWithResultAsync(
            ToBuffer(fragment),G::GattWriteOption::WriteWithResponse),result,3500ms)||
         result.Status()!=G::GattCommunicationStatus::Success)return false;
    }
    const auto deadline=std::chrono::steady_clock::now()+25s;
    while(std::chrono::steady_clock::now()<deadline && !stop()){
      std::vector<uint8_t> bytes;kenvix::Response answer;
      if(!ReadChar(connection.response,bytes)||!kenvix::ParseResponse(bytes,answer))return false;
      if(answer.status==kenvix::AuthStatus::rejected)return false;
      if(answer.status==kenvix::AuthStatus::signed_response){
        return VerifyPhoneSignature(expected,nonce,answer.signatureDer);
      }
      std::this_thread::sleep_for(450ms);
    }
  }catch(...){}
  return false;
}
} // namespace kb
