# 使用 RustDesk API 解锁和锁定 Windows

本功能将 RustDesk API 的远程连接事件接入 Windows 原生 Credential Provider。您可以在没有蓝牙设备的情况下使用，也可以同时保留蓝牙解锁。

## 使用前准备

- 已部署支持 `connection.authenticated` 和 `connection.ended` 事件的 RustDesk API 服务器。
- 服务器通过有效的 HTTPS 证书提供访问。Windows 使用 WinHTTP 验证服务器证书，并禁止重定向 SSE 请求。
- RustDesk 客户端向 API 服务正常提交连接审计信息。仅连接服务器但没有审计上报时，Windows 无法收到相应事件。
- Windows 10/11 x64、已安装本项目 Credential Provider 和 `KenvixRustDeskUnlock` 系统服务。
- Windows 登录账户有可验证的账户密码。本功能目前支持传统 Windows 本地账户和 Active Directory 域账户。

## 一、生成 SSE API Key

1. 登录 RustDesk API 控制台。
2. 打开 **账户 → 消息推送 → SSE 实时订阅**。
3. 新建个人密钥，复制以 `rdapi_` 开头的 API Key。建议为 Windows 自动解锁单独生成密钥。
4. 如果不再使用自动解锁，可在控制台撤销该密钥。

密钥只需要 `events.read` 权限。Windows 通过以下地址保持 SSE 连接：

```http
GET /api/events/v1/stream?events=connection.authenticated,connection.ended
Authorization: Bearer rdapi_你的密钥
Accept: text/event-stream
```

API Key 只保存在本机受限制的配置目录中，并使用 Windows DPAPI 加密。程序不会将密钥附加到 URL 中。

## 二、设置 Windows 端

1. 以管理员身份打开 `KenvixBleSettings.exe`。
2. 选择 **仅 RustDesk**、**手机验证** 或 **手环检测**。若只需 RustDesk 解锁，可以选择 **仅 RustDesk**，电脑不必安装蓝牙适配器。
3. 输入 Windows 账户名、计算机名或域名、账户密码并保存。密码经过本机验证后加密保存。
4. 点击主窗口右上角的 **RustDesk API 设置**。
5. 打开 RustDesk 自动解锁，填写下面的信息并保存。

| 设置 | 填写内容 |
| --- | --- |
| 服务器地址 | `https://desk.example.com`；部署在反向代理的子路径时可带路径前缀 |
| SSE API Key | 步骤一生成的 `rdapi_...` 密钥；已有保存记录时可以留空 |
| 本设备 RustDesk ID | 这台 Windows 电脑在 RustDesk 客户端中显示的设备 ID |
| 允许的连接方设备 ID | 可选；填写后仅允许该 RustDesk 设备连接时自动解锁 |
| 连接全部结束时自动锁定 | 默认开启。多个授权连接同时存在时，最后一个连接结束才会锁定 |

点击 **测试 SSE 连接** 可以验证服务器地址、TLS 证书及 API Key 是否有效。测试操作会建立一次 SSE 订阅并检查 HTTP 状态，不会发送远程连接事件或触发解锁。

保存 RustDesk 设置后，Windows 后台服务会重新读取配置。若服务尚未安装或已经停止，可使用管理员 PowerShell 执行 `Start-Service KenvixRustDeskUnlock`。

## 三、解锁和自动锁定

- 您在目标电脑上使用 `Win + L` 锁定 Windows。
- 远程设备使用 RustDesk 建立连接并完成认证。服务器发送 `connection.authenticated` 后，Windows 服务检查目标设备 ID、可选连接方 ID 和事件时间。如果目标 Windows 账户已经登录并处于锁定状态，服务向 Credential Provider 发放一次性短时解锁许可。
- Credential Provider 在原生 Windows 登录界面使用已加密保存的 Windows 密码提交系统身份验证。系统决定是否允许解锁。
- 远程连接结束时，服务器发送 `connection.ended`。本机服务按连接编号更新活动连接记录。如果已开启自动锁定，且没有其他已认证的连接，服务就在该账户的本地交互式桌面调用 `LockWorkStation`。

两种事件的 JSON 数据示例：

```json
{
  "id": "a246f8c4...",
  "type": "connection.authenticated",
  "occurred_at": "2026-11-13T02:30:00Z",
  "device_id": "123456789",
  "from_peer": "987654321",
  "connection_id": 4031
}
```

`connection.ended` 使用相同格式，`type` 改为 `connection.ended`；连接结束审计可能省略 `from_peer`，本程序仍会根据 `connection_id` 清理对应连接。

## 连接中断后的处理

程序通过 WinHTTP 保持长时间 SSE 请求。RustDesk API 每约 20 秒发送一次心跳。客户端将读取超时设置为 35 秒，连接中断后按逐渐增加的间隔重新连接，最长间隔为 60 秒。断线期间不会定时查询事件列表。

为了避免将旧事件当作新的解锁请求，程序会拒绝过期超过 90 秒、未来超过 15 秒、设备 ID 不符和重复的事件。收到错误的、缺少字段的 JSON 时不会改变锁屏状态。

**如果 SSE 连接断开，而本机仍记录着有效远程会话，且已经开启“连接全部结束时自动锁定”，程序会立即请求锁定电脑。** 因此，网络不稳定时可能提前锁定。连接恢复后需要收到新的认证成功事件，才会再次自动解锁。服务器断线期间不会补发历史事件。

如果多个 RustDesk API 实例共同处理设备事件，应按服务端文档配置共享 Redis，以确保连接到某个实例的 SSE 客户端能够收到其他实例处理的事件。

## 安全限制

- RustDesk API 的认证成功事件以服务器接收的客户端审计数据为依据。请保护 RustDesk 服务器、被控设备及 API Key；如对服务器或审计数据可信度有疑虑，关闭自动解锁。
- SSE API Key 必须通过 HTTPS 传输。程序不会接受 HTTP 明文地址，也不会在 HTTP 跳转后继续发送密钥。
- 配置中可以限制具体连接方设备 ID。建议为个人电脑设置，减少其他拥有远程连接权限的设备触发解锁。
- 服务通过目标设备 ID 和当前 Windows 控制台账户检查解锁条件。远程桌面会话、开机登录和已经注销的账户不能触发自动登录。
- 远程结束事件只表明 RustDesk 审计系统记录了连接结束。服务器未收到结束记录或 SSE 仍在正常传输却漏发事件时，Windows 可能无法按预期立即锁定。重要设备仍应启用 Windows 自动锁屏策略。
- Windows 账户密码仍由 Credential Provider 提交给 Windows。更改密码后，需在主设置程序中重新输入并保存。

## 故障排查

| 现象 | 处理方法 |
| --- | --- |
| 测试返回 HTTP 401 | 检查 API Key 是否具有 `events.read` 权限、是否已撤销或过期 |
| 连接测试无法通过 | 检查 HTTPS 证书、反向代理的 SSE 设置、WinHTTP 代理配置和服务器是否在线 |
| 能测试连接但无法解锁 | 检查本设备 RustDesk ID、连接方限制、服务器是否确实发送 `connection.authenticated`，以及目标 Windows 会话是否锁定 |
| 连接结束但没有锁定 | 检查服务是否运行、是否开启自动锁定、是否仍有其他已认证连接，以及服务器是否上报 `connection.ended` |
| 更换 Windows 账户密码后失败 | 重新运行主设置程序，输入新密码并保存 |
| 禁用后仍保持 SSE 连接 | 确认设置已经保存，检查 `KenvixRustDeskUnlock` 服务状态并重新启动服务 |

在管理员 PowerShell 中执行以下命令查看服务状态：

```powershell
Get-Service KenvixRustDeskUnlock
```

如需检查系统当前的 WinHTTP 代理设置：

```powershell
netsh winhttp show proxy
```
