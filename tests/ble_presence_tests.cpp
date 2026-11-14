#include "ble_presence.h"
#include <stdexcept>
#include <chrono>
#include <iostream>
#include <string>
using namespace std::chrono_literals;

void Check(bool condition) {
    if (!condition) throw std::runtime_error("BLE presence regression");
}

int main() {
    const auto zero = kb::ble::Clock::time_point{};
    kb::ble::PresenceCache cache;
    cache.Observe(1, -52, L"Pixel 9", false, zero);
    auto rows = cache.Snapshot(zero + 500ms);
    Check(rows.size() == 1 && rows[0].name == L"Pixel 9" && rows[0].fresh);
    cache.Observe(1, -50, L"", false, zero + 2s); // scan-response name remains cached
    rows = cache.Snapshot(zero + 2s);
    Check(rows.size() == 1 && rows[0].name == L"Pixel 9" && rows[0].observations == 2);
    rows = cache.Snapshot(zero + 31s);
    Check(rows.size() == 1 && !rows[0].fresh && rows[0].missedWindows == 0);
    rows = cache.Snapshot(zero + 62s);
    Check(rows.size() == 1 && rows[0].missedWindows == 2);
    rows = cache.Snapshot(zero + 183s);
    Check(rows.empty());
    cache.Observe(1, -53, L"Pixel 9", false, zero + 190s);
    rows = cache.Snapshot(zero + 190s);
    Check(rows.size() == 1 && rows[0].fresh && rows[0].observations == 1);

    // Service-UUID advertising phone still appears when Windows misses its name.
    cache.Observe(2, -38, L"", true, zero + 190s);
    rows = cache.Snapshot(zero + 190s);
    Check(rows.size() == 2 && rows[0].phoneService);
    Check(rows[0].name.find(L"名称未广播") != std::wstring::npos);
    cache.Observe(2, -39, L" Galaxy S25 ", false, zero + 191s);
    rows = cache.Snapshot(zero + 191s);
    Check(rows[0].name == L"Galaxy S25" && rows[0].phoneService);

    // Anonymous unrelated advertisements are never shown.
    cache.Observe(3, -45, L"", false, zero + 191s);
    Check(cache.Snapshot(zero + 191s).size() == 2);
    // A formerly forbidden near-field RSSI must be shown.
    cache.Observe(4, -11, L"Close headset", false, zero + 192s);
    Check(cache.Snapshot(zero + 192s).size() == 3);
    // Lost packets have no bearing on authorization freshness.
    rows = cache.Snapshot(zero + 220s);
    for (const auto& item : rows) Check(!item.fresh);
    // A new advertisement refreshes a stale row without losing its displayed name.
    cache.Observe(2, -40, L"", true, zero + 221s);
    rows = cache.Snapshot(zero + 221s);
    Check(rows[0].fresh && rows[0].name == L"Galaxy S25");
    // Control and direction formatting chars are removed from the UI.
    cache.Observe(9, -65, L"  Test\u202E Device  ", false, zero + 222s);
    rows = cache.Snapshot(zero + 222s);
    for (auto& row : rows) if (row.address == 9) Check(row.name == L"Test Device");
    // At least six completed 30s windows without a sighting precede eviction.
    rows = cache.Snapshot(zero + 221s + 179s);
    for (auto& row : rows) Check(row.address != 2 || !row.fresh);
    rows = cache.Snapshot(zero + 221s + 180s);
    for (auto& row : rows) Check(row.address != 2);
    cache.Clear();
    Check(cache.Snapshot(zero + 1h).empty());
    std::cout << "BLE presence retention checks passed\n";
}