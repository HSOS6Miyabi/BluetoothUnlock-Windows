#include "ble.h"
#include "security.h"
#include "storage.h"
#include "remote_settings.h"
#include "service_manager.h"
#include "ui_common.h"
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
COLORREF kBackground=RGB(247,249,252);
COLORREF kPanel=RGB(255,255,255);
COLORREF kEdit=RGB(243,247,252);
COLORREF kText=RGB(30,42,60);
COLORREF kMuted=RGB(92,108,130);
COLORREF kAccent=RGB(39,105,213);
COLORREF kAccentText=RGB(255,255,255),kBorder=RGB(215,224,237),kSubtle=RGB(233,241,253);
constexpr int cHigh=110,cLow=111,cDevices=112,cReadKey=113,cSlider=114,cUser=115,
              cDomain=116,cPassword=117,cSave=118,cVerified=119,cStatus=120,cSignal=121,cFingerprint=122,cRemote=123,cOnlyRemote=124,cBleTab=125;
constexpr UINT msgKey=WM_APP+2,msgSave=WM_APP+3;
struct KeyResult{uint64_t address{};bool ok{};kenvix::PublicKey key{};};
struct SaveResult{bool ok{};std::wstring message;kb::Configuration saved;};
struct Ui {
  HWND host{};
  HWND high{},low{},remoteOnly{},devices{},readKey{},slider{},username{},domain{},password{},save{},verified{},status{},signal{},fingerprint{},remote{},bleTab{},remotePage{};
  HFONT font{},heading{},smallFont{};
  HBRUSH editBrush{},panelBrush{};
  kb::ui::Palette palette{};
  UINT dpi{96};
  bool remoteActive{};
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
int S(const Ui& ui,int value){return kb::ui::Scale(ui.dpi,value);}
void Appearance(Ui& ui){
  ui.palette=kb::ui::Colors();
  kBackground=ui.palette.background;kPanel=ui.palette.panel;kEdit=ui.palette.field;
  kText=ui.palette.text;kMuted=ui.palette.muted;kAccent=ui.palette.accent;
  kAccentText=ui.palette.accentText;kBorder=ui.palette.border;kSubtle=ui.palette.subtle;
  HFONT normal=kb::ui::MakeFont(ui.dpi,18);
  HFONT heading=kb::ui::MakeFont(ui.dpi,31,FW_SEMIBOLD);
  HFONT small=kb::ui::MakeFont(ui.dpi,16);
  if(ui.font){
    for(HWND item:{ui.bleTab,ui.remote,ui.high,ui.low,ui.remoteOnly,ui.devices,
        ui.readKey,ui.slider,ui.username,ui.domain,ui.password,ui.save,ui.verified,
        ui.status,ui.signal,ui.fingerprint})
      if(item)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(normal),FALSE);
  }
  if(ui.font)DeleteObject(ui.font);if(ui.heading)DeleteObject(ui.heading);
  if(ui.smallFont)DeleteObject(ui.smallFont);
  ui.font=normal;ui.heading=heading;ui.smallFont=small;
  if(ui.editBrush)DeleteObject(ui.editBrush);
  if(ui.panelBrush)DeleteObject(ui.panelBrush);
  ui.editBrush=CreateSolidBrush(kEdit);ui.panelBrush=CreateSolidBrush(kPanel);
  if(ui.host){kb::ui::Titlebar(ui.host,ui.palette.dark);InvalidateRect(ui.host,nullptr,TRUE);}
  if(ui.remotePage)SendMessageW(ui.remotePage,kb::kRemotePageAppearance,ui.dpi,0);
}
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
void SetPage(Ui& ui,bool rustDesk){
  ui.remoteActive=rustDesk;
  for(HWND child:{ui.high,ui.low,ui.remoteOnly,ui.devices,ui.readKey,ui.slider,
      ui.username,ui.domain,ui.password,ui.save,ui.verified,ui.status,ui.signal,ui.fingerprint})
    ShowWindow(child,rustDesk?SW_HIDE:SW_SHOW);
  if(!rustDesk)UpdateModeControls(ui);
  if(ui.remotePage)ShowWindow(ui.remotePage,rustDesk?SW_SHOW:SW_HIDE);
  InvalidateRect(ui.bleTab,nullptr,TRUE);InvalidateRect(ui.remote,nullptr,TRUE);
  InvalidateRect(ui.host,nullptr,TRUE);
}
void Layout(Ui& ui){
  RECT physical{};GetClientRect(ui.host,&physical);
  const int w=MulDiv(physical.right,96,ui.dpi),h=MulDiv(physical.bottom,96,ui.dpi);
  const int col=std::max(415,static_cast<int>(w*0.43));
  const int rightX=col+34,rightWidth=std::max(385,w-rightX-38);
  auto place=[&](HWND child,int x,int y,int width,int height){
    if(child)Place(child,S(ui,x),S(ui,y),S(ui,width),S(ui,height));
  };
  place(ui.bleTab,26,119,178,47);
  place(ui.remote,216,119,189,47);
  place(ui.remotePage,0,182,w,std::max(550,h-182));
  const int left=42;
  const int segment=std::max(119,(col-65-22)/3);
  place(ui.high,left,212,segment,52);
  place(ui.low,left+segment+10,212,segment,52);
  place(ui.remoteOnly,left+(segment+10)*2,212,segment,52);
  place(ui.devices,left,333,col-left-14,std::max(190,h-333-165));
  place(ui.readKey,left,h-150,col-left-14,48);
  place(ui.fingerprint,left,h-90,col-left-14,29);
  place(ui.verified,left,h-53,col-left-14,35);
  place(ui.signal,rightX,252,rightWidth,32);
  place(ui.slider,rightX,297,rightWidth,42);
  place(ui.username,rightX,402,(rightWidth-16)/2,47);
  place(ui.domain,rightX+(rightWidth-16)/2+16,402,(rightWidth-16)/2,47);
  place(ui.password,rightX,502,rightWidth,47);
  place(ui.save,rightX,h-145,rightWidth,55);
  place(ui.status,rightX,h-76,rightWidth,52);
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
  Appearance(ui);
  ui.bleTab=Child(ui.host,L"BUTTON",L"蓝牙解锁",BS_OWNERDRAW,cBleTab);
  ui.remote=Child(ui.host,L"BUTTON",L"RustDesk API",BS_OWNERDRAW,cRemote);
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
  for(HWND c:{ui.bleTab,ui.remote,ui.high,ui.low,ui.remoteOnly,ui.devices,ui.readKey,ui.slider,ui.username,ui.domain,
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
  ui.remotePage=kb::CreateRemoteSettingsPage(ui.host);
  RefreshLabels(ui);Layout(ui);
  SetPage(ui,false);
}
void Paint(HWND hwnd,Ui& ui){
  PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);
  RECT screen{};GetClientRect(hwnd,&screen);
  HBRUSH bg=CreateSolidBrush(kBackground);FillRect(dc,&screen,bg);DeleteObject(bg);
  const int w=MulDiv(screen.right,96,ui.dpi),h=MulDiv(screen.bottom,96,ui.dpi);
  Text(dc,S(ui,28),S(ui,26),L"Kenvix",kAccent,ui.heading);
  Text(dc,S(ui,157),S(ui,26),L"Bluetooth Unlock",kText,ui.heading);
  Text(dc,S(ui,29),S(ui,81),L"让经过授权的设备和远程连接安全地唤醒 Windows 登录会话",kMuted,ui.font);
  HPEN divider=CreatePen(PS_SOLID,1,kBorder);auto old=SelectObject(dc,divider);
  MoveToEx(dc,S(ui,25),S(ui,176),nullptr);LineTo(dc,S(ui,w-25),S(ui,176));
  SelectObject(dc,old);DeleteObject(divider);
  if(!ui.remoteActive){
    const int col=std::max(415,static_cast<int>(w*0.43)),rightX=col+34;
    kb::ui::Rounded(dc,{S(ui,24),S(ui,193),S(ui,col+2),S(ui,h-19)},kPanel,S(ui,21));
    kb::ui::Rounded(dc,{S(ui,col+18),S(ui,193),S(ui,w-24),S(ui,h-19)},kPanel,S(ui,21));
    Text(dc,S(ui,42),S(ui,285),L"附近的蓝牙设备",kText,ui.font);
    Text(dc,S(ui,rightX),S(ui,214),L"解锁条件",kText,ui.font);
    Text(dc,S(ui,rightX),S(ui,370),L"Windows 用户名",kMuted,ui.smallFont);
    const int rightWidth=std::max(385,w-rightX-38);
    Text(dc,S(ui,rightX+(rightWidth-16)/2+16),S(ui,370),L"计算机名或域名",kMuted,ui.smallFont);
    Text(dc,S(ui,rightX),S(ui,469),L"Windows 账户密码（已保存且账户未变更时可以留空）",kMuted,ui.smallFont);
    Text(dc,S(ui,rightX),S(ui,h-190),L"保存后，Windows 原有的登录方式将继续保留。",kMuted,ui.smallFont);
  }
  EndPaint(hwnd,&ps);
}
void DrawButton(DRAWITEMSTRUCT* info,Ui& ui){
  const bool tab=info->CtlID==cBleTab||info->CtlID==cRemote;
  const bool selected=(info->CtlID==cBleTab&&!ui.remoteActive)||
      (info->CtlID==cRemote&&ui.remoteActive)||
      (info->CtlID==cHigh&&ui.selectedMode==kb::Mode::high)||
      (info->CtlID==cLow&&ui.selectedMode==kb::Mode::low)||
      (info->CtlID==cOnlyRemote&&ui.selectedMode==kb::Mode::remote);
  const bool primary=info->CtlID==cSave;
  const bool disabled=(info->itemState&ODS_DISABLED)!=0;
  COLORREF color=disabled?kEdit:primary?kAccent:selected?(tab?kSubtle:kAccent):kEdit;
  kb::ui::Rounded(info->hDC,info->rcItem,color,S(ui,13));
  SetTextColor(info->hDC,disabled?kMuted:(primary||selected&&!tab)?kAccentText:selected?kAccent:kText);
  SetBkMode(info->hDC,TRANSPARENT);SelectObject(info->hDC,ui.font);
  wchar_t label[150]{};GetWindowTextW(info->hwndItem,label,150);
  DrawTextW(info->hDC,label,-1,&info->rcItem,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
  if(info->itemState&ODS_FOCUS)DrawFocusRect(info->hDC,&info->rcItem);
}
LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
  auto* ui=reinterpret_cast<Ui*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  switch(msg){
    case WM_CREATE:{
      auto app=std::make_unique<Ui>();app->host=hwnd;app->dpi=kb::ui::Dpi(hwnd);
      app->hadConfig=kb::ReadConfig(app->config);
      SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app.get()));
      CreateControls(*app);
      try{app->scanner.Start();SetTimer(hwnd,1,900,nullptr);}catch(...){SetState(*app,L"蓝牙不可用，仍可使用 RustDesk API 解锁。 ");}
      app.release();return 0;
    }
    case WM_GETMINMAXINFO:{
      auto* mm=reinterpret_cast<MINMAXINFO*>(l);
      const UINT dpi=ui?ui->dpi:kb::ui::Dpi(hwnd);
      mm->ptMinTrackSize={kb::ui::Scale(dpi,950),kb::ui::Scale(dpi,790)};return 0;
    }
    case WM_SIZE:if(ui){Layout(*ui);InvalidateRect(hwnd,nullptr,TRUE);}return 0;
    case WM_DPICHANGED:if(ui){
      ui->dpi=HIWORD(w);
      auto suggested=reinterpret_cast<const RECT*>(l);
      SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,
          suggested->right-suggested->left,suggested->bottom-suggested->top,
          SWP_NOZORDER|SWP_NOACTIVATE);
      Appearance(*ui);Layout(*ui);return 0;
    }break;
    case WM_SETTINGCHANGE:case WM_THEMECHANGED:if(ui){
      Appearance(*ui);InvalidateRect(hwnd,nullptr,TRUE);return 0;
    }break;
    case WM_ERASEBKGND:return 1;
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
      if(id==cRemote||id==cBleTab){SetPage(*ui,id==cRemote);return 0;}
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
bool Elevated(){
  HANDLE token=nullptr;
  if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
  TOKEN_ELEVATION elevation{};DWORD length=0;
  const bool ok=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&length)!=FALSE;
  CloseHandle(token);return ok&&elevation.TokenIsElevated!=0;
}
int RelaunchElevated(const wchar_t* argument,bool wait){
  wchar_t file[32768]{};
  if(!GetModuleFileNameW(nullptr,file,32768))return 1;
  SHELLEXECUTEINFOW info{sizeof(info)};
  info.fMask=SEE_MASK_NOCLOSEPROCESS;
  info.lpVerb=L"runas";info.lpFile=file;info.lpParameters=argument;
  info.nShow=SW_SHOWNORMAL;
  if(!ShellExecuteExW(&info))return 1;
  if(!wait){CloseHandle(info.hProcess);return 0;}
  WaitForSingleObject(info.hProcess,INFINITE);
  DWORD result=1;GetExitCodeProcess(info.hProcess,&result);
  CloseHandle(info.hProcess);return static_cast<int>(result);
}
int APIENTRY wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int show){
  int argc=0;
  LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
  if(argc>1&&argv){
    const std::wstring arg=argv[1];
    LocalFree(argv);
    if(arg==L"--service")return kb::RunRustDeskService();
    if(arg==L"--lock")return LockWorkStation()?0:1;
    if(arg==L"--self-test"){
      const auto colors=kb::ui::Colors();
      return (kb::ui::Scale(144,20)==30&&colors.text!=colors.background)?0:1;
    }
    if(arg==L"--install"||arg==L"--uninstall"){
      if(!Elevated())return RelaunchElevated(arg.c_str(),true);
      std::wstring result;
      const bool ok=arg==L"--install"?kb::InstallApplication(result):kb::UninstallApplication(false,result);
      MessageBoxW(nullptr,result.c_str(),L"Kenvix Bluetooth Unlock",
          MB_OK|(ok?MB_ICONINFORMATION:MB_ICONERROR));
      return ok?0:1;
    }
  }else if(argv)LocalFree(argv);
  if(!Elevated())return RelaunchElevated(L"--settings",false);
  winrt::init_apartment(winrt::apartment_type::single_threaded);
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&common);
  WNDCLASSEXW cls{sizeof(cls)};
  cls.hInstance=inst;cls.lpfnWndProc=WndProc;cls.lpszClassName=L"KenvixBluetoothUnlockSettings";
  cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
  cls.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  if(!RegisterClassExW(&cls))return 1;
  const UINT dpi=kb::ui::Dpi(nullptr);
  HWND window=CreateWindowExW(0,cls.lpszClassName,L"Kenvix Bluetooth Unlock · 设置",
                   WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,
                   kb::ui::Scale(dpi,1120),kb::ui::Scale(dpi,860),
                   nullptr,nullptr,inst,nullptr);
  if(!window)return 1;
  kb::ui::Titlebar(window,kb::ui::SystemDark());
  ShowWindow(window,show);UpdateWindow(window);
  MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){
    if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
  }
  winrt::uninit_apartment();
  return static_cast<int>(msg.wParam);
}
