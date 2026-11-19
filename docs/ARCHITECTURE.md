# Windows 架构与代码导航

## 运行结构

```text
Windows 设置界面（BluetoothUnlock.exe）
  ├─ 蓝牙解锁设置：扫描设备、选择模式、阈值、核对 Android 公钥
  ├─ RustDesk API 设置：HTTPS、events.read API Key、目标设备 ID、事件选项
  └─ 服务管理：安装、修复、启动、停止、卸载
          │ 保存受保护的本机配置
          ▼
%ProgramData%\BluetoothUnlock\configuration.dat / rustdesk.dat
          │
          ├─ BluetoothUnlockService（同一 EXE 的 --service 模式）
          │    └─ WinHTTP SSE → 连接事件判别 → 一次性解锁许可 / 锁定请求
          │
          └─ BluetoothUnlockCredentialProvider.dll（由 Windows LogonUI 加载）
               ├─ BLE 广播与 RSSI → Android GATT 随机挑战签名验证
               ├─ RustDesk 一次性许可
               └─ 提交已保存的 Windows 账户凭据至系统身份验证
```

仅在已登录用户的本机控制台锁定会话中触发自动解锁。Windows 仍执行其原生身份验证，密码、PIN 和 Windows Hello 保持可用。

## 文件与职责

| 文件 | 功能 | 修改时重点 |
| --- | --- | --- |
| `windows/settings.cpp`、`windows/ui_common.h` | Win32 设置窗口、三页导航、主题、DPI 与设备列表 | 逐显示器 DPI、主题切换、选择状态和 UI 线程响应 |
| `windows/ble.cpp`、`windows/ble.h` | WinRT 主动 BLE 扫描、Android GATT 客户端和挑战读写 | WinRT 资源释放、扫描中断恢复、超时与蓝牙驱动兼容性 |
| `core/ble_presence.cpp`、`core/ble_presence.h` | 广播名称合并、设备驻留与历史 RSSI 显示 | 3 分钟列表驻留和 4 秒安全新鲜度相互独立 |
| `windows/credential_provider.cpp`、`windows/credential_provider.def` | Windows Credential Provider COM DLL | `CPUS_LOGON`／`CPUS_UNLOCK_WORKSTATION`、登录凭据封装及安全退出 |
| `windows/security.cpp` | 随机数、Android ECDSA 签名验证、会话状态 | 不接受仅凭设备名称或 MAC 的高安全性认证 |
| `windows/storage.cpp`、`windows/storage.h` | Windows 登录凭据及设备设置的读写 | 配置文件格式与旧版迁移 |
| `windows/tpm_protection.cpp` | TPM 密钥与 AES-GCM 加密封装 | 无 TPM 时 DPAPI 兼容、清除 TPM 后凭据恢复 |
| `windows/remote_http.cpp` | WinHTTP HTTPS SSE 订阅 | 证书校验、禁止重定向、凭据不进入 URL |
| `core/remote_protocol.cpp` | SSE 解析、去重、连接跟踪和事件时间校验 | 多连接、断线、结束事件及可信事件类型 |
| `windows/remote_service.cpp`、`windows/remote_ipc.cpp` | RustDesk Windows 服务和跨进程一次性许可 | 服务与桌面会话隔离，避免重复解锁 |
| `windows/remote_storage.cpp` | SSE API Key 与相关配置 | DPAPI/TPM、旧版配置与兼容读取 |
| `windows/service_manager.cpp`、`windows/service_settings.cpp` | 服务、登录 DLL 注册、GUI 安装及卸载 | 提升权限、SCM 状态、错误处理和旧版迁移 |
| `core/protocol.cpp`、`core/protocol.h` | Windows/Android BLE 二进制认证协议 | 与 Android `Protocol.kt` 和共用测试数据一致 |
| `tests/` | BLE、RustDesk、CNG、TPM 与跨端测试 | 每项安全条件应有可自动执行的测试 |

## 蓝牙认证流程

1. Android 前台服务广播约定服务 UUID，并在可能时通过扫描响应发送手机名称。
2. Windows 主动扫描合并名称与服务 UUID；普通未命名设备不显示，广播应用 UUID 的未命名设备可使用占位名称。
3. 设备行保留约 3 分钟，显示历史 RSSI 时标明“上次”。**仅最近 4 秒内的有效广播**可以参与解锁判断。
4. 低安全性模式检查已配对设备地址及 RSSI；高安全性模式从 Android 读取公钥，发送新的随机挑战并用 CNG 验证 ECDSA P-256 签名。
5. Credential Provider 再次检查目标用户及本机锁定会话，随后由 Windows 验证已配置的登录凭据。

协议的完整字节格式见 [PROTOCOL](PROTOCOL.md)。历史签名消息前缀 `KENVIX-BLE-UNLOCK/v1:` 属于线上协议内容，变更它必须同时修改两端和测试数据，并安排协议版本迁移。Android 的包标识和 Keystore 别名也属于既有应用身份，不随界面品牌名变化。

## RustDesk 事件流程

服务以本机受限配置中的 `events.read` 密钥连接 SSE；`ConnectionTracker` 按目标设备、连接方、连接编号、事件标识和发生时间检查事件。认证成功且会话已锁定时，服务向凭据提供程序发放一次性短时许可；最后一条有效连接结束，或连接中断且仍存在活跃连接时，根据设置请求锁定本机桌面。

`connection.authenticated` / `connection.ended` 属于常规审计事件；`connection.trusted_authenticated` / `connection.trusted_ended` 为客户端已识别的可选事件类型。选择“仅使用可信服务器事件”时，服务端必须真正产生相应事件，并保证上报来源可靠。仅能订阅 SSE 并不说明事件自身具有足够的认证保证。参见 [RustDesk 接入](RUSTDESK_API_UNLOCK.md) 和 [安全说明](SECURITY.md)。

## 配置及兼容约束

- 本机新配置目录为 `%ProgramData%\BluetoothUnlock`，仅允许 SYSTEM 和管理员访问；密码先使用机器级 DPAPI 加密，有可用 TPM 时再使用平台密钥保护。
- Windows 服务名为 `BluetoothUnlockService`，凭据提供程序 CLSID 为 `{12E137F0-91AB-4B74-A37F-019B284A5C8E}`。新版本安装前必须保护既有登录方式。
- 配置内部标识、旧版读取逻辑、API Key 的加密附加数据、跨进程对象名称需要兼容迁移。只修改显示名称时不要改动这些内部标识。
- Android Keystore 私钥不传输；Windows 只保存已核对的公钥。手机重装或生成新密钥后需要重新绑定。

详细操作与恢复步骤见 [OPERATIONS](OPERATIONS.md)。
