#include "ble_presence.h"
#include <algorithm>

namespace kb::ble {
namespace {
std::wstring CleanName(std::wstring_view name) {
    std::wstring clean;
    clean.reserve(std::min<std::size_t>(name.size(), 96));
    for (wchar_t value : name) {
        if (value < 0x20 || value == 0x7f ||
            (value >= 0x202a && value <= 0x202e) ||
            (value >= 0x2066 && value <= 0x2069)) continue;
        clean.push_back(value);
        if (clean.size() == 96) break;
    }
    const auto first = clean.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    const auto last = clean.find_last_not_of(L" \t");
    return clean.substr(first, last - first + 1);
}
}

void PresenceCache::Observe(std::uint64_t address, int rssi,
                            std::wstring_view advertisedName, bool phoneService,
                            Clock::time_point at) {
    // -127 means RSSI unavailable. Strong signals must not be filtered out.
    if (address == 0 || rssi <= -127 || rssi > 20) return;
    if (!samples_.contains(address)) {
        // Reclaim expired entries before evicting any recently observed devices.
        for (auto it = samples_.begin(); it != samples_.end();) {
            if (at >= it->second.seen && at - it->second.seen >= retention)
                it = samples_.erase(it);
            else ++it;
        }
        if (samples_.size() >= maximumDevices) {
            const auto oldest = std::min_element(samples_.begin(), samples_.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.second.seen < rhs.second.seen;
                });
            if (oldest != samples_.end()) samples_.erase(oldest);
        }
    }
    auto& sample = samples_[address];
    if (at < sample.seen || at - sample.seen > freshWindow) sample.observations = 0;
    sample.observations = std::min(sample.observations + 1u, 10u);
    sample.seen = at;
    sample.rssi = rssi;
    sample.phoneService = sample.phoneService || phoneService;
    const auto name = CleanName(advertisedName);
    if (!name.empty()) sample.name = name;
}

std::vector<Device> PresenceCache::Snapshot(Clock::time_point at) const {
    std::vector<Device> devices;
    devices.reserve(samples_.size());
    for (const auto& [address, sample] : samples_) {
        // Unnamed generic devices remain hidden; a device advertising this
        // application's service UUID remains discoverable without scan response.
        if (sample.name.empty() && !sample.phoneService) continue;
        const auto age = at >= sample.seen ? at - sample.seen : Clock::duration::zero();
        if (age >= retention) continue;
        const auto missed = static_cast<unsigned>(age / scanWindow);
        Device item{address,
            sample.name.empty() ? L"解锁服务设备（名称未广播）" : sample.name,
            sample.rssi,
            sample.observations,
            sample.phoneService,
            age <= freshWindow,
            missed};
        devices.push_back(std::move(item));
    }
    // Stable ordering prevents rows from jumping around as RSSI fluctuates.
    std::sort(devices.begin(), devices.end(), [](const Device& lhs, const Device& rhs) {
        if (lhs.phoneService != rhs.phoneService) return lhs.phoneService > rhs.phoneService;
        if (lhs.name != rhs.name) return lhs.name < rhs.name;
        return lhs.address < rhs.address;
    });
    return devices;
}

void PresenceCache::Clear() { samples_.clear(); }
} // namespace kb::ble