#include "ble.h"
#include "security.h"
#include "storage.h"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <winrt/base.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#pragma comment(lib,"comctl32.lib")
namespace {
constexpr COLORREF kBackground=RGB(13,20,33);
constexpr COLORREF kPanel=RGB(24,36,54);
constexpr COLORREF kEdit=RGB(34,47,67);
constexpr COLORREF kText=RGB(237,243,253);
constexpr COLORREF kMuted=RGB(156,176,201);
constexpr COLORREF kAccent=RGB(89,155,245);
constexpr int cHigh=110,cLow=111,cDevices=112,cReadKey=113,cSlider=114,cUser=115,
              cDomain=116,cPassword=117,cSave=118,cVerified=119,cStatus=120,cSignal=121,cFingerprint=122,cRemote=123,cOnlyRemote=124;
constexpr UINT msgKey=WM_APP+2,msgSave=WM_APP+3;
struct KeyResult{uint64_t address{};bool ok{};kenvix::PublicKey key{};};
struct SaveResult{bool ok{};std::wstring message;kb::Configuration saved;};
struct Ui {
  HWND host{};
  HWND high{},low{},remoteOnly{},devices{},readKey{},slider{},username{},domain{},password{},save{},verified{},status{},signal{},fingerprint{},remote{};
  HFONT font{},heading{},smallFont{};
  HBRUSH editBrush{},panelBrush{};
  kb::BleScanner scanner;
  kb::Configuration config{};
  bool hadConfig{};
  kb::Mode selectedMode{kb::Mode::high};
  kenvix::PublicKey key{};
  uint64_t keyAddress{};
  uint64_t selectedAddress{};
  std::vector<kb::SeenDevice> rows;
  std::jthread keyJob,saveJob;
  bool busy{};
};
std::wstring GetText(HWND child){
  const int n=GetWindowTextLengthW(child);
  if(n<=0)return {};
  std::wstring text(static_cast<size_t>(n)+1,L'\0');
  GetWindowTextW(child,text.data(),n+1);text.resize(wcslen(text.c_str()));return text;
}
void Text(HDC dc,int x,int y,const wchar_t* value,COLORREF color,HFONT font){
  SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);SelectObject(dc,font);
  TextOutW(dc,x,y,value,static_cast<int>(wcslen(value)));
}
void FillRound(HDC dc,RECT rect,COLORREF background,int radius=20){
  HBRUSH brush=CreateSolidBrush(background);
  HPEN pen=CreatePen(PS_SOLID,1,background);
  HGDIOBJ oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
  RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);
  SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
}
std::wstring FormatAddress(uint64_t addr){
  wchar_t buf[25]{};swprintf_s(buf,L"%012llX",static_cast<unsigned long long>(addr));
  return buf;
}
void SetState(Ui& ui,const std::wstring& message){SetWindowTextW(ui.status,message.c_str());}
HWND Child(HWND parent,const wchar_t* cls,const wchar_t* text,DWORD style,int id){
  return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,
     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
}
void Font(HWND control,HFONT font){SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
void Place(HWND child,int x,int y,int width,int height){MoveWindow(child,x,y,width,height,TRUE);}
void UpdateModeControls(Ui& ui){
  const bool ble=ui.selectedMode!=kb::Mode::remote;
  const bool phone=ui.selectedMode==kb::Mode::high;
  EnableWindow(ui.devices,ble);
  EnableWindow(ui.slider,ble);
  EnableWindow(ui.readKey,phone);
  ShowWindow(ui.fingerprint,phone?SW_SHOW:SW_HIDE);
  ShowWindow(ui.verified,phone?SW_SHOW:SW_HIDE);
}
void RefreshLabels(Ui& ui){
  int n=static_cast<int>(SendMessageW(ui.slider,TBM_GETPOS,0,0))-100;
  std::wstring label=std::format(L"最低信号强度：{} dBm",n);
  SetWindowTextW(ui.signal,label.c_str());
}
void Layout(Ui& ui){
  RECT rect{};GetClientRect(ui.host,&rect);const int width=rect.right;
  const int left=30;
  const int column=std::max(375,static_cast<int>(width*0.41));
  const int x=column+32;const int rightWidth=std::max(350,width-x-34);
  Place(ui.remote,width-272,88,242,46);
  const int buttonWidth=(column-left-24)/3;
  Place(ui.high,left,170,buttonWidth,62);
  Place(ui.low,left+buttonWidth+12,170,buttonWidth,62);
  Place(ui.remoteOnly,left+(buttonWidth+12)*2,170,buttonWidth,62);
  Place(ui.devices,left,286,column-left-8,250);
  Place(ui.readKey,left,558,column-left-8,48);
  Place(ui.fingerprint,left,626,column-left-8,30);
  Place(ui.verified,left,660,column-left-8,32);
  Place(ui.signal,x,183,rightWidth,32);
  Place(ui.slider,x,228,rightWidth,42);
  Place(ui.username,x,332,(rightWidth-16)/2,42);
  Place(ui.domain,x+(rightWidth-16)/2+16,332,(rightWidth-16)/2,42);
  Place(ui.password,x,427,rightWidth,42);
  Place(ui.save,x,512,rightWidth,50);
  Place(ui.status,x,584,rightWidth,87);
}
void Populate(Ui& ui){
  uint64_t selected=ui.selectedAddress;
  auto fresh=ui.scanner.Snapshot();
  if(fresh.size()==ui.rows.size()){
    bool identical=true;
    for(size_t i=0;i<fresh.size();++i)
      if(fresh[i].address!=ui.rows[i].address||fresh[i].rssi!=ui.rows[i].rssi)identical=false;
    if(identical)return;
  }
  ui.rows=std::move(fresh);
  SendMessageW(ui.devices,WM_SETREDRAW,FALSE,0);SendMessageW(ui.devices,LB_RESETCONTENT,0,0);
  for(size_t i=0;i<ui.rows.size();++i){
    const auto& row=ui.rows[i];
    auto s=std::format(L"{}   {}    {} dBm",row.phoneService?L"Android 手机":L"蓝牙设备",
                       FormatAddress(row.address),row.rssi);
    SendMessageW(ui.devices,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(s.c_str()));
    if(row.address==selected)SendMessageW(ui.devices,LB_SETCURSEL,i,0);
  }
  SendMessageW(ui.devices,WM_SETREDRAW,TRUE,0);InvalidateRect(ui.devices,nullptr,TRUE);
}
void Save(Ui& ui){
  if(ui.busy)return;
  kb::Configuration candidate;
  candidate.mode=ui.selectedMode;
  candidate.lowAddress=candidate.mode==kb::Mode::low?ui.selectedAddress:0;
  candidate.minRssi=static_cast<int>(SendMessageW(ui.slider,TBM_GETPOS,0,0))-100;
  candidate.username=GetText(ui.username);candidate.domain=GetText(ui.domain);
  if(candidate.username.empty()||candidate.domain.empty()){
    SetState(ui,L"请填写 Windows 用户名和计算机名或域名。");return;
  }
  if(candidate.mode==kb::Mode::high){
    const bool existingKey=ui.hadConfig&&ui.config.mode==kb::Mode::high&&
      ui.selectedAddress==0&&ui.keyAddress==0&&ui.key==ui.config.trustedKey&&
      kenvix::ValidPublicKey(ui.key);
    const bool newKey=ui.selectedAddress!=0&&ui.keyAddress==ui.selectedAddress&&
      kenvix::ValidPublicKey(ui.key);
    if(!existingKey&&!newKey){
      SetState(ui,L"请选择 Android 手机并读取公钥。");return;
    }
    if(newKey&&SendMessageW(ui.verified,BM_GETCHECK,0,0)!=BST_CHECKED){
      SetState(ui,L"请在手机上核对公钥指纹并勾选确认。");return;
    }
    candidate.trustedKey=ui.key;
  }else if(candidate.mode==kb::Mode::low&&candidate.lowAddress==0){
    SetState(ui,L"请在附近设备中选择已经与 Windows 配对的手环。");return;
  }
  std::wstring password=GetText(ui.password);
  if(password.empty()&&ui.hadConfig&&candidate.username==ui.config.username&&
      candidate.domain==ui.config.domain){
    candidate.passwordEncrypted=ui.config.passwordEncrypted;
  }else if(password.empty()){
    SetState(ui,L"首次设置或更换登录账户时需要填写 Windows 账户密码。");return;
  }
  ui.busy=true;EnableWindow(ui.save,FALSE);SetState(ui,L"正在检查账户和设备并保存配置…");
  if(ui.saveJob.joinable())ui.saveJob.join();
  const HWND host=ui.host;
  ui.saveJob=std::jthread([host,candidate,password=std::move(password)](std::stop_token)mutable{
    auto* result=new SaveResult;
    auto cfg=candidate;
    bool paired=true;
    if(cfg.mode==kb::Mode::low){
      try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        paired=kb::IsPairedBleDevice(cfg.lowAddress);
        winrt::uninit_apartment();
      }catch(...){paired=false;}
    }
    if(!paired)
      result->message=L"此设备尚未与 Windows 配对。请先在系统蓝牙设置中完成配对。";
    else if(!(cfg.userSid=kb::ResolveUserSid(cfg.username,cfg.domain)).size())
      result->message=L"无法找到这个 Windows 登录账户，请检查账户名和域名。";
    else if(!password.empty()&&!kb::ValidatePassword(cfg.username,cfg.domain,password))
      result->message=L"Windows 账户密码验证失败，请检查登录密码。";
    else if(!password.empty()&&!kb::EncryptPassword(password,cfg.passwordEncrypted))
      result->message=L"无法加密 Windows 账户密码。";
    else if(kb::SaveConfig(cfg)){
      result->ok=true;result->saved=std::move(cfg);
      result->message=L"配置保存成功。按 Win + L 可检查所选解锁方式。";
    }else result->message=L"无法保存配置。请确认已使用管理员权限打开设置程序。";
    kb::Wipe(password);
    if(!PostMessageW(host,msgSave,0,reinterpret_cast<LPARAM>(result)))delete result;
  });
}
void ReadKey(Ui& ui){
  if(ui.keyJob.joinable())ui.keyJob.join();
  if(!ui.selectedAddress){SetState(ui,L"请先选择附近的 Android 手机。");return;}
  const auto address=ui.selectedAddress;
  SetState(ui,L"正在通过蓝牙连接读取手机公钥…");
  ui.keyJob=std::jthread([address,host=ui.host](std::stop_token){
    auto* result=new KeyResult;result->address=address;
    bool comInitialized=false;
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
      comInitialized=true;
      result->ok=kb::ReadPhonePublicKey(address,result->key);
    } catch(...) {result->ok=false;}
    if(comInitialized)winrt::uninit_apartment();
    if(!PostMessageW(host,msgKey,0,reinterpret_cast<LPARAM>(result)))delete result;
  });
}
void CreateControls(Ui& ui){
  ui.font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  ui.heading=CreateFontW(-32,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  ui.smallFont=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  ui.editBrush=CreateSolidBrush(kEdit);ui.panelBrush=CreateSolidBrush(kPanel);
  ui.remote=Child(ui.host,L"BUTTON",L"RustDesk API 设置",BS_OWNERDRAW,cRemote);
  ui.high=Child(ui.host,L"BUTTON",L"手机验证",BS_OWNERDRAW,cHigh);
  ui.low=Child(ui.host,L"BUTTON",L"手环检测",BS_OWNERDRAW,cLow);
  ui.remoteOnly=Child(ui.host,L"BUTTON",L"仅 RustDesk",BS_OWNERDRAW,cOnlyRemote);
  ui.devices=Child(ui.host,L"LISTBOX",L"",WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_BORDER,cDevices);
  ui.readKey=Child(ui.host,L"BUTTON",L"读取手机公钥",BS_OWNERDRAW,cReadKey);
  ui.slider=Child(ui.host,TRACKBAR_CLASSW,L"",TBS_AUTOTICKS,cSlider);
  ui.username=Child(ui.host,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP|WS_BORDER,cUser);
  ui.domain=Child(ui.host,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP|WS_BORDER,cDomain);
  ui.password=Child(ui.host,L"EDIT",L"",ES_PASSWORD|ES_AUTOHSCROLL|WS_TABSTOP|WS_BORDER,cPassword);
  ui.save=Child(ui.host,L"BUTTON",L"保存设置",BS_OWNERDRAW,cSave);
  ui.verified=Child(ui.host,L"BUTTON",L"我已核对双方显示的公钥指纹",BS_AUTOCHECKBOX,cVerified);
  ui.status=Child(ui.host,L"STATIC",L"选择设备并配置 Windows 登录账户。",SS_LEFT,cStatus);
  ui.signal=Child(ui.host,L"STATIC",L"",SS_LEFT,cSignal);
  ui.fingerprint=Child(ui.host,L"STATIC",L"公钥指纹：尚未读取",SS_LEFT,cFingerprint);
  for(HWND c:{ui.remote,ui.high,ui.low,ui.remoteOnly,ui.devices,ui.readKey,ui.slider,ui.username,ui.domain,
              ui.password,ui.save,ui.verified,ui.status,ui.signal,ui.fingerprint})Font(c,ui.font);
  SendMessageW(ui.slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,65));
  SendMessageW(ui.slider,TBM_SETPOS,TRUE,30);
  if(ui.hadConfig){
    SetWindowTextW(ui.username,ui.config.username.c_str());SetWindowTextW(ui.domain,ui.config.domain.c_str());
    SendMessageW(ui.slider,TBM_SETPOS,TRUE,ui.config.minRssi+100);
    ui.selectedMode=ui.config.mode;
    ui.selectedAddress=ui.config.mode==kb::Mode::low?ui.config.lowAddress:0;
    if(ui.selectedMode==kb::Mode::high){ui.key=ui.config.trustedKey;ui.keyAddress=0;
      auto fp=kb::Fingerprint(ui.key);SetWindowTextW(ui.fingerprint,(L"已保存指纹："+fp).c_str());}
    SetState(ui,L"已读取上次配置。修改信号强度时可以留空密码。");
  }else{
    wchar_t username[257]{};DWORD len=257;
    if(GetUserNameW(username,&len))SetWindowTextW(ui.username,username);
    SetWindowTextW(ui.domain,L".");
  }
  UpdateModeControls(ui);
  RefreshLabels(ui);Layout(ui);
}
void Paint(HWND hwnd,Ui& ui){
  PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);
  RECT r{};GetClientRect(hwnd,&r);
  HBRUSH backgroundBrush=CreateSolidBrush(kBackground);
  FillRect(dc,&r,backgroundBrush);DeleteObject(backgroundBrush);
  const int col=std::max(375,static_cast<int>(r.right*0.41));
  FillRound(dc,{20,142,col+4,r.bottom-27},kPanel,24);
  FillRound(dc,{col+20,142,r.right-20,r.bottom-27},kPanel,24);
  Text(dc,30,34,L"Kenvix",kAccent,ui.heading);
  Text(dc,158,42,L"Bluetooth Unlock",kText,ui.heading);
  Text(dc,32,94,L"通过蓝牙设备或已认证的 RustDesk 远程连接自动解锁 Windows",kMuted,ui.font);
  Text(dc,30,251,L"附近的蓝牙设备",kText,ui.font);
  Text(dc,col+32,153,L"解锁条件",kText,ui.font);
  Text(dc,col+32,301,L"Windows 用户名",kMuted,ui.smallFont);
  Text(dc,col+32+(std::max<int>(350,r.right-col-66)-16)/2+16,301,L"计算机名或域名",kMuted,ui.smallFont);
  Text(dc,col+32,394,L"Windows 账户密码（已有配置时可以留空）",kMuted,ui.smallFont);
  Text(dc,col+32,480,L"保存后，系统原有登录选项将继续保留。",kMuted,ui.smallFont);
  EndPaint(hwnd,&ps);
}
void DrawButton(DRAWITEMSTRUCT* info,Ui& ui){
  const bool selected=(info->CtlID==cHigh&&ui.selectedMode==kb::Mode::high)||
                      (info->CtlID==cLow&&ui.selectedMode==kb::Mode::low)||
                      (info->CtlID==cOnlyRemote&&ui.selectedMode==kb::Mode::remote);
  const bool primary=info->CtlID==cSave;
  const bool disabled=(info->itemState&ODS_DISABLED)!=0;
  COLORREF bg=disabled?kEdit:(selected||primary?kAccent:kEdit);
  FillRound(info->hDC,info->rcItem,bg,16);
  SetTextColor(info->hDC,(selected||primary)?RGB(9,20,32):kText);
  SetBkMode(info->hDC,TRANSPARENT);SelectObject(info->hDC,ui.font);
  wchar_t text[150]{};GetWindowTextW(info->hwndItem,text,150);
  DrawTextW(info->hDC,text,-1,&info->rcItem,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
  if(info->itemState&ODS_FOCUS)DrawFocusRect(info->hDC,&info->rcItem);
}
LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
  auto* ui=reinterpret_cast<Ui*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  switch(msg){
    case WM_CREATE:{
      auto app=std::make_unique<Ui>();app->host=hwnd;
      app->hadConfig=kb::ReadConfig(app->config);
      SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app.get()));
      CreateControls(*app);
      try{app->scanner.Start();SetTimer(hwnd,1,900,nullptr);}catch(...){SetState(*app,L"蓝牙不可用，仍可使用 RustDesk API 解锁。 ");}
      app.release();return 0;
    }
    case WM_GETMINMAXINFO:{auto* mm=reinterpret_cast<MINMAXINFO*>(l);mm->ptMinTrackSize={900,720};return 0;}
    case WM_SIZE:if(ui){Layout(*ui);InvalidateRect(hwnd,nullptr,TRUE);}return 0;
    case WM_TIMER:if(ui&&w==1){Populate(*ui);}return 0;
    case WM_HSCROLL:if(ui&&reinterpret_cast<HWND>(l)==ui->slider){RefreshLabels(*ui);return 0;}break;
    case WM_COMMAND:if(ui){
      const auto id=LOWORD(w),code=HIWORD(w);
      if(id==cHigh||id==cLow||id==cOnlyRemote){
        ui->selectedMode=id==cHigh?kb::Mode::high:id==cLow?kb::Mode::low:kb::Mode::remote;
        for(HWND control:{ui->high,ui->low,ui->remoteOnly})InvalidateRect(control,nullptr,TRUE);
        UpdateModeControls(*ui);return 0;
      }
      if(id==cDevices&&code==LBN_SELCHANGE){
        int index=static_cast<int>(SendMessageW(ui->devices,LB_GETCURSEL,0,0));
        if(index>=0&&static_cast<size_t>(index)<ui->rows.size()){
          const auto addr=ui->rows[index].address;
          if(addr!=ui->selectedAddress){
            ui->selectedAddress=addr;ui->keyAddress=0;ui->key={};
            SetWindowTextW(ui->fingerprint,L"公钥指纹：尚未读取");
            SendMessageW(ui->verified,BM_SETCHECK,BST_UNCHECKED,0);
          }
        }return 0;
      }
      if(id==cRemote){
        wchar_t module[32768]{};
        if(GetModuleFileNameW(nullptr,module,32768)){
          std::wstring path=module;
          auto slash=path.find_last_of(L"\\/");
          if(slash!=std::wstring::npos){
            path.resize(slash+1);path+=L"KenvixRustDeskSettings.exe";
            auto result=ShellExecuteW(hwnd,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            if(reinterpret_cast<INT_PTR>(result)<=32)
              SetState(*ui,L"无法打开 RustDesk 设置窗口，请重新安装程序。 ");
          }
        }
        return 0;
      }
      if(id==cReadKey){ReadKey(*ui);return 0;}
      if(id==cSave){Save(*ui);return 0;}
    }break;
    case msgKey:if(ui){
      std::unique_ptr<KeyResult> result(reinterpret_cast<KeyResult*>(l));
      if(result->address==ui->selectedAddress){
        if(result->ok){
          ui->key=result->key;ui->keyAddress=result->address;
          SetWindowTextW(ui->fingerprint,(L"公钥指纹："+kb::Fingerprint(ui->key)).c_str());
          SetState(*ui,L"请对照 Android 应用显示的指纹，并勾选确认。");
        }else SetState(*ui,L"无法读取手机公钥，请确认手机已启动蓝牙认证服务。");
      }return 0;
    }break;
    case msgSave:if(ui){
      std::unique_ptr<SaveResult> result(reinterpret_cast<SaveResult*>(l));
      ui->busy=false;EnableWindow(ui->save,TRUE);
      if(result->ok){
        ui->config=std::move(result->saved);ui->hadConfig=true;
        SetWindowTextW(ui->password,L"");
      }
      SetState(*ui,result->message);return 0;
    }break;
    case WM_DRAWITEM:if(ui){DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(l),*ui);return TRUE;}break;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:if(ui){
      HDC dc=reinterpret_cast<HDC>(w);SetTextColor(dc,kText);SetBkColor(dc,kPanel);
      return reinterpret_cast<LRESULT>(ui->panelBrush);
    }break;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:if(ui){
      HDC dc=reinterpret_cast<HDC>(w);SetTextColor(dc,kText);SetBkColor(dc,kEdit);
      return reinterpret_cast<LRESULT>(ui->editBrush);
    }break;
    case WM_PAINT:if(ui){Paint(hwnd,*ui);return 0;}break;
    case WM_DESTROY:if(ui){
      KillTimer(hwnd,1);ui->scanner.Stop();
      if(ui->keyJob.joinable())ui->keyJob.join();
      if(ui->saveJob.joinable())ui->saveJob.join();
      MSG pending{};
      while(PeekMessageW(&pending,hwnd,msgKey,msgKey,PM_REMOVE))
        delete reinterpret_cast<KeyResult*>(pending.lParam);
      while(PeekMessageW(&pending,hwnd,msgSave,msgSave,PM_REMOVE))
        delete reinterpret_cast<SaveResult*>(pending.lParam);
      DeleteObject(ui->editBrush);DeleteObject(ui->panelBrush);
      DeleteObject(ui->font);DeleteObject(ui->heading);DeleteObject(ui->smallFont);
      SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);delete ui;
    }PostQuitMessage(0);return 0;
  }
  return DefWindowProcW(hwnd,msg,w,l);
}
} // namespace
int APIENTRY wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int show){
  winrt::init_apartment(winrt::apartment_type::single_threaded);
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
  const BOOL dark=TRUE;
  WNDCLASSEXW cls{sizeof(cls)};
  cls.hInstance=inst;cls.lpfnWndProc=WndProc;cls.lpszClassName=L"KenvixBluetoothUnlockSettings";
  cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
  cls.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  if(!RegisterClassExW(&cls))return 1;
  HWND window=CreateWindowExW(0,cls.lpszClassName,L"Kenvix Bluetooth Unlock · 设置",
                   WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1030,770,
                   nullptr,nullptr,inst,nullptr);
  if(!window)return 1;
  DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
  ShowWindow(window,show);UpdateWindow(window);
  MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
  return static_cast<int>(msg.wParam);
}
