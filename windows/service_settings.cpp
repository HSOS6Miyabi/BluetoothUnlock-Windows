#include "service_settings.h"
#include "service_manager.h"
#include "ui_common.h"
#include <windows.h>
#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace {
constexpr int cInstall=301,cStart=302,cStop=303,cUninstall=304,cRefresh=305,cKeep=306;
constexpr UINT kFinished=WM_APP+73;
constexpr UINT_PTR kStatusTimer=5;

struct OperationResult {bool ok{}; std::wstring message;};
struct Ui {
    HWND wnd{},install{},start{},stop{},uninstall{},refresh{},keep{};
    HFONT font{},smallFont{},heading{};
    kb::ui::Palette colors{};
    UINT dpi{96};
    kb::BackgroundServiceInfo service{};
    std::wstring feedback;
    std::jthread worker;
    bool busy{};
    bool keepSettings{true};
};
int S(const Ui& ui,int pixels){return kb::ui::Scale(ui.dpi,pixels);}
HWND Button(HWND parent,int id,const wchar_t* title,DWORD styles=BS_OWNERDRAW){
    return CreateWindowExW(0,L"BUTTON",title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|styles,
        0,0,1,1,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),nullptr);
}
void ApplyAppearance(Ui& ui){
    const auto fresh=kb::ui::Colors();
    const HFONT font=kb::ui::MakeFont(ui.dpi,17),
                smallText=kb::ui::MakeFont(ui.dpi,15),
                heading=kb::ui::MakeFont(ui.dpi,27,FW_SEMIBOLD);
    for(auto child:{ui.install,ui.start,ui.stop,ui.uninstall,ui.refresh,ui.keep})
        if(child)SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
    if(ui.font)DeleteObject(ui.font);
    if(ui.smallFont)DeleteObject(ui.smallFont);
    if(ui.heading)DeleteObject(ui.heading);
    ui.font=font;ui.smallFont=smallText;ui.heading=heading;
    ui.colors=fresh;
    InvalidateRect(ui.wnd,nullptr,TRUE);
}
void Layout(Ui& ui){
    RECT rect{};GetClientRect(ui.wnd,&rect);
    const int w=MulDiv(rect.right,96,ui.dpi);
    auto place=[&](HWND child,int x,int y,int width,int height){
        MoveWindow(child,S(ui,x),S(ui,y),S(ui,width),S(ui,height),TRUE);
    };
    const int content=std::max(620,w-92);
    place(ui.refresh,w-185,22,139,43);
    place(ui.install,46,303,206,51);
    place(ui.start,267,303,140,51);
    place(ui.stop,422,303,140,51);
    place(ui.uninstall,46,439,206,47);
    place(ui.keep,267,440,std::max(245,content-221),46);
}
const wchar_t* StateDescription(kb::BackgroundServiceState state){
    using K=kb::BackgroundServiceState;
    switch(state){
        case K::notInstalled:return L"尚未安装";
        case K::stopped:return L"已停止";
        case K::running:return L"正在运行";
        case K::starting:return L"正在启动";
        case K::stopping:return L"正在停止";
        case K::paused:return L"已暂停";
        case K::unknown:return L"其他状态";
        default:return L"无法读取状态";
    }
}
void Refresh(Ui& ui){
    if(ui.busy)return;
    ui.service=kb::QueryBackgroundService();
    using K=kb::BackgroundServiceState;
    const bool installed=ui.service.state!=K::notInstalled&&ui.service.state!=K::unavailable;
    const bool running=ui.service.state==K::running;
    const bool stopped=ui.service.state==K::stopped;
    EnableWindow(ui.install,TRUE);
    EnableWindow(ui.start,installed&&stopped);
    EnableWindow(ui.stop,installed&&running);
    EnableWindow(ui.uninstall,installed||ui.service.credentialProviderRegistered);
    SetWindowTextW(ui.install,installed?L"重新安装 / 修复":L"安装服务与登录组件");
    InvalidateRect(ui.wnd,nullptr,TRUE);
}
void SetBusy(Ui& ui,bool busy){
    ui.busy=busy;
    if(busy){
        for(auto button:{ui.install,ui.start,ui.stop,ui.uninstall,ui.refresh})
            EnableWindow(button,FALSE);
    }else Refresh(ui);
}
void DrawTextLine(HDC dc,int x,int y,const std::wstring& value,COLORREF color,HFONT font){
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);SelectObject(dc,font);
    TextOutW(dc,x,y,value.c_str(),static_cast<int>(value.size()));
}
void DrawWrapped(HDC dc,RECT rect,const std::wstring& value,COLORREF color,HFONT font){
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);SelectObject(dc,font);
    DrawTextW(dc,value.c_str(),-1,&rect,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
}
void Paint(Ui& ui){
    PAINTSTRUCT ps{};HDC dc=BeginPaint(ui.wnd,&ps);
    RECT client{};GetClientRect(ui.wnd,&client);
    const HBRUSH brush=CreateSolidBrush(ui.colors.background);
    FillRect(dc,&client,brush);DeleteObject(brush);
    const int w=MulDiv(client.right,96,ui.dpi),h=MulDiv(client.bottom,96,ui.dpi);
    auto r=[&](int x,int y,int width,int height){return RECT{S(ui,x),S(ui,y),S(ui,x+width),S(ui,y+height)};};
    kb::ui::Rounded(dc,r(18,4,w-36,h-16),ui.colors.panel,S(ui,22));
    DrawTextLine(dc,S(ui,46),S(ui,26),L"Windows 服务管理",ui.colors.text,ui.heading);
    DrawTextLine(dc,S(ui,46),S(ui,72),L"管理后台连接监听及 Windows 原生登录组件",ui.colors.muted,ui.font);
    kb::ui::Rounded(dc,r(45,110,w-90,128),ui.colors.subtle,S(ui,18));
    DrawTextLine(dc,S(ui,64),S(ui,128),L"RustDesk 后台服务",ui.colors.muted,ui.smallFont);
    DrawTextLine(dc,S(ui,64),S(ui,157),StateDescription(ui.service.state),ui.colors.text,ui.heading);
    DrawTextLine(dc,S(ui,64),S(ui,204),
        ui.service.credentialProviderRegistered?L"原生登录组件：已注册":L"原生登录组件：尚未注册",
        ui.colors.muted,ui.font);
    DrawTextLine(dc,S(ui,46),S(ui,259),L"安装与运行",ui.colors.text,ui.font);
    DrawTextLine(dc,S(ui,46),S(ui,392),L"维护与卸载",ui.colors.text,ui.font);
    DrawWrapped(dc,r(46,505,w-92,58),
        L"卸载将删除后台服务与 Windows 登录组件，原有密码、PIN 和 Windows Hello 登录方式保持可用。",ui.colors.muted,ui.smallFont);
    if(!ui.feedback.empty()){
        DrawWrapped(dc,r(46,570,w-92,std::max(40,h-575)),ui.feedback,
                    ui.colors.muted,ui.smallFont);
    }else if(!ui.service.detail.empty()){
        DrawWrapped(dc,r(46,570,w-92,std::max(40,h-575)),ui.service.detail,
                    ui.colors.muted,ui.smallFont);
    }
    EndPaint(ui.wnd,&ps);
}
void DrawButton(DRAWITEMSTRUCT* item,Ui& ui){
    const bool primary=item->CtlID==cInstall;
    const bool destructive=item->CtlID==cUninstall;
    const bool toggle=item->CtlID==cKeep;
    const bool disabled=(item->itemState&ODS_DISABLED)!=0;
    const COLORREF background=disabled?ui.colors.field:
      primary?ui.colors.accent:(destructive||toggle)?ui.colors.subtle:ui.colors.field;
    kb::ui::Rounded(item->hDC,item->rcItem,background,S(ui,13));
    SetBkMode(item->hDC,TRANSPARENT);
    SetTextColor(item->hDC,primary&&!disabled?ui.colors.accentText:
        disabled?ui.colors.muted:ui.colors.text);
    SelectObject(item->hDC,ui.font);
    wchar_t text[128]{};GetWindowTextW(item->hwndItem,text,128);
    RECT bounds=item->rcItem;
    DrawTextW(item->hDC,text,-1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    if(item->itemState&ODS_FOCUS)DrawFocusRect(item->hDC,&bounds);
}
void Action(Ui& ui,int id){
    if(ui.busy)return;
    if(id==cUninstall){
        if(MessageBoxW(ui.wnd,L"确认卸载后台服务及 Windows 登录组件吗？\n可以继续使用 Windows 密码、PIN 和 Windows Hello 登录。",
                L"卸载 Kenvix Bluetooth Unlock",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    }
    const bool keep=ui.keepSettings;
    ui.feedback=L"正在执行服务操作…";
    SetBusy(ui,true);
    InvalidateRect(ui.wnd,nullptr,TRUE);
    if(ui.worker.joinable())ui.worker.join();
    ui.worker=std::jthread([target=ui.wnd,id,keep](std::stop_token){
        auto output=std::make_unique<OperationResult>();
        switch(id){
            case cInstall:output->ok=kb::InstallApplication(output->message);break;
            case cStart:output->ok=kb::StartBackgroundService(output->message);break;
            case cStop:output->ok=kb::StopBackgroundService(output->message);break;
            case cUninstall:output->ok=kb::UninstallApplication(keep,output->message);break;
            default:output->message=L"未知的服务操作。";break;
        }
        if(PostMessageW(target,kFinished,0,reinterpret_cast<LPARAM>(output.get())))output.release();
    });
}
LRESULT CALLBACK WndProc(HWND window,UINT msg,WPARAM w,LPARAM l){
    auto* ui=reinterpret_cast<Ui*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    switch(msg){
        case WM_CREATE:{
            auto state=std::make_unique<Ui>();
            state->wnd=window;state->dpi=kb::ui::Dpi(window);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state.get()));
            state->install=Button(window,cInstall,L"安装服务与登录组件");
            state->start=Button(window,cStart,L"启动服务");
            state->stop=Button(window,cStop,L"停止服务");
            state->uninstall=Button(window,cUninstall,L"卸载程序");
            state->refresh=Button(window,cRefresh,L"刷新状态");
            state->keep=Button(window,cKeep,L"●  卸载时保留设置");
            ApplyAppearance(*state);
            Layout(*state);
            Refresh(*state);
            state.release();return 0;
        }
        case WM_SIZE:if(ui){Layout(*ui);InvalidateRect(window,nullptr,TRUE);}return 0;
        case kb::kServicePageAppearance:if(ui){
            ui->dpi=w?static_cast<UINT>(w):kb::ui::Dpi(window);
            ApplyAppearance(*ui);Layout(*ui);return 0;
        }
        case WM_SHOWWINDOW:if(ui){
            if(w){Refresh(*ui);SetTimer(window,kStatusTimer,5000,nullptr);}
            else KillTimer(window,kStatusTimer);
            return 0;
        }break;
        case WM_TIMER:if(ui&&w==kStatusTimer){Refresh(*ui);return 0;}break;
        case WM_COMMAND:if(ui){
            const int id=LOWORD(w);
            if(id==cRefresh){Refresh(*ui);return 0;}
            if(id==cKeep){
                ui->keepSettings=!ui->keepSettings;
                SetWindowTextW(ui->keep,ui->keepSettings?
                    L"●  卸载时保留设置":L"○  卸载时删除设置");
                InvalidateRect(ui->keep,nullptr,TRUE);
                return 0;
            }
            if(id==cInstall||id==cStart||id==cStop||id==cUninstall){Action(*ui,id);return 0;}
        }break;
        case kFinished:if(ui){
            std::unique_ptr<OperationResult> result(reinterpret_cast<OperationResult*>(l));
            ui->feedback=result->message;
            SetBusy(*ui,false);
            InvalidateRect(window,nullptr,TRUE);
            return 0;
        }
        case WM_ERASEBKGND:return 1;
        case WM_DRAWITEM:if(ui){DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(l),*ui);return TRUE;}break;
        case WM_CTLCOLORBTN:case WM_CTLCOLORSTATIC:if(ui){
            HDC dc=reinterpret_cast<HDC>(w);
            SetBkColor(dc,ui->colors.panel);SetTextColor(dc,ui->colors.text);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }break;
        case WM_PAINT:if(ui){Paint(*ui);return 0;}
        case WM_DESTROY:if(ui){
            KillTimer(window,kStatusTimer);
            if(ui->worker.joinable())ui->worker.join();
            MSG pending{};
            while(PeekMessageW(&pending,window,kFinished,kFinished,PM_REMOVE))
                delete reinterpret_cast<OperationResult*>(pending.lParam);
            for(auto font:{ui->font,ui->smallFont,ui->heading})if(font)DeleteObject(font);
            SetWindowLongPtrW(window,GWLP_USERDATA,0);delete ui;
        }return 0;
    }
    return DefWindowProcW(window,msg,w,l);
}
} // namespace

HWND kb::CreateServiceSettingsPage(HWND parent){
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpfnWndProc=WndProc;
    cls.lpszClassName=L"KenvixServiceSettingsPage";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    cls.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
    return CreateWindowExW(0,cls.lpszClassName,L"",WS_CHILD|WS_CLIPCHILDREN,
                           0,0,1,1,parent,nullptr,cls.hInstance,nullptr);
}