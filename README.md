# Kenvix Bluetooth Unlock for Windows

Windows 10/11 原生锁屏的蓝牙与 RustDesk API 解锁程序。Windows Credential Provider 集成在系统原有的 LogonUI 中，保留密码、PIN 和 Windows Hello 登录方式。

## 功能

- Windows C++20、Win32 与 Windows Runtime，提供原生登录凭据提供程序和设置窗口。
- 兼容蓝牙低安全性手环存在检测和 Android Keystore ECDSA P-256 挑战响应认证。
- 调整 RSSI 信号强度门槛。
- 可选 RustDesk API SSE 自动解锁和连接结束后锁定；支持目标设备 ID、来源设备 ID 限制与多连接状态管理。
- RustDesk API Key 和 Windows 登录凭据使用 Windows DPAPI 加密保存。

## 构建及测试

使用 Visual Studio 2022 + Windows SDK：

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

以管理员身份运行 `windows/install.ps1`，按设置程序提示绑定设备。卸载时运行 `windows/uninstall.ps1`。

GitHub Actions 将编译 x64 版本、运行 C++ 自动化测试，并在默认分支构建成功且对应版本尚未发布时创建 Release。每个版本的压缩包包含 DLL、EXE、安装脚本及文档。安装 Credential Provider 前请确认能够使用密码正常登录 Windows，首次安装建议在测试电脑执行。

## RustDesk API

可设置 RustDesk API 服务器 HTTPS 地址、个人 `events.read` API Key、目标电脑设备 ID、允许的连接方 ID 和自动锁定选项。详情见 [RustDesk 接入说明](docs/RUSTDESK_API_UNLOCK.md)。

**安全提示：** RustDesk API 普通 `connection.authenticated` 事件来自客户端审计报告。将其用于自动解锁时，需评估服务器对审计请求的身份验证措施。对于无法确认可信来源的事件，建议同时启用本机身份验证要求，或选择可信事件模式。

## Android 客户端

Android 应用位于独立仓库：[BluetoothUnlock-Android](https://github.com/HSOS6Miyabi/BluetoothUnlock-Android)。

## 其他文档

[协议](docs/PROTOCOL.md) · [安全](docs/SECURITY.md) · [测试](docs/TESTING.md) · [安装和故障处理](docs/OPERATIONS.md)。
