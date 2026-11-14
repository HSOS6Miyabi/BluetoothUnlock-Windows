#include "remote_http.h"
#include "storage.h"
#include <winhttp.h>
#include <algorithm>
#include <memory>
#include <vector>
#pragma comment(lib,"winhttp.lib")
namespace kb {
namespace {
struct HttpHandle {
    HINTERNET handle{};
    explicit HttpHandle(HINTERNET value=nullptr):handle(value){}
    ~HttpHandle(){if(handle)WinHttpCloseHandle(handle);}
    HttpHandle(const HttpHandle&)=delete;
    HttpHandle& operator=(const HttpHandle&)=delete;
    operator HINTERNET() const {return handle;}
};
struct Endpoint {std::wstring host, path; INTERNET_PORT port{};};
bool ParseEndpoint(const std::wstring& base,Endpoint& out){
    if(base.size()<9||base.size()>1024||base.rfind(L"https://",0)!=0||
       base.find_first_of(L"\r\n\t?#")!=std::wstring::npos)return false;
    URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
    parts.dwHostNameLength=static_cast<DWORD>(-1);
    parts.dwUrlPathLength=static_cast<DWORD>(-1);
    parts.dwUserNameLength=static_cast<DWORD>(-1);
    parts.dwPasswordLength=static_cast<DWORD>(-1);
    parts.dwExtraInfoLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(base.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS||
       parts.dwHostNameLength==0||parts.dwUserNameLength||parts.dwPasswordLength||parts.dwExtraInfoLength)return false;
    out.host.assign(parts.lpszHostName,parts.dwHostNameLength);
    out.path.assign(parts.lpszUrlPath?parts.lpszUrlPath:L"",parts.dwUrlPathLength);
    // A reverse proxy may mount the API under a fixed path prefix.
    if(out.path.find(L"..")!=std::wstring::npos||out.path.find(L'%')!=std::wstring::npos)return false;
    while(!out.path.empty()&&out.path.back()==L'/')out.path.pop_back();
    out.path+=L"/api/events/v1/stream?events=connection.authenticated,connection.ended,connection.trusted_authenticated,connection.trusted_ended";
    out.port=parts.nPort;
    return true;
}
bool Open(const RemoteConfiguration& config,Endpoint& endpoint,
          std::unique_ptr<HttpHandle>& session,std::unique_ptr<HttpHandle>& connection,
          std::unique_ptr<HttpHandle>& request,std::wstring& error){
    if(!ParseEndpoint(config.serverUrl,endpoint)){
        error=L"请填写有效的 HTTPS 服务器地址，不要包含查询参数。";return false;
    }
    std::wstring key;
    if(!DecryptRemoteApiKey(config.apiKeyEncrypted,key)){
        error=L"无法读取加密 API Key。";return false;
    }
    for(auto c:key)if(c<33||c>126){Wipe(key);error=L"API Key 格式无效。";return false;}
    session=std::make_unique<HttpHandle>(WinHttpOpen(L"BluetoothUnlock/1.2",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!*session){Wipe(key);error=L"无法初始化 WinHTTP。";return false;}
    WinHttpSetTimeouts(*session,8000,8000,8000,35000);
    connection=std::make_unique<HttpHandle>(WinHttpConnect(*session,endpoint.host.c_str(),endpoint.port,0));
    if(!*connection){Wipe(key);error=L"无法连接到 RustDesk API 服务器。";return false;}
    request=std::make_unique<HttpHandle>(WinHttpOpenRequest(*connection,L"GET",endpoint.path.c_str(),
        nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    if(!*request){Wipe(key);error=L"无法建立 SSE 请求。";return false;}
    DWORD disabled=WINHTTP_DISABLE_REDIRECTS;
    if(!WinHttpSetOption(*request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled))){
        Wipe(key);error=L"无法禁用 HTTP 重定向，连接已取消。";return false;
    }
    DWORD revocation=WINHTTP_ENABLE_SSL_REVOCATION;
    WinHttpSetOption(*request,WINHTTP_OPTION_ENABLE_FEATURE,&revocation,sizeof(revocation));
    std::wstring header=L"Authorization: Bearer "+key+L"\r\nAccept: text/event-stream\r\nCache-Control: no-cache\r\n";
    const bool sent=WinHttpSendRequest(*request,header.c_str(),static_cast<DWORD>(header.size()),
                                     WINHTTP_NO_REQUEST_DATA,0,0,0)!=FALSE;
    Wipe(header);Wipe(key);
    if(!sent||!WinHttpReceiveResponse(*request,nullptr)){
        error=L"SSE 请求失败，请检查网络连接和 TLS 证书。";return false;
    }
    DWORD status=0,length=sizeof(status);
    if(!WinHttpQueryHeaders(*request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX)){
        error=L"无法读取服务器响应状态。";return false;
    }
    if(status!=200){error=L"SSE 服务器返回 HTTP "+std::to_wstring(status)+L"。";return false;}
    wchar_t contentType[256]{};length=sizeof(contentType);
    if(!WinHttpQueryHeaders(*request,WINHTTP_QUERY_CONTENT_TYPE,WINHTTP_HEADER_NAME_BY_INDEX,
                            contentType,&length,WINHTTP_NO_HEADER_INDEX)||
       std::wstring(contentType).rfind(L"text/event-stream",0)!=0){
        error=L"服务器没有返回 SSE 数据流。";return false;
    }
    return true;
}
} // namespace
bool TestRustDeskStream(const RemoteConfiguration& config,std::wstring& error){
    Endpoint endpoint;std::unique_ptr<HttpHandle> session,connection,request;
    if(!Open(config,endpoint,session,connection,request,error))return false;
    error=L"SSE 认证成功，服务器已接受订阅。";
    return true;
}
bool ReadRustDeskStream(const RemoteConfiguration& config,
                        const std::function<bool()>& stopping,
                        const std::function<void(const kenvix::remote::Event&)>& onEvent,
                        std::wstring& error){
    Endpoint endpoint;std::unique_ptr<HttpHandle> session,connection,request;
    if(!Open(config,endpoint,session,connection,request,error))return false;
    kenvix::remote::SseParser parser;
    char bytes[4096];
    while(!stopping()){
        DWORD count=0;
        if(!WinHttpReadData(*request,bytes,sizeof(bytes),&count)){
            error=L"SSE 连接中断或心跳超时。";return false;
        }
        if(count==0){error=L"SSE 连接已经结束。";return false;}
        for(const auto& event:parser.Feed(std::string_view(bytes,count))){
            if(stopping())return false;
            onEvent(event);
        }
    }
    return false;
}
} // namespace kb
