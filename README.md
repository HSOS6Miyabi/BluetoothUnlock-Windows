# Bluetooth Unlock for Windows

Windows 10/11 原生锁屏的蓝牙与 RustDesk API 解锁程序。Windows Credential Provider 集成在系统原有的 LogonUI 中，保留密码、PIN 和 Windows Hello 登录方式。

## 功能

- Windows C++20、Win32 与 Windows Runtime，提供原生登录凭据提供程序和统一设置窗口。
- 兼容蓝牙低安全性手环存在检测和 Android Keystore ECDSA P-256 挑战响应认证。
- 蓝牙设备按名称、MAC 地址和信号强度显示为表格。普通 BLE 设备需要广播名称；手机广播本程序的服务 UUID 时，即使系统未返回名称，也会显示为“解锁服务设备（名称未广播）”。
- 设置 RSSI 最低信号强度；界面自动跟随 Windows 应用浅色或深色模式，支持逐显示器 DPI 缩放与动态调整。
- 可选 RustDesk API SSE 自动解锁和连接结束后锁定；支持目标设备 ID、来源设备 ID 限制与多连接状态管理。
- Windows 账户密码与 RustDesk API Key 使用 DPAPI 加密；支持 TPM 的电脑还会使用 TPM 中的不可导出密钥进行保护。
- “服务管理”页面可查看后台服务和登录组件状态，完成安装、修复、启动、停止及卸载。卸载时可选择保留设置。

## 构建及测试

使用 Visual Studio 2022 + Windows SDK：

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

构建输出仅包含一个可执行文件 `BluetoothUnlock.exe` 和 Windows 原生登录必需的 `BluetoothUnlockCredentialProvider.dll`。将二者放在同一目录，直接运行 `BluetoothUnlock.exe` 打开设置窗口，允许 Windows 请求管理员权限。在 **服务管理** 页面点击“安装服务与登录组件”，程序将安装后台服务并注册 Windows 原生登录组件。

统一窗口包含 **蓝牙解锁**、**RustDesk API** 和 **服务管理** 三个页面。服务管理页面会显示运行状态，并提供启动、停止、重新安装、修复及卸载操作。卸载时可以保留本机配置以供重新安装使用，无需运行命令行。

也可以使用 `--install`、`--uninstall` 命令，或调用 `windows/install.ps1`、`windows/uninstall.ps1`。

GitHub Actions 将编译 x64 版本、运行 C++ 自动化测试，并在默认分支构建成功且对应版本尚未发布时创建 Release。每个版本的压缩包包含一个 EXE、系统登录扩展 DLL、安装脚本及文档。安装 Credential Provider 前请确认能够使用密码正常登录 Windows，首次安装建议在测试电脑执行。

## 蓝牙设备驻留与扫描恢复

Windows 保留最近发现的设备，连续 6 个 30 秒时间段收不到广播后才从列表中移除（约 3 分钟）。暂停收到广播时，信号栏标为“上次 xx dBm”，方便辨认和再次选择设备。为了保护解锁安全，真正触发解锁仍要求最近 4 秒有有效广播，并完成原有的配对检查或随机挑战签名认证；历史显示记录不参与解锁。

程序使用 Windows 主动 BLE 扫描，并自动尝试恢复被系统中断的扫描。设备列表不根据设备类别过滤；普通无名称广播仍不显示。Windows 发现手机需要 Android 应用在后台提供 BLE 外围设备广播，普通系统蓝牙开关处于开启状态并不保证手机会被 BLE 扫描发现。

## TPM 与旧版配置

Windows 优先通过 Microsoft Platform Crypto Provider 在 TPM 内生成不可导出的计算机级 RSA 密钥，并使用 RSA-OAEP-SHA256 与 AES-GCM 保护经过 DPAPI 加密的账户密码和 RustDesk API Key。TPM 不可用时继续使用 DPAPI，设置页面显示实际使用的保护方式。安装程序会迁移旧版安装文件及保存的配置；旧凭据再次保存时会尝试升级加密。

TPM 保护静态保存的密钥，但本机管理员或 SYSTEM 进程仍可能通过系统接口使用这些密钥。建议为管理员账户配置可靠的登录方式，并启用 BitLocker。

## Windows 启动参数

| 参数 | 功能 |
| --- | --- |
| 无参数或 `--settings` | 打开带有蓝牙、RustDesk API 和服务管理页面的统一设置窗口 |
| `--install` | 安装 EXE、凭据提供程序 DLL，注册系统登录扩展及 Windows 后台服务 |
| `--uninstall` | 停止并删除后台服务、注销系统登录组件、删除本程序配置 |
| `--uninstall-keep-settings` | 卸载程序和服务，保留配置以便重新安装 |
| `--service` | 由 Windows 服务管理器启动 SSE 后台服务 |
| `--lock` | 在当前交互式用户会话请求锁定 Windows |
| `--self-test` | 执行无需安装系统服务的启动自检 |

Windows 服务名为 `BluetoothUnlockService`，进程使用同一 EXE 的 `--service` 参数运行。设置窗口在需要写入系统级配置时自动请求管理员权限；服务启动和锁定操作不请求交互式提升权限。

## RustDesk API

可设置 RustDesk API 服务器 HTTPS 地址、个人 `events.read` API Key、目标电脑设备 ID、允许的连接方 ID 和自动锁定选项。详情见 [RustDesk 接入说明](docs/RUSTDESK_API_UNLOCK.md)。

**安全提示：** RustDesk API 普通 `connection.authenticated` 事件来自客户端审计报告。将其用于自动解锁时，需评估服务器对审计请求的身份验证措施。对于无法确认可信来源的事件，建议同时启用本机身份验证要求，或选择可信事件模式。

## Windows 和 Android 认证兼容性

两个仓库使用相同的一组 P-256 签名测试数据。Windows 在原生 CNG 中验证 Android 格式的公钥和 DER 签名，Android 在 JVM 中验证相同的签名、挑战和公钥指纹。详情见 [测试说明](docs/TESTING.md)。

## Android 客户端

Android 应用位于独立仓库：[BluetoothUnlock-Android](https://github.com/HSOS6Miyabi/BluetoothUnlock-Android)。

## 其他文档

[协议](docs/PROTOCOL.md) · [安全](docs/SECURITY.md) · [测试](docs/TESTING.md) · [安装和故障处理](docs/OPERATIONS.md)。
