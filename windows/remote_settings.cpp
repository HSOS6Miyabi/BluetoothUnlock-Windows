#include "remote_http.h"
#include "remote_storage.h"
#include "remote_settings.h"
#include "service_manager.h"
#include "ui_common.h"
#include "storage.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"comctl32.lib")
namespace {
COLORREF kBg=RGB(247,249,252),kCard=RGB(255,255,255),kInput=RGB(243,247,252);
COLORREF kText=RGB(30,42,60),kMuted=RGB(92,108,130),kBlue=RGB(39,105,213);
COLORREF kAccentText=RGB(255,255,255),kBorder=RGB(215,224,237),kSubtle=RGB(233,241,253);
constexpr int cEnabled=201,cServer=202,cKey=203,cDevice=204,cPeer=205,cLock=206,cTest=207,cSave=208,cStatus=209,cTrusted=210,cInstall=211;
constexpr UINT kTestFinished=WM_APP+50;
struct TestResult{bool ok{};std::wstring message;};
struct Ui{
    HWND wnd{},enabled{},server{},key{},device{},peer{},lock{},trusted{},test{},save{},status{},install{};
    HFONT normal{},bold{},smallFont{},heading{};
    HBRUSH cardBrush{},inputBrush{};
    kb::ui::Palette palette{};
    UINT dpi{96};
    kb::RemoteConfiguration saved{};
    bool existing{},isEnabled{},autoLock{true},trustedEventsOnly{},busy{};
    std::jthread testJob{};
};
int S(const Ui& ui,int n){return kb::ui::Scale(ui.dpi,n);}
void Appearance(Ui& ui){
    ui.palette=kb::ui::Colors();
    kBg=ui.palette.background;kCard=ui.palette.panel;kInput=ui.palette.field;
    kText=ui.palette.text;kMuted=ui.palette.muted;kBlue=ui.palette.accent;
    kAccentText=ui.palette.accentText;kBorder=ui.palette.border;kSubtle=ui.palette.subtle;
    auto normal=kb::ui::MakeFont(ui.dpi,18);
    auto bold=kb::ui::MakeFont(ui.dpi,18,FW_SEMIBOLD);
    auto smallText=kb::ui::MakeFont(ui.dpi,16);
    auto heading=kb::ui::MakeFont(ui.dpi,25,FW_SEMIBOLD);
    if(ui.normal){for(HWND item:{ui.enabled,ui.server,ui.key,ui.device,ui.peer,ui.lock,ui.trusted,ui.test,ui.save,ui.status,ui.install})
        if(item)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(normal),FALSE);}
    for(auto f:{ui.normal,ui.bold,ui.smallFont,ui.heading})if(f)DeleteObject(f);
    ui.normal=normal;ui.bold=bold;ui.smallFont=smallText;ui.heading=heading;
    if(ui.cardBrush)DeleteObject(ui.cardBrush);
    if(ui.inputBrush)DeleteObject(ui.inputBrush);
    ui.cardBrush=CreateSolidBrush(kCard);ui.inputBrush=CreateSolidBrush(kInput);
    if(ui.wnd){InvalidateRect(ui.wnd,nullptr,TRUE);}
}
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
    const int w=MulDiv(r.right,96,ui.dpi),h=MulDiv(r.bottom,96,ui.dpi);
    const int left=46, usable=std::max(390,w-92);
    auto place=[&](HWND child,int x,int y,int width,int height){
        Move(child,S(ui,x),S(ui,y),S(ui,width),S(ui,height));};
    place(ui.install,std::max(45,w-300),21,247,43);
    place(ui.enabled,left,94,usable,43);
    place(ui.server,left,199,usable,44);
    place(ui.key,left,279,usable,44);
    place(ui.device,left,359,(usable-20)/2,44);
    place(ui.peer,left+(usable-20)/2+20,359,(usable-20)/2,44);
    place(ui.lock,left,421,usable,36);
    place(ui.trusted,left,463,usable,36);
    place(ui.test,left,std::max(510,h-76),182,46);
    place(ui.save,w-228,std::max(510,h-76),182,46);
    place(ui.status,left+202,std::max(511,h-75),std::max(160,usable-408),46);
}
void Paint(Ui& ui){
    PAINTSTRUCT ps{};auto dc=BeginPaint(ui.wnd,&ps);
    RECT r{};GetClientRect(ui.wnd,&r);
    HBRUSH bg=CreateSolidBrush(kBg);FillRect(dc,&r,bg);DeleteObject(bg);
    int w=MulDiv(r.right,96,ui.dpi),h=MulDiv(r.bottom,96,ui.dpi);
    auto rect=[&](int x1,int y1,int x2,int y2){return RECT{S(ui,x1),S(ui,y1),S(ui,x2),S(ui,y2)};};
    kb::ui::Rounded(dc,rect(18,4,w-18,h-12),kCard,S(ui,20));
    Label(dc,S(ui,46),S(ui,22),L"RustDesk API",kText,ui.heading);
    Label(dc,S(ui,46),S(ui,63),L"通过服务器事件管理远程解锁和连接结束后的锁定",kMuted,ui.normal);
    Label(dc,S(ui,46),S(ui,150),L"请使用可信服务器，并限制连接方设备 ID。",kMuted,ui.smallFont);
    Label(dc,S(ui,46),S(ui,171),L"RustDesk API 服务器地址（HTTPS）",kMuted,ui.smallFont);
    Label(dc,S(ui,46),S(ui,251),L"SSE API Key（已保存时可留空）",kMuted,ui.smallFont);
    Label(dc,S(ui,46),S(ui,332),L"本机 RustDesk ID",kMuted,ui.smallFont);
    Label(dc,S(ui,46+(std::max(390,w-92)-20)/2+20),S(ui,332),L"允许的连接方 ID（可选）",kMuted,ui.smallFont);
    EndPaint(ui.wnd,&ps);
}
void Draw(DRAWITEMSTRUCT* d,Ui& ui){
    const bool primary=d->CtlID==cSave;
    const bool toggle=d->CtlID==cEnabled||d->CtlID==cLock||d->CtlID==cTrusted;
    const bool disabled=(d->itemState&ODS_DISABLED)!=0;
    const COLORREF bg=disabled?kInput:primary?kBlue:toggle?kSubtle:kInput;
    kb::ui::Rounded(d->hDC,d->rcItem,bg,S(ui,14));
    SetTextColor(d->hDC,primary?kAccentText:kText);
    SetBkMode(d->hDC,TRANSPARENT);SelectObject(d->hDC,ui.normal);
    wchar_t title[180]{};GetWindowTextW(d->hwndItem,title,180);
    RECT rect=d->rcItem;
    if(toggle)rect.left+=S(ui,18);
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
    Appearance(ui);
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
    ui.install=Control(ui.wnd,L"BUTTON",L"安装或修复后台服务",BS_OWNERDRAW,cInstall);
    for(HWND child:{ui.enabled,ui.server,ui.key,ui.device,ui.peer,ui.lock,ui.trusted,ui.test,ui.save,ui.status,ui.install})
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
        auto state=std::make_unique<Ui>();state->wnd=hwnd;state->dpi=kb::ui::Dpi(hwnd);
        state->existing=kb::ReadRemoteConfiguration(state->saved);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state.get()));
        Create(*state);state.release();return 0;
      }
      case WM_SIZE:if(ui){Layout(*ui);InvalidateRect(hwnd,nullptr,TRUE);}return 0;
      case kb::kRemotePageAppearance:if(ui){
        ui->dpi=w?static_cast<UINT>(w):kb::ui::Dpi(hwnd);
        Appearance(*ui);Layout(*ui);return 0;
      }
      case WM_ERASEBKGND:return 1;
      case WM_COMMAND:if(ui){
        const auto id=LOWORD(w);
        if(id==cEnabled){ui->isEnabled=!ui->isEnabled;UpdateButtons(*ui);return 0;}
        if(id==cLock){ui->autoLock=!ui->autoLock;UpdateButtons(*ui);return 0;}
        if(id==cTrusted){ui->trustedEventsOnly=!ui->trustedEventsOnly;UpdateButtons(*ui);return 0;}
        if(id==cTest){Test(*ui);return 0;}
        if(id==cSave){Save(*ui);return 0;}
        if(id==cInstall){
          wchar_t path[32768]{};
          if(GetModuleFileNameW(nullptr,path,32768)){
            auto result=ShellExecuteW(ui->wnd,L"runas",path,L"--install",nullptr,SW_SHOWNORMAL);
            SetWindowTextW(ui->status,reinterpret_cast<INT_PTR>(result)>32?
               L"已启动安装程序。完成后即可使用后台服务。":L"无法启动安装程序。");
          }
          return 0;
        }
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
        for(auto font:{ui->normal,ui->bold,ui->smallFont,ui->heading})DeleteObject(font);
        DeleteObject(ui->cardBrush);DeleteObject(ui->inputBrush);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);delete ui;
      }return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
} // namespace
HWND kb::CreateRemoteSettingsPage(HWND parent){
    HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSEXW klass{sizeof(klass)};klass.hInstance=instance;klass.lpfnWndProc=WndProc;
    klass.lpszClassName=L"KenvixRustDeskSettingsPage";
    klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    klass.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    if(!RegisterClassExW(&klass)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
    return CreateWindowExW(0,klass.lpszClassName,L"",WS_CHILD|WS_CLIPCHILDREN,
                           0,0,1,1,parent,nullptr,instance,nullptr);
}
