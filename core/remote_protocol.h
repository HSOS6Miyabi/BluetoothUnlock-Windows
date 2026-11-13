#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kenvix::remote {
struct Event {
    std::string id;
    std::string type;
    std::string deviceId;
    std::string fromPeer;
    std::int64_t connectionId{};
    std::chrono::system_clock::time_point occurredAt{};
};

// A bounded incremental parser for the RustDesk API text/event-stream response.
class SseParser {
public:
    std::vector<Event> Feed(std::string_view input);
    void Reset();
private:
    void ProcessLine(std::vector<Event>& result);
    std::string line_, eventName_, data_;
    bool discard_{false};
};

// Connections are scoped to a single configured target device.
enum class Action { none, unlock, lock };
class ConnectionTracker {
public:
    ConnectionTracker(std::string deviceId, std::string fromPeer, bool autoLock, bool trustedOnly = false);
    Action Apply(const Event& event, std::chrono::system_clock::time_point now);
    Action ConnectionLost();
    std::size_t ActiveConnections() const noexcept { return active_.size(); }
private:
    struct Session { std::int64_t id; std::string peer; };
    std::string deviceId_, allowedPeer_;
    bool autoLock_, trustedOnly_;
    std::unordered_map<std::string, Session> active_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> ended_;
    std::unordered_set<std::string> seen_;
    std::deque<std::string> seenOrder_;
};

std::optional<Event> ParseEvent(std::string_view json, std::string_view sseEventName);
std::optional<std::chrono::system_clock::time_point> ParseUtcTimestamp(std::string_view value);
} // namespace kenvix::remote
