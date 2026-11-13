#include "remote_protocol.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <iostream>
using namespace kenvix::remote;
using namespace std::chrono;
namespace {
std::string Make(const char* type,const char* id,const char* peer="remote-2",const char* device="local-1",int conn=40) {
    return std::string("event: ")+type+"\r\ndata: {\"id\":\""+id+"\",\"type\":\""+type+
      "\",\"occurred_at\":\"2026-11-13T10:00:00Z\",\"device_id\":\""+device+
      "\",\"from_peer\":\""+peer+"\",\"connection_id\":"+std::to_string(conn)+"}\r\n\r\n";
}
}
int main(){
    auto now=ParseUtcTimestamp("2026-11-13T10:00:00Z");assert(now);
    assert(ParseUtcTimestamp("2026-11-13T10:00:00.123456789Z"));
    assert(!ParseUtcTimestamp("2026-02-30T10:00:00Z"));
    assert(!ParseUtcTimestamp("2026-11-13T10:00:00+02:00"));
    SseParser parser;
    std::vector<Event> all;
    std::string bytes=": heartbeat\n\n"+Make("connection.authenticated","one")+Make("connection.authenticated","one")+
      Make("connection.ended","close","", "local-1",40);
    for(char ch:bytes){auto list=parser.Feed(std::string_view(&ch,1));all.insert(all.end(),list.begin(),list.end());}
    assert(all.size()==3);
    ConnectionTracker tracker("local-1","remote-2",true);
    assert(tracker.Apply(all[0],*now)==Action::unlock);
    assert(tracker.ActiveConnections()==1);
    assert(tracker.Apply(all[1],*now)==Action::none);
    assert(tracker.Apply(all[2],*now)==Action::lock);
    assert(tracker.ActiveConnections()==0);
    assert(tracker.Apply(all[0],*now)==Action::none);
    auto newer=all[0];newer.id="new";newer.connectionId=42;
    assert(tracker.Apply(newer,*now)==Action::unlock);
    newer.id="new-2";newer.connectionId=43;
    assert(tracker.Apply(newer,*now)==Action::unlock);
    auto end=newer;end.id="end-43";end.type="connection.ended";
    assert(tracker.Apply(end,*now)==Action::none);
    end.connectionId=42;end.id="end-42";
    assert(tracker.Apply(end,*now)==Action::lock);
    newer.id="remote-other";newer.fromPeer="unknown";newer.connectionId=55;
    assert(tracker.Apply(newer,*now)==Action::none);
    newer.fromPeer="remote-2";newer.deviceId="not-this-device";
    assert(tracker.Apply(newer,*now)==Action::none);
    newer.deviceId="local-1";newer.occurredAt=*now-seconds{100};
    assert(tracker.Apply(newer,*now)==Action::none);
    newer.occurredAt=*now;newer.id="reconnected";
    assert(tracker.Apply(newer,*now)==Action::unlock);
    assert(tracker.ConnectionLost()==Action::lock);
    assert(tracker.ConnectionLost()==Action::none);
    assert(!ParseEvent("{\"type\":\"connection.authenticated\",\"type\":\"connection.ended\"}","connection.authenticated"));
    assert(!ParseEvent("{\"type\":\"connection.authenticated\",\"connection_id\":0}","connection.authenticated"));
    assert(!ParseEvent("{\"type\":\"connection.ended\",\"connection_id\":1}","connection.authenticated"));
    parser.Reset();
    auto invalid=parser.Feed("event: connection.authenticated\ndata: {broken}\n\n");
    assert(invalid.empty());
    parser.Reset();
    auto good=parser.Feed(Make("connection.authenticated","post-error"));
    assert(good.size()==1);
    auto trustedAuth=parser.Feed(Make("connection.trusted_authenticated","secure-start"));
    auto trustedEnd=parser.Feed(Make("connection.trusted_ended","secure-end","","local-1",40));
    assert(trustedAuth.size()==1 && trustedEnd.size()==1);
    ConnectionTracker safe("local-1","remote-2",true,true);
    assert(safe.Apply(all[0],*now)==Action::none);
    assert(safe.Apply(trustedAuth[0],*now)==Action::unlock);
    assert(safe.Apply(all[2],*now)==Action::none);
    assert(safe.Apply(trustedEnd[0],*now)==Action::lock);
    assert(tracker.Apply(trustedAuth[0],*now)==Action::none);
    std::cout<<"RustDesk SSE, JSON and session tracking tests passed\n";
}
