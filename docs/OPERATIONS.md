# 安装、使用与故障处理

## Windows 编译与安装

使用 Windows 10/11 x64、Visual Studio 2022（安装“使用 C++ 的桌面开发”工作负载）和 Windows SDK，在项目根目录执行：

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

使用**管理员权限**运行 `windows\install.ps1`，脚本会安装原生设置程序并向 Windows 注册一个额外的 Credential Provider：

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\install.ps1
```

打开 `C:\Program Files\Kenvix\BluetoothUnlock\KenvixBleSettings.exe`，首次设置时同样使用管理员权限。选择解锁模式、授权设备与 RSSI，再填写 Windows 账户信息和该账户的密码。账户密码只在本地保存，经 DPAPI 加密。

建议先在测试机器或虚拟机中检查 Credential Provider 注册和登录界面。真实蓝牙功能还需要一台物理 Windows 电脑。

## Android 编译与使用

使用最新版稳定版 Android Studio，安装 JDK 17 和 Android SDK API 37。打开 `android/` 文件夹同步 Gradle 项目。也可以在已安装 Gradle 9.6.0 的环境中执行：

```bash
cd android
gradle :app:testDebugUnitTest :app:assembleDebug
```

APK 生成位置：`android/app/build/outputs/apk/debug/app-debug.apk`。安装后允许蓝牙连接与蓝牙广播权限。如果使用手动确认模式，还需要允许通知权限，并在 Android 系统中配置生物识别或锁屏密码。

主页面可选择自动响应或手动确认，可启用“开机自动启动”，并可以复制手机公钥指纹。后台服务运行时显示常驻通知。要停止广播，点击应用中的“停止蓝牙认证”或通知中的“停止”。

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

以管理员身份运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\uninstall.ps1
```

脚本会移除本项目的 Credential Provider 和 COM 注册信息，删除程序文件及加密配置。重新启动 Windows 后生效。如需保留个人配置文件，可增加 `-KeepSettings` 参数。卸载不修改系统已有的登录方式。

如果无法进入通常的桌面，可从安全模式或 Windows 恢复环境执行注册表操作。仅删除本项目的注册表项：

```powershell
Remove-Item -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{12E137F0-91AB-4B74-A37F-019B284A5C8E}' -Recurse -Force
Remove-Item -LiteralPath 'HKLM:\SOFTWARE\Classes\CLSID\{12E137F0-91AB-4B74-A37F-019B284A5C8E}' -Recurse -Force
```

在 Windows 恢复环境离线编辑注册表时，需要先正确加载已安装 Windows 的 SOFTWARE 配置单元；以上 PowerShell 路径只适用于正在运行的目标 Windows 系统。

## 版本适用范围与测试

项目可以在不安装 Windows SDK 的其他系统上执行跨平台协议单元测试，但 Windows DLL 和 EXE 只有在 Windows SDK 环境中才能完成编译和运行。Android APK 需要 Android SDK。持续集成配置分别负责 Windows Release 编译、Windows 协议测试、Android JVM 测试和 Android APK 构建。
