#include "remote_protocol.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>

namespace kenvix::remote {
namespace {
constexpr std::size_t kMaxLine = 32768;
constexpr std::size_t kMaxData = 65536;
void AppendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) out.push_back(static_cast<char>(code));
    else if (code < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 63)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
        out.push_back(static_cast<char>(0x80 | (code & 63)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 63)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
        out.push_back(static_cast<char>(0x80 | (code & 63)));
    }
}
int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
class JsonReader {
public:
    explicit JsonReader(std::string_view source) : s_(source) {}
    bool Read(std::unordered_map<std::string, std::string>& values) {
        if (!Take('{')) return false;
        if (Take('}')) return Done();
        for (unsigned count = 0; count < 32; ++count) {
            std::string key, value;
            if (!String(key) || !Take(':') || !Value(value)) return false;
            if (!values.emplace(key, value).second) return false;
            if (Take('}')) return Done();
            if (!Take(',')) return false;
        }
        return false;
    }
private:
    bool Done() { Space(); return at_ == s_.size(); }
    void Space() { while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t' || s_[at_] == '\r' || s_[at_] == '\n')) ++at_; }
    bool Take(char c) { Space(); if (at_ < s_.size() && s_[at_] == c) { ++at_; return true; } return false; }
    bool Unicode(unsigned& out) {
        if (at_ + 4 > s_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const int v = Hex(s_[at_++]);
            if (v < 0) return false;
            out = (out << 4) | static_cast<unsigned>(v);
        }
        return true;
    }
    bool String(std::string& out) {
        if (!Take('"')) return false;
        while (at_ < s_.size() && out.size() < kMaxData) {
            const unsigned char c = static_cast<unsigned char>(s_[at_++]);
            if (c == '"') return true;
            if (c < 32) return false;
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            if (at_ == s_.size()) return false;
            switch (s_[at_++]) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (!Unicode(cp)) return false;
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (at_ + 2 > s_.size() || s_[at_++] != '\\' || s_[at_++] != 'u') return false;
                        unsigned second = 0;
                        if (!Unicode(second) || second < 0xdc00 || second > 0xdfff) return false;
                        cp = 0x10000 + ((cp - 0xd800) << 10) + second - 0xdc00;
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
                    AppendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool Value(std::string& out) {
        Space();
        if (at_ >= s_.size()) return false;
        if (s_[at_] == '"') return String(out);
        const auto begin = at_;
        while (at_ < s_.size() && s_[at_] != ',' && s_[at_] != '}' &&
               s_[at_] != ' ' && s_[at_] != '\r' && s_[at_] != '\n' && s_[at_] != '\t') ++at_;
        if (begin == at_ || at_ - begin > 32) return false;
        out = std::string{s_.substr(begin, at_ - begin)};
        return true;
    }
    std::string_view s_;
    std::size_t at_{};
};
std::string SessionKey(std::int64_t id, std::string_view peer) {
    return std::to_string(id) + ":" + std::string(peer);
}
int Digit(std::string_view s, std::size_t pos, unsigned count) {
    if (pos + count > s.size()) return -1;
    int value = 0;
    for (unsigned n = 0; n < count; ++n) {
        if (s[pos+n] < '0' || s[pos+n] > '9') return -1;
        value = value * 10 + s[pos+n] - '0';
    }
    return value;
}
} // namespace

std::optional<std::chrono::system_clock::time_point> ParseUtcTimestamp(std::string_view input) {
    using namespace std::chrono;
    if (input.size() < 20 || input[4] != '-' || input[7] != '-' ||
        input[10] != 'T' || input[13] != ':' || input[16] != ':') return std::nullopt;
    auto yearNum = Digit(input,0,4),monthNum=Digit(input,5,2),dayNum=Digit(input,8,2);
    auto hour=Digit(input,11,2),minute=Digit(input,14,2),second=Digit(input,17,2);
    if (yearNum < 2000 || yearNum > 9999 || hour > 23 || minute > 59 || second > 59 ||
        hour < 0 || minute < 0 || second < 0) return std::nullopt;
    year_month_day date{year{yearNum}, month{static_cast<unsigned>(monthNum)}, day{static_cast<unsigned>(dayNum)}};
    if (!date.ok()) return std::nullopt;
    std::size_t at = 19;
    nanoseconds fraction{};
    if (at < input.size() && input[at] == '.') {
        ++at;
        unsigned digits = 0;
        std::int64_t value = 0;
        while (at < input.size() && std::isdigit(static_cast<unsigned char>(input[at])) && digits < 9) {
            value = value * 10 + input[at++] - '0';
            ++digits;
        }
        if (digits == 0 || (at < input.size() && std::isdigit(static_cast<unsigned char>(input[at])))) return std::nullopt;
        while (digits++ < 9) value *= 10;
        fraction = nanoseconds{value};
    }
    if (at + 1 != input.size() || input[at] != 'Z') return std::nullopt;
    return time_point_cast<system_clock::duration>(sys_days{date} + hours{hour} + minutes{minute} + seconds{second} + fraction);
}

std::optional<Event> ParseEvent(std::string_view json, std::string_view name) {
    if (json.empty() || json.size() > kMaxData ||
        (name != "connection.authenticated" && name != "connection.ended" &&
         name != "connection.trusted_authenticated" && name != "connection.trusted_ended")) return std::nullopt;
    std::unordered_map<std::string,std::string> fields;
    if (!JsonReader(json).Read(fields)) return std::nullopt;
    Event event;
    event.type = fields["type"];
    event.id = fields["id"];
    event.deviceId = fields["device_id"];
    event.fromPeer = fields["from_peer"];
    const std::string& conn = fields["connection_id"];
    auto [end, error] = std::from_chars(conn.data(), conn.data() + conn.size(), event.connectionId);
    auto timestamp=ParseUtcTimestamp(fields["occurred_at"]);
    if (event.type != name || event.id.empty() || event.id.size() > 256 ||
        event.deviceId.empty() || event.deviceId.size() > 253 ||
        event.fromPeer.size() > 253 || error != std::errc() || end != conn.data()+conn.size() ||
        event.connectionId <= 0 || !timestamp) return std::nullopt;
    if ((event.type == "connection.authenticated" || event.type == "connection.trusted_authenticated") && event.fromPeer.empty()) return std::nullopt;
    event.occurredAt = *timestamp;
    return event;
}

void SseParser::Reset() { line_.clear(); eventName_.clear(); data_.clear(); discard_=false; }
void SseParser::ProcessLine(std::vector<Event>& result) {
    if (line_.empty()) {
        if (!discard_ && !data_.empty()) {
            if (!data_.empty() && data_.back() == '\n') data_.pop_back();
            auto item=ParseEvent(data_,eventName_);
            if (item) result.push_back(std::move(*item));
        }
        data_.clear(); eventName_.clear(); discard_=false;return;
    }
    if (discard_ || line_[0] == ':') return;
    const auto pos = line_.find(':');
    std::string_view name(line_.data(),pos==std::string::npos?line_.size():pos);
    std::string_view value = pos==std::string::npos?std::string_view{}:
        std::string_view(line_).substr(pos+1);
    if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    if (name == "event") eventName_ = std::string(value);
    else if (name == "data") {
        if (data_.size()+value.size()+1 > kMaxData) { discard_=true;return; }
        data_.append(value);data_.push_back('\n');
    }
}
std::vector<Event> SseParser::Feed(std::string_view input) {
    std::vector<Event> output;
    for (char c:input) {
        if (c == '\n') {
            if (!line_.empty() && line_.back() == '\r') line_.pop_back();
            ProcessLine(output);line_.clear();
        } else if (line_.size() < kMaxLine) line_.push_back(c);
        else discard_=true;
    }
    return output;
}

ConnectionTracker::ConnectionTracker(std::string target, std::string peer, bool autoLock, bool trustedOnly)
    :deviceId_(std::move(target)),allowedPeer_(std::move(peer)),autoLock_(autoLock),trustedOnly_(trustedOnly){}
Action ConnectionTracker::Apply(const Event& event, std::chrono::system_clock::time_point now) {
    using namespace std::chrono;
    const bool verified = event.type == "connection.trusted_authenticated" || event.type == "connection.trusted_ended";
    const bool start = event.type == "connection.authenticated" || event.type == "connection.trusted_authenticated";
    const bool end = event.type == "connection.ended" || event.type == "connection.trusted_ended";
    if (verified != trustedOnly_ || event.deviceId != deviceId_ || event.connectionId <= 0 || event.id.empty() ||
        event.occurredAt < now - seconds{90} || event.occurredAt > now + seconds{15} ||
        (!start && !end) ||
        seen_.contains(event.id)) return Action::none;
    seen_.insert(event.id);seenOrder_.push_back(event.id);
    if (seenOrder_.size() > 512) { seen_.erase(seenOrder_.front());seenOrder_.pop_front(); }
    const auto conn = std::to_string(event.connectionId);
    if (start) {
        if (event.fromPeer.empty() || (!allowedPeer_.empty() && event.fromPeer != allowedPeer_)) return Action::none;
        const auto key = SessionKey(event.connectionId,event.fromPeer);
        for (const auto& tomb: {conn, key}) {
            auto it = ended_.find(tomb);
            if (it != ended_.end() && it->second >= event.occurredAt) return Action::none;
        }
        if (active_.contains(key)) return Action::none;
        if (active_.size() >= 128) return Action::none;
        active_.emplace(key,Session{event.connectionId,event.fromPeer});
        return Action::unlock;
    }
    // The closing audit may omit from_peer; the corresponding connection ID remains authoritative.
    if (!event.fromPeer.empty()) ended_[SessionKey(event.connectionId,event.fromPeer)] = event.occurredAt;
    ended_[conn] = event.occurredAt;
    if (ended_.size() > 256) ended_.clear();
    bool removed = false;
    for (auto it=active_.begin(); it!=active_.end();) {
        if (it->second.id == event.connectionId &&
            (event.fromPeer.empty() || it->second.peer == event.fromPeer)) {
            it=active_.erase(it); removed=true;
        } else ++it;
    }
    if (removed && active_.empty() && autoLock_) return Action::lock;
    return Action::none;
}
Action ConnectionTracker::ConnectionLost() {
    const bool wasActive = !active_.empty();
    active_.clear();ended_.clear();seen_.clear();seenOrder_.clear();
    return (wasActive && autoLock_) ? Action::lock : Action::none;
}
} // namespace kenvix::remote
