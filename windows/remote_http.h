#pragma once
#include "remote_storage.h"
#include "remote_protocol.h"
#include <windows.h>
#include <functional>
#include <string>
namespace kb {
// HTTPS-only authenticated SSE transport. WinHTTP validates the server certificate.
// Returns false on cancellation, stream termination, or a transport/authentication error.
bool ReadRustDeskStream(const RemoteConfiguration& config,
                        const std::function<bool()>& stopping,
                        const std::function<void(const kenvix::remote::Event&)>& onEvent,
                        std::wstring& error);
bool TestRustDeskStream(const RemoteConfiguration& config,std::wstring& error);
} // namespace kb
