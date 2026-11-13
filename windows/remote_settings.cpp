#include "remote_http.h"
#include "remote_storage.h"
#include "storage.h"
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"comctl32.lib")
namespace {
constexpr COLORREF kBg=RGB(13,20,33),kCard=RGB(24,36,54),kInput=RGB(34,47,67);
constexpr COLORREF kText=RGB(235,243,255),kMuted=RGB(156,176,201),kBlue=RGB(89,155,245);
constexpr int cEnabled=201,cServer=202,cKey=203,cDevice=204,cPeer=205,cLock=206,cTest=207,cSave=208,cStatus=209,cTrusted=210;
constexpr UINT kTestFinished=WM_APP+50;
struct TestResult{bool ok{};std::wstring message;};
struct Ui{
    HWND wnd{},enabled{},server{},key{},device{},peer{},lock{},trusted{},test{},save{},status{};
    HFONT normal{},bold{},small{},heading{};
    HBRUSH cardBrush{},inputBrush{};
    kb::RemoteConfiguration saved{};
    bool existing{},isEnabled{},autoLock{true},trustedEventsOnly{},busy{};
    std::jthread testJob{};
};
std::wstring Read(HWND h){
    int count=GetWindowTextLengthW(h);if(count<=0)return {};
    std::wstring text(static_cast<size_t>(count)+1,L'\0');
    GetWindowTextW(h,text.data(),count+1);text.resize(wcslen(text.c_str()));return text;
}
HWND Control(HWND parent,const wchar_t* kind,const wchar_t* value,DWORD style,int id){
    return CreateWindowExW(0,kind,value,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
}
void Label(HDC h,int x,int y,const wchar_t* text,COLORREF color,HFONT font){
    SetBkMode(h,TRANSPARENT);SetTextColor(h,color);SelectObject(h,font);
    TextOutW(h,x,y,text,static_cast<int>(wcslen(text)));
}
void Rounded(HDC dc,RECT r,COLORREF color,int radius){
    HBRUSH brush=CreateSolidBrush(color);
    HPEN pen=CreatePen(PS_SOLID,1,color);
    auto prevBrush=SelectObject(dc,brush),prevPen=SelectObject(dc,pen);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,prevPen);SelectObject(dc,prevBrush);DeleteObject(pen);DeleteObject(brush);
}
void Font(HWND h,HFONT font){SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
void Move(HWND h,int x,int y,int width,int height){MoveWindow(h,x,y,width,height,TRUE);}
void UpdateButtons(Ui& ui){
    SetWindowTextW(ui.enabled,ui.isEnabled?L"●  已启用 RustDesk 自动解锁":L"○  启用 RustDesk 自动解锁");
    SetWindowTextW(ui.lock,ui.autoLock?L"●  连接全部结束时自动锁定":L"○  连接全部结束时自动锁定");
    SetWindowTextW(ui.trusted,ui.trustedEventsOnly?L"●  仅使用可信服务器事件":L"○  使用普通连接审计事件");
    for(HWND item:{ui.enabled,ui.lock,ui.trusted})InvalidateRect(item,nullptr,TRUE);
}
void Layout(Ui& ui){
    RECT r{};GetClientRect(ui.wnd,&r);
    int w=r.right,usable=w-110;
    Move(ui.enabled,54,145,usable,48);
    Move(ui.server,54,269,usable,42);
    Move(ui.key,54,353,usable,42);
    Move(ui.device,54,437,(usable-20)/2,42);
    Move(ui.peer,54+(usable-20)/2+20,437,(usable-20)/2,42);
    Move(ui.lock,54,511,usable,42);
    Move(ui.trusted,54,566,usable,42);
    Move(ui.test,54,632,190,48);
    Move(ui.save,w-242,632,188,48);
    Move(ui.status,260,632,w-520,60);
}
void Paint(Ui& ui){
    PAINTSTRUCT ps{};auto dc=BeginPaint(ui.wnd,&ps);
    RECT r{};GetClientRect(ui.wnd,&r);
    HBRUSH background=CreateSolidBrush(kBg);FillRect(dc,&r,background);DeleteObject(background);
    Rounded(dc,{26,125,r.right-26,r.bottom-26},kCard,22);
    Label(dc,54,31,L"Kenvix",kBlue,ui.heading);
    Label(dc,182,37,L"RustDesk API",kText,ui.heading);
    Label(dc,54,91,L"RustDesk 连接事件可触发 Windows 自动解锁和连接结束后的锁定。",kMuted,ui.normal);
    Label(dc,54,202,L"普通审计事件由 RustDesk 客户端上报，建议限制可信服务器的访问。",kMuted,ui.small);
    Label(dc,54,231,L"服务器地址（HTTPS）",kMuted,ui.small);
    Label(dc,54,319,L"SSE API Key（已保存时留空即可）",kMuted,ui.small);
    Label(dc,54,405,L"本设备 RustDesk ID",kMuted,ui.small);
    int usable=r.right-110;
    Label(dc,54+(usable-20)/2+20,405,L"允许的连接方设备 ID（可选）",kMuted,ui.small);
    Label(dc,54,r.bottom-54,L"API Key 可从账户 → 消息推送 → SSE 实时订阅中生成。",kMuted,ui.small);
    EndPaint(ui.wnd,&ps);
}
void Draw(DRAWITEMSTRUCT* d,Ui& ui){
    const bool primary=d->CtlID==cSave;
    const bool toggle=d->CtlID==cEnabled||d->CtlID==cLock||d->CtlID==cTrusted;
    const bool disabled=(d->itemState&ODS_DISABLED)!=0;
    COLORREF bg=disabled?kInput:primary?kBlue:toggle?kInput:RGB(47,64,86);
    Rounded(d->hDC,d->rcItem,bg,16);
    SetTextColor(d->hDC,primary?RGB(9,20,32):kText);
    SetBkMode(d->hDC,TRANSPARENT);SelectObject(d->hDC,ui.normal);
    wchar_t title[180]{};GetWindowTextW(d->hwndItem,title,180);
    RECT rect=d->rcItem;
    if(toggle)rect.left+=18;
    DrawTextW(d->hDC,title,-1,&rect,DT_SINGLELINE|DT_VCENTER|(toggle?DT_LEFT:DT_CENTER));
    if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);
}
bool Current(Ui& ui,kb::RemoteConfiguration& out,std::wstring& error){
    out.enabled=ui.isEnabled;out.lockOnLastDisconnect=ui.autoLock;out.trustedEventsOnly=ui.trustedEventsOnly;
    out.serverUrl=Read(ui.server);out.deviceId=Read(ui.device);out.allowedPeer=Read(ui.peer);
    std::wstring key=Read(ui.key);
    if(!key.empty()){
        if(!kb::EncryptRemoteApiKey(key,out.apiKeyEncrypted)){
            kb::Wipe(key);error=L"API Key 无效，请使用以 rdapi_ 开头的 SSE 密钥。";return false;
        }
    }else if(ui.existing)out.apiKeyEncrypted=ui.saved.apiKeyEncrypted;
    kb::Wipe(key);
    if(out.enabled){
        if(out.serverUrl.rfind(L"https://",0)!=0){error=L"服务器必须使用 HTTPS 地址。";return false;}
        if(out.deviceId.empty()){error=L"请填写这台电脑的 RustDesk 设备 ID。";return false;}
        if(out.apiKeyEncrypted.empty()){error=L"请填写 SSE API Key。";return false;}
        kb::Configuration local;
        if(!kb::ReadConfig(local)){
            error=L"请先在主设置窗口配置并保存 Windows 登录账户。";return false;
        }
    }
    return true;
}
void Test(Ui& ui){
    if(ui.busy)return;
    kb::RemoteConfiguration cfg;std::wstring error;
    if(!Current(ui,cfg,error)){SetWindowTextW(ui.status,error.c_str());return;}
    if(!cfg.enabled){SetWindowTextW(ui.status,L"请先启用 RustDesk 解锁。 ");return;}
    ui.busy=true;EnableWindow(ui.test,FALSE);SetWindowTextW(ui.status,L"正在验证 SSE 连接…");
    if(ui.testJob.joinable())ui.testJob.join();
    ui.testJob=std::jthread([cfg,host=ui.wnd](std::stop_token){
        auto output=std::make_unique<TestResult>();
        output->ok=kb::TestRustDeskStream(cfg,output->message);
        if(!PostMessageW(host,kTestFinished,0,reinterpret_cast<LPARAM>(output.get())))return;
        output.release();
    });
}
void Save(Ui& ui){
    if(ui.busy)return;
    kb::RemoteConfiguration next;std::wstring error;
    if(!Current(ui,next,error)){SetWindowTextW(ui.status,error.c_str());return;}
    if(!kb::SaveRemoteConfiguration(next)){
        SetWindowTextW(ui.status,L"保存失败。请先保存 Windows 登录账户并以管理员权限运行。 ");return;
    }
    ui.saved=std::move(next);ui.existing=true;
    SetWindowTextW(ui.key,L"");
    SetWindowTextW(ui.status,L"设置已保存，后台服务将自动更新订阅。 ");
}
void Create(Ui& ui){
    ui.normal=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    ui.bold=CreateFontW(-17,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    ui.small=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    ui.heading=CreateFontW(-29,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    ui.cardBrush=CreateSolidBrush(kCard);ui.inputBrush=CreateSolidBrush(kInput);
    ui.enabled=Control(ui.wnd,L"BUTTON",L"",BS_OWNERDRAW,cEnabled);
    ui.server=Control(ui.wnd,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,cServer);
    ui.key=Control(ui.wnd,L"EDIT",L"",ES_PASSWORD|ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,cKey);
    ui.device=Control(ui.wnd,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,cDevice);
    ui.peer=Control(ui.wnd,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,cPeer);
    ui.lock=Control(ui.wnd,L"BUTTON",L"",BS_OWNERDRAW,cLock);
    ui.trusted=Control(ui.wnd,L"BUTTON",L"",BS_OWNERDRAW,cTrusted);
    ui.test=Control(ui.wnd,L"BUTTON",L"测试 SSE 连接",BS_OWNERDRAW,cTest);
    ui.save=Control(ui.wnd,L"BUTTON",L"保存配置",BS_OWNERDRAW,cSave);
    ui.status=Control(ui.wnd,L"STATIC",L"",SS_LEFT,cStatus);
    for(HWND child:{ui.enabled,ui.server,ui.key,ui.device,ui.peer,ui.lock,ui.trusted,ui.test,ui.save,ui.status})
        Font(child,ui.normal);
    if(ui.existing){
        ui.isEnabled=ui.saved.enabled;ui.autoLock=ui.saved.lockOnLastDisconnect;ui.trustedEventsOnly=ui.saved.trustedEventsOnly;
        SetWindowTextW(ui.server,ui.saved.serverUrl.c_str());
        SetWindowTextW(ui.device,ui.saved.deviceId.c_str());
        SetWindowTextW(ui.peer,ui.saved.allowedPeer.c_str());
        SetWindowTextW(ui.status,L"已读取保存的配置；API Key 不会在界面中显示。 ");
    }else{
        SetWindowTextW(ui.server,L"https://desk.example.com");
        SetWindowTextW(ui.status,L"请填写服务器地址、API Key 和目标设备 ID。 ");
    }
    UpdateButtons(ui);Layout(ui);
}
LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    auto* ui=reinterpret_cast<Ui*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    switch(msg){
      case WM_CREATE:{
        auto state=std::make_unique<Ui>();state->wnd=hwnd;
        state->existing=kb::ReadRemoteConfiguration(state->saved);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state.get()));
        Create(*state);state.release();return 0;
      }
      case WM_GETMINMAXINFO:{
        auto* info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={760,800};return 0;
      }
      case WM_SIZE:if(ui){Layout(*ui);InvalidateRect(hwnd,nullptr,TRUE);}return 0;
      case WM_COMMAND:if(ui){
        const auto id=LOWORD(w);
        if(id==cEnabled){ui->isEnabled=!ui->isEnabled;UpdateButtons(*ui);return 0;}
        if(id==cLock){ui->autoLock=!ui->autoLock;UpdateButtons(*ui);return 0;}
        if(id==cTrusted){ui->trustedEventsOnly=!ui->trustedEventsOnly;UpdateButtons(*ui);return 0;}
        if(id==cTest){Test(*ui);return 0;}
        if(id==cSave){Save(*ui);return 0;}
      }break;
      case kTestFinished:if(ui){
        std::unique_ptr<TestResult> result(reinterpret_cast<TestResult*>(l));
        ui->busy=false;EnableWindow(ui->test,TRUE);
        SetWindowTextW(ui->status,result->message.c_str());return 0;
      }
      case WM_DRAWITEM:if(ui){Draw(reinterpret_cast<DRAWITEMSTRUCT*>(l),*ui);return TRUE;}
      case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:if(ui){
        auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,kText);SetBkColor(dc,kCard);
        return reinterpret_cast<LRESULT>(ui->cardBrush);
      }break;
      case WM_CTLCOLOREDIT:if(ui){
        auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,kText);SetBkColor(dc,kInput);
        return reinterpret_cast<LRESULT>(ui->inputBrush);
      }break;
      case WM_PAINT:if(ui){Paint(*ui);return 0;}
      case WM_DESTROY:if(ui){
        if(ui->testJob.joinable())ui->testJob.join();
        MSG message{};
        while(PeekMessageW(&message,hwnd,kTestFinished,kTestFinished,PM_REMOVE))
            delete reinterpret_cast<TestResult*>(message.lParam);
        for(auto font:{ui->normal,ui->bold,ui->small,ui->heading})DeleteObject(font);
        DeleteObject(ui->cardBrush);DeleteObject(ui->inputBrush);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);delete ui;
      }PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
} // namespace
int APIENTRY wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_STANDARD_CLASSES};InitCommonControlsEx(&init);
    WNDCLASSEXW klass{sizeof(klass)};klass.hInstance=instance;klass.lpfnWndProc=WndProc;
    klass.lpszClassName=L"KenvixRustDeskUnlockSettings";
    klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    klass.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if(!RegisterClassExW(&klass))return 1;
    auto window=CreateWindowExW(0,klass.lpszClassName,L"Kenvix · RustDesk API 解锁设置",
         WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,860,810,
         nullptr,nullptr,instance,nullptr);
    if(!window)return 1;
    BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    ShowWindow(window,show);UpdateWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    return static_cast<int>(message.wParam);
}
