#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::ble {
using Clock = std::chrono::steady_clock;

// A visible row may remain on screen after its most recent radio observation.
// Only fresh observations are eligible for proximity-based authorization.
struct Device {
    std::uint64_t address{};
    std::wstring name;
    int rssi{-127};
    unsigned observations{};
    bool phoneService{};
    bool fresh{};
    unsigned missedWindows{};
};

class PresenceCache {
public:
    static constexpr auto freshWindow = std::chrono::seconds(4);
    static constexpr auto scanWindow = std::chrono::seconds(30);
    static constexpr unsigned requiredMissingWindows = 6;
    static constexpr auto retention = scanWindow * requiredMissingWindows;
    static constexpr std::size_t maximumDevices = 256;

    void Observe(std::uint64_t address, int rssi, std::wstring_view advertisedName,
                 bool phoneService, Clock::time_point at);
    std::vector<Device> Snapshot(Clock::time_point at) const;
    void Clear();

private:
    struct Sample {
        std::wstring name;
        int rssi{-127};
        unsigned observations{};
        bool phoneService{};
        Clock::time_point seen{};
    };
    std::unordered_map<std::uint64_t, Sample> samples_;
};
} // namespace kb::ble