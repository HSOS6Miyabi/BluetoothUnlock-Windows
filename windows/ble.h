#pragma once
#include "storage.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace kb {
struct SeenDevice {
  uint64_t address{};
  std::wstring name;
  int rssi{-127};
  unsigned observations{};
  bool phoneService{};
  bool fresh{};
  unsigned missedWindows{};
};
class BleScanner {
public:
  BleScanner(); ~BleScanner();
  BleScanner(const BleScanner&)=delete;BleScanner& operator=(const BleScanner&)=delete;
  void Start();void Stop();
  bool EnsureRunning();
  std::vector<SeenDevice> Snapshot() const;
private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
bool IsPairedBleDevice(uint64_t address);
bool ReadPhonePublicKey(uint64_t address,kenvix::PublicKey& key);
bool AuthenticatePhone(uint64_t address,const kenvix::PublicKey& expected,
                       const kenvix::Nonce& nonce,const std::function<bool()>& shouldStop);
} // namespace kb
