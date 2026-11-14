# 安装、使用与故障处理

## Windows 编译与安装

使用 Windows 10/11 x64、Visual Studio 2022（安装“使用 C++ 的桌面开发”工作负载）和 Windows SDK，在项目根目录执行：

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

将 `BluetoothUnlock.exe` 与 `BluetoothUnlockCredentialProvider.dll` 放在同一目录。双击打开 EXE，在“服务管理”页面点击“安装服务与登录组件”。根据 Windows 提示授予管理员权限。

服务管理页面包含实时运行状态、登录组件注册状态、重新安装／修复、启动、停止、卸载以及刷新状态。卸载前可选择是否保留已保存的设置。管理操作会在后台线程运行，界面显示完成结果。

安装程序会将二进制文件复制到 `C:\Program Files\BluetoothUnlock`，安装 `BluetoothUnlockService` 后台服务，并注册原生 Windows 凭据提供程序。

打开安装目录中的 `BluetoothUnlock.exe`。如果当前用户没有管理员权限，设置程序会请求提升权限。统一设置窗口有“蓝牙解锁”“RustDesk API”和“服务管理”三个页面。蓝牙页面可选择手机验证、手环检测或仅 RustDesk，并可设置 RSSI 阈值；随后填写 Windows 登录账户和密码。密码只保存在本机，先通过 DPAPI 加密，TPM 可用时再使用 TPM 保护的密钥加密。界面使用 Windows 应用主题设置，自动切换浅色与深色模式，并按显示器 DPI 缩放字体和控件。

建议先在测试机器或虚拟机中检查 Credential Provider 注册和登录界面。真实蓝牙功能还需要一台物理 Windows 电脑。

## Android 编译与使用

使用 Android Studio，安装 JDK 17 和 Android SDK API 37。打开独立的 [BluetoothUnlock-Android](https://github.com/HSOS6Miyabi/BluetoothUnlock-Android) 仓库根目录同步 Gradle 项目。也可以在已安装 Gradle 9.6.0 的环境中执行：

```bash
gradle :app:testDebugUnitTest :app:assembleDebug
```

APK 生成位置：`app/build/outputs/apk/debug/app-debug.apk`。安装后允许蓝牙连接与蓝牙广播权限。如果使用手动确认模式，还需要允许通知权限，并在 Android 系统中配置生物识别或锁屏密码。

主页面可选择自动响应或手动确认，可启用“开机自动启动”，并可以复制手机公钥指纹。后台服务运行时显示常驻通知。要停止广播，点击应用中的“停止蓝牙认证”或通知中的“停止”。

## 蓝牙设备列表

Windows 显示“设备名称、MAC 地址、信号强度”三列表格，并过滤没有名称的扫描结果。Android 通过蓝牙扫描响应发送手机的蓝牙名称。部分手环不广播名称，因此不会出现在列表中。左侧设备面板宽于右侧账户面板；调整窗口大小或 DPI 时会自动重新计算列宽。

## TPM 凭据保护

电脑具备可用 TPM 时，程序优先使用 TPM 的 RSA 密钥和 AES-GCM 保护已通过 DPAPI 加密的密码与 RustDesk API Key。设置界面会显示账户密码的实际保护方式；没有可用 TPM 时继续使用 Windows DPAPI。旧版设置可继续读取，再次保存后会尝试升级。清除 TPM 或丢失硬件密钥后，需要重新输入凭据。

## RSSI 设置

RSSI 数值越接近 `0 dBm`，表示接收信号通常越强。例如，`-55 dBm` 要求比 `-80 dBm` 更强的信号。推荐从 `-70 dBm` 开始，结合实际使用位置调整。信号强度不等于实际距离，墙壁、手臂遮挡和蓝牙硬件均会影响读数。

## 账户设置

`用户名` 填写 Windows 登录用户名；`计算机名或域名` 可填写 `.` 代表本机，或填写实际 Active Directory 域名。该账户必须已登录本机，并且在设置前能通过账户密码完成验证。配置后锁定电脑 (`Win + L`) 进行测试。

手机更换、Android Keystore 数据重置或重新安装应用后，手机的身份密钥可能改变，需要重新读取公钥并核对指纹。

## 常见问题

| 情况 | 检查方法 |
|---|---|
| Windows 找不到手机 | 检查手机是否开启蓝牙、应用后台服务是否正在运行、手机是否支持 BLE 广播；短暂关闭并重新开启手机服务 |
| 手机已发现但无法读取公钥 | 检查 GATT 服务注册是否成功，确认 Windows 蓝牙驱动程序允许访问手机的 GATT 服务 |
| Windows 设置程序无法保存 | 以管理员身份启动，确认账户名、域名和登录密码有效 |
| 手环已经配对但不出现在设备列表 | 确认手环会持续发送 Windows 可以接收的 BLE 广播；部分手环的私有协议或随机地址无法兼容 |
| RSSI 看起来足够强仍未解锁 | 需要至少两次新近 BLE 广播；高安全性模式还必须完成公钥核对和挑战签名验证 |
| 手机未收到手动确认通知 | 检查通知权限、系统通知渠道以及手机的省电设置 |
| Windows 登陆界面没有新选项 | 检查 DLL 是否位于安装目录以及 Credential Provider 注册项是否存在，尝试重新登录或重新启动 |
| 修改 Windows 密码后无法使用 | 管理员运行 Windows 设置程序，输入新密码并保存 |
| 电脑无法正常进入登录界面 | 使用系统安全模式或 Windows 恢复环境撤销该凭据提供程序的注册，见下节 |

## 卸载与恢复

打开设置窗口中的“服务管理”，点击“卸载程序”。确认后会移除后台服务和原生登录组件。勾选“卸载时保留设置”可保留当前的身份验证配置。也可以在管理员 PowerShell 中运行：

```powershell
& 'C:\Program Files\BluetoothUnlock\BluetoothUnlock.exe' --uninstall
```

程序会停止并删除后台服务，移除 Credential Provider 和 COM 注册信息，删除安装文件及本程序配置。需要保留配置时，可以使用 `--uninstall-keep-settings`。系统正在使用的文件会安排在重新启动后删除。卸载不修改 Windows 原有登录方式。

如果无法进入通常的桌面，可从安全模式或 Windows 恢复环境执行注册表操作。仅删除本项目的注册表项：

```powershell
Remove-Item -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{12E137F0-91AB-4B74-A37F-019B284A5C8E}' -Recurse -Force
Remove-Item -LiteralPath 'HKLM:\SOFTWARE\Classes\CLSID\{12E137F0-91AB-4B74-A37F-019B284A5C8E}' -Recurse -Force
```

在 Windows 恢复环境离线编辑注册表时，需要先正确加载已安装 Windows 的 SOFTWARE 配置单元；以上 PowerShell 路径只适用于正在运行的目标 Windows 系统。

## 版本适用范围与测试

项目可以在不安装 Windows SDK 的其他系统上执行跨平台协议单元测试，但 Windows DLL 和 EXE 只有在 Windows SDK 环境中才能完成编译和运行。Android APK 需要 Android SDK。持续集成配置分别负责 Windows Release 编译、Windows 协议测试、Android JVM 测试和 Android APK 构建。
